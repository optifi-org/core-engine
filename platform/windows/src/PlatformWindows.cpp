#include "IAdapter.h"
#include "IIpcServer.h"
#include "IHardware.h"
#include "PlatformUtils.h"
#include "UsbHardware.h"
#include <windows.h>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <memory>
#include <cstdlib>
#include <mutex>
#include <cstring>

// Wintun API Signatures
typedef void* WINTUN_ADAPTER_HANDLE;
typedef void* WINTUN_SESSION_HANDLE;
typedef WINTUN_ADAPTER_HANDLE (*WintunCreateAdapter_t)(const WCHAR*, const WCHAR*, const GUID*);
typedef void (*WintunCloseAdapter_t)(WINTUN_ADAPTER_HANDLE);
typedef WINTUN_SESSION_HANDLE (*WintunStartSession_t)(WINTUN_ADAPTER_HANDLE, DWORD);
typedef void (*WintunEndSession_t)(WINTUN_SESSION_HANDLE);
typedef HANDLE (*WintunGetReadWaitEvent_t)(WINTUN_SESSION_HANDLE);
typedef BYTE* (*WintunReceivePacket_t)(WINTUN_SESSION_HANDLE, DWORD*);
typedef DWORD (*WintunGetLastError_t)(void);
typedef void (*WintunReleaseReceivePacket_t)(WINTUN_SESSION_HANDLE, const BYTE*);
typedef BYTE* (*WintunAllocateSendPacket_t)(WINTUN_SESSION_HANDLE, DWORD);
typedef void (*WintunSendPacket_t)(WINTUN_SESSION_HANDLE, const BYTE*);

namespace optifi {
namespace core {

namespace {
constexpr uint8_t HOST_MAC[6] = { 0x02, 0x00, 0x00, 0x13, 0x37, 0x00 };
constexpr uint8_t USB_MAC[6]  = { 0x02, 0x00, 0x00, 0x13, 0x37, 0x01 };
constexpr uint16_t ETH_TYPE_IPV4 = 0x0800;
constexpr uint16_t ETH_TYPE_IPV6 = 0x86DD;

static void WriteEthType(uint8_t* frame, uint16_t type) {
    frame[12] = static_cast<uint8_t>(type >> 8);
    frame[13] = static_cast<uint8_t>(type & 0xFF);
}

static uint16_t ReadEthType(const uint8_t* frame) {
    return (static_cast<uint16_t>(frame[12]) << 8) | frame[13];
}
}

static bool RunSystemCommand(const std::string& command) {
    std::cout << "[PAL] " << command << std::endl;
    int code = std::system(command.c_str());
    if (code != 0) {
        std::cerr << "[PAL] Command failed with exit code " << code << ": " << command << std::endl;
        return false;
    }
    return true;
}

static bool RunPowerShellCommand(const std::string& script) {
    return RunSystemCommand("powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"" + script + "\"");
}

// --- PLATFORM UTILS ---
std::function<void()> g_WinShutdownCallback;
BOOL WINAPI WindowsConsoleHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT || dwCtrlType == CTRL_CLOSE_EVENT) {
        if (g_WinShutdownCallback) g_WinShutdownCallback();
        return TRUE;
    }
    return FALSE;
}

void PlatformUtils::SetupSignalHandler(std::function<void()> onShutdown) {
    g_WinShutdownCallback = onShutdown;
    SetConsoleCtrlHandler(WindowsConsoleHandler, TRUE);
}

PlatformConfig PlatformUtils::GetDefaultConfig() {
    return {"\\\\.\\pipe\\OptiFiCommandPipe", "OptiFi", "10.137.137.1", "255.255.255.0"};
}

// --- WINDOWS ADAPTER (REAL WINTUN) ---
class WindowsAdapter : public IAdapter {
public:
    WindowsAdapter(const std::string& name) 
        : m_name(std::wstring(name.begin(), name.end())), m_lib(nullptr), m_adapter(nullptr), m_session(nullptr), m_waitEvent(nullptr) {}
    
