#ifndef _WIN32

#include "IAdapter.h"
#include "IIpcServer.h"
#include "IHardware.h"
#include <iostream>
#include <vector>
#include <atomic>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>

#include "PlatformUtils.h"
#include "UsbHardware.h"
#include <csignal>
#include <thread>
#include <memory>

namespace optifi {
namespace core {

std::function<void()> g_ShutdownCallback;
void LinuxSignalHandler(int) { if (g_ShutdownCallback) g_ShutdownCallback(); }

void PlatformUtils::SetupSignalHandler(std::function<void()> onShutdown) {
    g_ShutdownCallback = onShutdown;
    std::signal(SIGINT, LinuxSignalHandler);
    std::signal(SIGTERM, LinuxSignalHandler);
}

PlatformConfig PlatformUtils::GetDefaultConfig() {
    return {"/tmp/optifi.sock", "optifi0", "10.137.137.1", "255.255.255.0"};
}

// --- LINUX ADAPTER ---
class LinuxAdapter : public IAdapter {
public:
    LinuxAdapter(const std::string& name) : m_name(name), m_fd(-1) {}
    bool Initialize() override {
        m_fd = open("/dev/net/tun", O_RDWR);
        if (m_fd < 0) return false;
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
        strncpy(ifr.ifr_name, m_name.c_str(), IFNAMSIZ);
        if (ioctl(m_fd, TUNSETIFF, (void*)&ifr) < 0) { close(m_fd); return false; }
        return true;
    }
    bool StartSession(uint32_t bufferSize) override {
        m_buffer.resize(bufferSize);
        return true;
    }
    void Shutdown() override { if (m_fd >= 0) close(m_fd); }
    uint8_t* ReadPacket(uint32_t* outSize) override {
        struct pollfd pfd = { m_fd, POLLIN, 0 };
        if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(m_fd, m_buffer.data(), m_buffer.size());
            if (n > 0) { *outSize = n; return m_buffer.data(); }
        }
        return nullptr;
    }
    void ReleasePacket(const uint8_t*) override {}
    bool SendPacket(const uint8_t* data, uint32_t size) override {
        return write(m_fd, data, size) == (ssize_t)size;
    }
private:
    std::string m_name;
    int m_fd;
    std::vector<uint8_t> m_buffer;
};

// --- LINUX IPC SERVER ---
class LinuxIpcServer : public IIpcServer {
public:
    LinuxIpcServer(const std::string& path) : m_path(path), m_serverFd(-1), m_clientFd(-1), m_running(false) {}
    ~LinuxIpcServer() { StopListening(); }
    
    bool StartListening() override {
        m_running = true;
        m_thread = std::thread([this]() {
            m_serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
            unlink(m_path.c_str());
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, m_path.c_str(), sizeof(addr.sun_path)-1);
            bind(m_serverFd, (struct sockaddr*)&addr, sizeof(addr));
            listen(m_serverFd, 1);
            while (m_running) {
                struct pollfd pfd = { m_serverFd, POLLIN, 0 };
                if (poll(&pfd, 1, 500) > 0) {
                    int cli = accept(m_serverFd, nullptr, nullptr);
                    if (cli >= 0) {
                        m_clientFd = cli;
                        char buf[128];
                        while (m_running) {
                            struct pollfd cpfd = { m_clientFd, POLLIN, 0 };
                            if (poll(&cpfd, 1, 100) > 0) {
                                ssize_t n = read(m_clientFd, buf, sizeof(buf)-1);
                                if (n <= 0) break;
                                buf[n] = 0;
                                std::string cmd(buf);
                                if (cmd.find("SET_BATTERY") != std::string::npos) m_preset = DriverPreset::BATTERY;
                                else if (cmd.find("SET_PERFORMANCE") != std::string::npos) m_preset = DriverPreset::PERFORMANCE;
                                else if (cmd.find("SET_BALANCED") != std::string::npos) m_preset = DriverPreset::BALANCED;
                            }
                        }
                        close(m_clientFd);
                        m_clientFd = -1;
                    }
                }
            }
        });
        return true;
    }
    void StopListening() override {
        m_running = false;
        if (m_serverFd >= 0) { shutdown(m_serverFd, SHUT_RDWR); close(m_serverFd); }
        if (m_thread.joinable()) m_thread.join();
        unlink(m_path.c_str());
    }
    DriverPreset GetCurrentPreset() const override { return m_preset.load(); }
    bool BroadcastTelemetry(uint64_t cpu, size_t sz) override {
        int cli = m_clientFd.load();
        if (cli < 0) return false;
        std::string p = "TELEMETRY|" + std::to_string(sz) + "|" + std::to_string(cpu) + "\n";
        return write(cli, p.c_str(), p.length()) > 0;
    }
private:
    std::string m_path;
    int m_serverFd;
    std::atomic<int> m_clientFd;
    std::atomic<bool> m_running;
    std::thread m_thread;
    std::atomic<DriverPreset> m_preset{DriverPreset::BALANCED};
};

// --- MOCK HARDWARE ---
class MockHardware : public IHardware {
public:
    bool Initialize() override { std::cout << "[HW] Mock Init" << std::endl; return true; }
    void SendPacket(const uint8_t*, uint32_t) override {}
    int ReadPacket(uint8_t*, uint32_t) override { return 0; }
    void Disconnect() override { std::cout << "[HW] Mock Exit" << std::endl; }
};

// --- FACTORIES ---
std::unique_ptr<IAdapter> CreateAdapter(const std::string& name) { return std::make_unique<LinuxAdapter>(name); }
std::unique_ptr<IIpcServer> CreateIpcServer(const std::string& path) { return std::make_unique<LinuxIpcServer>(path); }
std::unique_ptr<IHardware> CreateHardware() { 
    auto usb = std::make_unique<hardware::UsbHardware>(0x303A, 0x4001, 0x01, 0x81);
    if (usb->Initialize()) {
        return usb;
    }
    std::cout << "[HW] OptiFi hardware not found. Falling back to MockHardware." << std::endl;
    return std::make_unique<MockHardware>(); 
}

} // namespace core
} // namespace optifi

#endif // _WIN32
