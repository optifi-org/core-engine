#include <iostream>
#include <thread>
#include <memory>
#include <vector>
#include <atomic>

#include "PlatformUtils.h"
#include "IIpcServer.h"
#include "IAdapter.h"
#include "IHardware.h"
#include "PacketProcessor.h"
#include "Profiler.h"

namespace optifi {
namespace core {
    std::unique_ptr<IAdapter> CreateAdapter(const std::string& name);
    std::unique_ptr<IIpcServer> CreateIpcServer(const std::string& path);
    std::unique_ptr<IHardware> CreateHardware();
}
}

std::atomic<bool> g_Running(true);

int main() {
    std::cout << "--- OptiFi Unified Core Engine ---" << std::endl;

    // Zero #ifdefs: Platform logic handled by PAL
    optifi::core::PlatformUtils::SetupSignalHandler([]() {
        g_Running = false;
    });

    auto config = optifi::core::PlatformUtils::GetDefaultConfig();

    auto ipcServer = optifi::core::CreateIpcServer(config.ipcPath);
    ipcServer->StartListening();

    optifi::telemetry::Profiler profiler;
    optifi::core::PacketProcessor processor;

    auto hardware = optifi::core::CreateHardware();
    hardware->Initialize();

    auto adapter = optifi::core::CreateAdapter(config.adapterName);
    if (!adapter->Initialize() || !adapter->StartSession(0x100000)) {
        std::cerr << "CRITICAL: Failed to initialize adapter! (Try running with sudo)" << std::endl;
        return 1;
    }

    std::cout << "Core Engine Online." << std::endl;

    uint8_t hwBuffer[2048];

    while (g_Running) {
        // --- 1. HANDLE PACKETS FROM OS ---
        uint32_t packetSize = 0;
        uint8_t* rawPacket = adapter->ReadPacket(&packetSize);

        if (rawPacket) {
            profiler.StartPacketTrace();

            // Intercept and send to Hardware
            hardware->SendPacket(rawPacket, packetSize);

            // Handle Local Ping Bounce (Internal Logic)
            if (processor.IsIcmpEchoRequest(rawPacket, packetSize)) {
                auto reply = processor.CreateIcmpReply(rawPacket, packetSize);
                adapter->SendPacket(reply.data(), packetSize);
            }

            adapter->ReleasePacket(rawPacket);
            profiler.EndPacketTrace(packetSize);
            ipcServer->BroadcastTelemetry(profiler.GetLastPacketMetrics().processingTimeMicroseconds, packetSize);
        }

        // --- 2. HANDLE PACKETS FROM HARDWARE (Return Path) ---
        int hwRead = hardware->ReadPacket(hwBuffer, sizeof(hwBuffer));
        if (hwRead > 0) {
            std::cout << "[HW] Received " << hwRead << " bytes from ESP32. Injecting back to OS..." << std::endl;
            adapter->SendPacket(hwBuffer, static_cast<uint32_t>(hwRead));
        }

        // Small sleep if no traffic to prevent 100% CPU usage
        if (!rawPacket && hwRead <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::cout << "Cleaning up..." << std::endl;
    adapter->Shutdown();
    hardware->Disconnect();
    ipcServer->StopListening();
    
    return 0;
}