    ~WindowsAdapter() { Shutdown(); }

    bool Initialize() override {
        m_lib = LoadLibraryA("wintun.dll");
        if (!m_lib) {
            std::cerr << "CRITICAL: Could not find or load wintun.dll! Error: " << GetLastError() << std::endl;
            std::cerr << "Ensure wintun.dll is in the same folder as the executable." << std::endl;
            return false;
        }

        auto create = (WintunCreateAdapter_t)GetProcAddress(m_lib, "WintunCreateAdapter");
        if (!create) {
             std::cerr << "CRITICAL: Wintun DLL is corrupt or incompatible." << std::endl;
             return false;
        }
        
        m_adapter = create(m_name.c_str(), L"OptiFi", NULL);
        if (!m_adapter) {
            std::cerr << "CRITICAL: Failed to create Wintun adapter. Error: " << GetLastError() << std::endl;
            std::cerr << "Are you running as Administrator?" << std::endl;
            return false;
        }

        ConfigureNetwork();
        return true;
    }

    bool StartSession(uint32_t bufferSize) override {
        auto start = (WintunStartSession_t)GetProcAddress(m_lib, "WintunStartSession");
        auto getEvent = (WintunGetReadWaitEvent_t)GetProcAddress(m_lib, "WintunGetReadWaitEvent");
        m_session = start(m_adapter, bufferSize);
        if (m_session) m_waitEvent = getEvent(m_session);
        std::cout << "[ADAPTER] Wintun session " << (m_session ? "started" : "failed")
                  << ". waitEvent=" << m_waitEvent << std::endl;
        return m_session != nullptr;
    }

    void Shutdown() override {
        if (m_session) { ((WintunEndSession_t)GetProcAddress(m_lib, "WintunEndSession"))(m_session); m_session = nullptr; }
        if (m_adapter) { ((WintunCloseAdapter_t)GetProcAddress(m_lib, "WintunCloseAdapter"))(m_adapter); m_adapter = nullptr; }
        if (m_lib) { FreeLibrary(m_lib); m_lib = nullptr; }
    }

    uint8_t* ReadPacket(uint32_t* outSize) override {
        auto recv = (WintunReceivePacket_t)GetProcAddress(m_lib, "WintunReceivePacket");
        DWORD wintunSize = 0;
        BYTE* packet = recv(m_session, &wintunSize);
        if (!packet || wintunSize == 0) {
            *outSize = 0;
            if (packet) ReleaseWintunPacket(packet);
            return nullptr;
        }

        uint8_t version = packet[0] >> 4;
        uint16_t ethType = 0;
        if (version == 4) ethType = ETH_TYPE_IPV4;
        else if (version == 6) ethType = ETH_TYPE_IPV6;
        else {
            ReleaseWintunPacket(packet);
            *outSize = 0;
            return nullptr;
        }

        if (wintunSize + 14 > m_rxFrame.size()) {
            m_rxFrame.resize(wintunSize + 14);
        }

        memcpy(m_rxFrame.data(), USB_MAC, sizeof(USB_MAC));
        memcpy(m_rxFrame.data() + 6, HOST_MAC, sizeof(HOST_MAC));
        WriteEthType(m_rxFrame.data(), ethType);
        memcpy(m_rxFrame.data() + 14, packet, wintunSize);
        *outSize = wintunSize + 14;
        ReleaseWintunPacket(packet);
        return m_rxFrame.data();
    }

    void ReleasePacket(const uint8_t* packet) override {
        (void)packet;
    }

