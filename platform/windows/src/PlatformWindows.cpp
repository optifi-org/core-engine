#include "IAdapter.h"
#include "IIpcServer.h"
#include "IHardware.h"
#include "PlatformUtils.h"
#include <windows.h>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <memory>

// Wintun API Signatures
typedef void* WINTUN_ADAPTER_HANDLE;
typedef void* WINTUN_SESSION_HANDLE;
typedef WINTUN_ADAPTER_HANDLE (*WintunCreateAdapter_t)(const WCHAR*, const WCHAR*, const GUID*);
typedef void (*WintunCloseAdapter_t)(WINTUN_ADAPTER_HANDLE);
typedef WINTUN_SESSION_HANDLE (*WintunStartSession_t)(WINTUN_ADAPTER_HANDLE, DWORD);
typedef void (*WintunEndSession_t)(WINTUN_SESSION_HANDLE);
typedef HANDLE (*WintunGetReadWaitEvent_t)(WINTUN_SESSION_HANDLE);
typedef BYTE* (*WintunReceivePacket_t)(WINTUN_SESSION_HANDLE, DWORD*);
typedef void (*WintunReleaseReceivePacket_t)(WINTUN_SESSION_HANDLE, const BYTE*);
typedef BYTE* (*WintunAllocateSendPacket_t)(WINTUN_SESSION_HANDLE, DWORD);
typedef void (*WintunSendPacket_t)(WINTUN_SESSION_HANDLE, const BYTE*);

namespace optifi {
namespace core {

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
        : m_name(std::wstring(name.begin(), name.end())), m_lib(nullptr), m_adapter(nullptr), m_session(nullptr) {}
    
    ~WindowsAdapter() { Shutdown(); }

    bool Initialize() override {
        m_lib = LoadLibraryA("wintun.dll");
        if (!m_lib) return false;

        auto create = (WintunCreateAdapter_t)GetProcAddress(m_lib, "WintunCreateAdapter");
        m_adapter = create(m_name.c_str(), L"OptiFi", NULL);
        return m_adapter != nullptr;
    }

    bool StartSession(uint32_t bufferSize) override {
        auto start = (WintunStartSession_t)GetProcAddress(m_lib, "WintunStartSession");
        auto getEvent = (WintunGetReadWaitEvent_t)GetProcAddress(m_lib, "WintunGetReadWaitEvent");
        m_session = start(m_adapter, bufferSize);
        if (m_session) m_waitEvent = getEvent(m_session);
        return m_session != nullptr;
    }

    void Shutdown() override {
        if (m_session) { ((WintunEndSession_t)GetProcAddress(m_lib, "WintunEndSession"))(m_session); m_session = nullptr; }
        if (m_adapter) { ((WintunCloseAdapter_t)GetProcAddress(m_lib, "WintunCloseAdapter"))(m_adapter); m_adapter = nullptr; }
        if (m_lib) { FreeLibrary(m_lib); m_lib = nullptr; }
    }

    uint8_t* ReadPacket(uint32_t* outSize) override {
        auto recv = (WintunReceivePacket_t)GetProcAddress(m_lib, "WintunReceivePacket");
        BYTE* packet = recv(m_session, (DWORD*)outSize);
        if (!packet) WaitForSingleObject(m_waitEvent, 100);
        return packet;
    }

    void ReleasePacket(const uint8_t* packet) override {
        ((WintunReleaseReceivePacket_t)GetProcAddress(m_lib, "WintunReleaseReceivePacket"))(m_session, packet);
    }

    bool SendPacket(const uint8_t* data, uint32_t size) override {
        auto alloc = (WintunAllocateSendPacket_t)GetProcAddress(m_lib, "WintunAllocateSendPacket");
        auto send = (WintunSendPacket_t)GetProcAddress(m_lib, "WintunSendPacket");
        BYTE* buf = alloc(m_session, size);
        if (buf) { memcpy(buf, data, size); send(m_session, buf); return true; }
        return false;
    }

private:
    std::wstring m_name;
    HMODULE m_lib;
    WINTUN_ADAPTER_HANDLE m_adapter;
    WINTUN_SESSION_HANDLE m_session;
    HANDLE m_waitEvent;
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
                    char buf[128];
                    DWORD read;
                    while (m_running) {
                        if (ReadFile(m_pipe, buf, sizeof(buf)-1, &read, NULL) && read > 0) {
                            buf[read] = 0;
                            std::string cmd(buf);
                            if (cmd.find("SET_BATTERY") != std::string::npos) m_preset = DriverPreset::BATTERY;
                            else if (cmd.find("SET_PERFORMANCE") != std::string::npos) m_preset = DriverPreset::PERFORMANCE;
                        } else break;
                    }
                }
                CloseHandle(m_pipe);
                m_pipe = INVALID_HANDLE_VALUE;
            }
        });
        return true;
    }
    void StopListening() override { m_running = false; if (m_pipe != INVALID_HANDLE_VALUE) CloseHandle(m_pipe); if (m_thread.joinable()) m_thread.join(); }
    DriverPreset GetCurrentPreset() const override { return m_preset.load(); }
    bool BroadcastTelemetry(uint64_t cpu, size_t sz) override {
        if (m_pipe == INVALID_HANDLE_VALUE) return false;
        std::string p = "TELEMETRY|" + std::to_string(sz) + "|" + std::to_string(cpu) + "\n";
        DWORD written;
        return WriteFile(m_pipe, p.c_str(), (DWORD)p.length(), &written, NULL);
    }
private:
    std::string m_path;
    HANDLE m_pipe;
    std::atomic<bool> m_running;
    std::thread m_thread;
    std::atomic<DriverPreset> m_preset{DriverPreset::BALANCED};
};

// --- FACTORIES ---
std::unique_ptr<IAdapter> CreateAdapter(const std::string& name) { return std::make_unique<WindowsAdapter>(name); }
std::unique_ptr<IIpcServer> CreateIpcServer(const std::string& path) { return std::make_unique<WindowsIpcServer>(path); }
std::unique_ptr<IHardware> CreateHardware() { 
    class MockHardware : public IHardware {
    public:
        bool Initialize() override { return true; }
        void SendPacket(const uint8_t*, uint32_t) override {}
        void Disconnect() override {}
    };
    return std::make_unique<MockHardware>(); 
}

} // namespace core
} // namespace optifi
