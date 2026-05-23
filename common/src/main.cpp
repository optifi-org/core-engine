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

    while (g_Running) {
        uint32_t packetSize = 0;
        uint8_t* rawPacket = adapter->ReadPacket(&packetSize);

        if (rawPacket) {
            profiler.StartPacketTrace();

            // Processing logic...
            hardware->SendPacket(rawPacket, packetSize);

            if (processor.IsIcmpEchoRequest(rawPacket, packetSize)) {
                auto reply = processor.CreateIcmpReply(rawPacket, packetSize);
                adapter->SendPacket(reply.data(), packetSize);
            }

            adapter->ReleasePacket(rawPacket);
            profiler.EndPacketTrace(packetSize);
            ipcServer->BroadcastTelemetry(profiler.GetLastPacketMetrics().processingTimeMicroseconds, packetSize);
        }
    }

    std::cout << "Cleaning up..." << std::endl;
    adapter->Shutdown();
    hardware->Disconnect();
    ipcServer->StopListening();
    
    return 0;
}