    bool SendPacket(const uint8_t* data, uint32_t size) override {
        auto alloc = (WintunAllocateSendPacket_t)GetProcAddress(m_lib, "WintunAllocateSendPacket");
        auto send = (WintunSendPacket_t)GetProcAddress(m_lib, "WintunSendPacket");
        const uint8_t* payload = data;
        uint32_t payloadSize = size;

        if (size >= 14) {
            uint16_t ethType = ReadEthType(data);
            if (ethType == ETH_TYPE_IPV4 || ethType == ETH_TYPE_IPV6) {
                payload = data + 14;
                payloadSize = size - 14;
            }
        }

        if (payloadSize == 0) return false;

        BYTE* buf = alloc(m_session, payloadSize);
        if (buf) { memcpy(buf, payload, payloadSize); send(m_session, buf); return true; }
        return false;
    }

private:
    void ReleaseWintunPacket(const BYTE* packet) {
        auto release = (WintunReleaseReceivePacket_t)GetProcAddress(m_lib, "WintunReleaseReceivePacket");
        if (release && packet) release(m_session, packet);
    }

    void ConfigureNetwork() {
        std::cout << "[ADAPTER] Configuring Wintun adapter (IP=10.137.137.1/24, GW=10.137.137.2)..." << std::endl;

        const std::string adapterSelector =
            "$a=Get-NetAdapter | Where-Object { $_.InterfaceDescription -like 'OptiFi Tunnel*' -or $_.Name -like 'OptiFi*' } | Sort-Object ifIndex -Descending | Select-Object -First 1; "
            "if (-not $a) { Write-Error 'OptiFi Wintun adapter not found'; exit 1 }; ";

        RunPowerShellCommand(adapterSelector +
            "Get-NetIPAddress -InterfaceIndex $a.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue | Remove-NetIPAddress -Confirm:$false -ErrorAction SilentlyContinue; "
            "New-NetIPAddress -InterfaceIndex $a.ifIndex -IPAddress 10.137.137.1 -PrefixLength 24 -DefaultGateway 10.137.137.2 -ErrorAction Stop | Out-Null; "
            "Set-NetIPInterface -InterfaceIndex $a.ifIndex -AutomaticMetric Disabled -InterfaceMetric 1 -ErrorAction Stop; "
            "Set-DnsClientServerAddress -InterfaceIndex $a.ifIndex -ServerAddresses 1.1.1.1,8.8.8.8 -ErrorAction SilentlyContinue; "
            "Get-NetRoute -InterfaceIndex $a.ifIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue | Remove-NetRoute -Confirm:$false -ErrorAction SilentlyContinue; "
            "New-NetRoute -InterfaceIndex $a.ifIndex -DestinationPrefix '0.0.0.0/0' -NextHop 10.137.137.2 -RouteMetric 1 -ErrorAction Stop | Out-Null; "
            "Write-Host ('Configured ' + $a.Name + ' / ifIndex=' + $a.ifIndex)");

        // Keep a known diagnostic route available even if Windows deprioritizes the default route.
        RunPowerShellCommand(
            "Get-NetRoute -DestinationPrefix '8.8.8.8/32' -ErrorAction SilentlyContinue | Remove-NetRoute -Confirm:$false -ErrorAction SilentlyContinue; "
            "New-NetRoute -DestinationPrefix '8.8.8.8/32' -NextHop 10.137.137.2 -InterfaceAlias (Get-NetAdapter | Where-Object { $_.InterfaceDescription -like 'OptiFi Tunnel*' -or $_.Name -like 'OptiFi*' } | Sort-Object ifIndex -Descending | Select-Object -First 1).Name -RouteMetric 1 -ErrorAction SilentlyContinue | Out-Null");
    }

    std::wstring m_name;
    HMODULE m_lib;
    WINTUN_ADAPTER_HANDLE m_adapter;
    WINTUN_SESSION_HANDLE m_session;
    HANDLE m_waitEvent;
    std::vector<uint8_t> m_rxFrame{std::vector<uint8_t>(4096)};
};

// --- WINDOWS IPC SERVER ---
class WindowsIpcServer : public IIpcServer {
public:
    WindowsIpcServer(const std::string& path) : m_path(path), m_pipe(INVALID_HANDLE_VALUE), m_running(false) {}
    ~WindowsIpcServer() { StopListening(); }

    bool StartListening() override {
        m_running = true;
        m_thread = std::thread([this]() {
            while (m_running) {
                m_pipe = CreateNamedPipeA(m_path.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 1024, 1024, 0, NULL);
                if (ConnectNamedPipe(m_pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
                    std::cout << "[IPC] Frontend CONNECTED." << std::endl;
                    char buf[128];
                    while (m_running) {
                        DWORD available = 0;
                        if (!PeekNamedPipe(m_pipe, NULL, 0, NULL, &available, NULL)) {
                            break;
                        }

                        if (available == 0) {
                            Sleep(10);
                            continue;
                        }

                        DWORD read = 0;
                        if (ReadFile(m_pipe, buf, sizeof(buf)-1, &read, NULL) && read > 0) {
                            buf[read] = 0;
                            std::string cmd(buf);
                            if (cmd.find("SET_BATTERY") != std::string::npos) m_preset = DriverPreset::BATTERY;
                            else if (cmd.find("SET_PERFORMANCE") != std::string::npos) m_preset = DriverPreset::PERFORMANCE;
                            else if (cmd.find("SET_BALANCED") != std::string::npos) m_preset = DriverPreset::BALANCED;
                            else if (cmd.find("SCAN_WIFI") != std::string::npos) m_pendingScan = true;
                        } else {
                            break;
                        }
                    }
                    std::cout << "[IPC] Frontend DISCONNECTED." << std::endl;
                }
                CloseHandle(m_pipe);
                m_pipe = INVALID_HANDLE_VALUE;
            }
        });
        return true;
    }
    void StopListening() override { m_running = false; if (m_pipe != INVALID_HANDLE_VALUE) CloseHandle(m_pipe); if (m_thread.joinable()) m_thread.join(); }
    DriverPreset GetCurrentPreset() const override { return m_preset.load(); }
    bool HasPendingScan() override { return m_pendingScan.load(); }
    void ClearPendingScan() override { m_pendingScan = false; }
    bool BroadcastTelemetry(uint64_t cpu, size_t sz) override {
        if (m_pipe == INVALID_HANDLE_VALUE) return false;
        std::string p = "TELEMETRY|" + std::to_string(sz) + "|" + std::to_string(cpu) + "\n";
        DWORD written;
        std::lock_guard<std::mutex> lock(m_writeMutex);
        return WriteFile(m_pipe, p.c_str(), (DWORD)p.length(), &written, NULL);
    }
    bool BroadcastMessage(const std::string& msg) override {
        if (m_pipe == INVALID_HANDLE_VALUE) return false;
        std::string p = "MSG|" + msg + "\n";
        DWORD written;
        std::lock_guard<std::mutex> lock(m_writeMutex);
        return WriteFile(m_pipe, p.c_str(), (DWORD)p.length(), &written, NULL);
    }
private:
    std::string m_path;
    HANDLE m_pipe;
    std::atomic<bool> m_running;
    std::thread m_thread;
    std::mutex m_writeMutex;
    std::atomic<DriverPreset> m_preset{DriverPreset::BALANCED};
    std::atomic<bool> m_pendingScan{false};
};

// --- FACTORIES ---
std::unique_ptr<IAdapter> CreateAdapter(const std::string& name) { return std::make_unique<WindowsAdapter>(name); }
std::unique_ptr<IIpcServer> CreateIpcServer(const std::string& path) { return std::make_unique<WindowsIpcServer>(path); }
std::unique_ptr<IHardware> CreateHardware() { 
    auto usb = std::make_unique<hardware::UsbHardware>(0x303A, 0x4001, 0x01, 0x81);
    if (usb->Initialize()) {
        return usb;
    }
    std::cout << "[HW] OptiFi hardware not found. Falling back to MockHardware." << std::endl;
    class MockHardware : public IHardware {
    public:
        bool Initialize() override { return true; }
        void SendPacket(const uint8_t*, uint32_t) override {}
        int ReadPacket(uint8_t*, uint32_t) override { return 0; }
        bool IsConnected() const override { return true; }
        void Disconnect() override {}
    };
    return std::make_unique<MockHardware>(); 
}

} // namespace core
} // namespace optifi
