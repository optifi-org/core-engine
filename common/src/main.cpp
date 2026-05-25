#include <iostream>
#include <thread>
#include <memory>
#include <vector>
#include <atomic>
#include <iomanip>
#include <cstring>
#include <ctime>

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

namespace {
constexpr uint8_t OPTIFI_PKT_DATA = 0x00;
constexpr uint8_t OPTIFI_PKT_CONTROL = 0x01;
constexpr size_t kOptifiHeaderSize = 7;
}

int main() {
    std::cout << "--- OptiFi Unified Core Engine [DEBUG MODE] ---" << std::endl;

    optifi::core::PlatformUtils::SetupSignalHandler([]() {
        std::cout << "\n[CORE] Shutdown signal received." << std::endl;
        g_Running = false;
    });

    auto config = optifi::core::PlatformUtils::GetDefaultConfig();
    std::cout << "[CORE] Config: IPC=" << config.ipcPath << " Adapter=" << config.adapterName << " IP=" << config.staticIp << std::endl;

    auto ipcServer = optifi::core::CreateIpcServer(config.ipcPath);
    if (ipcServer->StartListening()) {
        std::cout << "[IPC] Server listening on " << config.ipcPath << std::endl;
    }

    optifi::telemetry::Profiler profiler;
    optifi::core::PacketProcessor processor;

    auto hardware = optifi::core::CreateHardware();
    std::cout << "[CORE] Initializing hardware..." << std::endl;
    if (!hardware->Initialize()) {
        std::cerr << "[CORE] Hardware initialization failed!" << std::endl;
    }

    auto adapter = optifi::core::CreateAdapter(config.adapterName);
    std::cout << "[CORE] Initializing network adapter..." << std::endl;
    if (!adapter->Initialize() || !adapter->StartSession(0x100000)) {
        std::cerr << "CRITICAL: Failed to initialize adapter! (Try running with sudo)" << std::endl;
        return 1;
    }

    std::cout << "--- Core Engine Online and Polling ---" << std::endl;

    uint8_t hwBuffer[4096];
    uint8_t usbTransferBuf[4096];
    optifi::core::DriverPreset lastPreset = ipcServer->GetCurrentPreset();
    uint32_t lastHwRetry = 0;
    
    uint64_t packetsOut = 0;
    uint64_t packetsIn = 0;
    time_t lastLogTime = time(nullptr);
    
    // --- MAIN LOOP ---
    while (g_Running) {
        time_t now = time(nullptr);

        // Periodic logging of stats
        if (now - lastLogTime >= 1) {
            std::cout << "[CORE STATS] TX: " << packetsOut << " packets | RX: " << packetsIn << " packets" << std::endl;
            ipcServer->BroadcastMessage("BRIDGE_STATS|" + std::to_string(packetsOut) + "|" + std::to_string(packetsIn) + "|32");
            lastLogTime = now;
        }

        // --- 0. HARDWARE RECONNECTION CHECK ---
        if (!hardware->IsConnected() && (now - lastHwRetry >= 5)) {
            if (hardware->Initialize()) {
                std::cout << "[USB] Hardware successfully RECONNECTED." << std::endl;
            }
            lastHwRetry = now;
        }

        auto currentPreset = ipcServer->GetCurrentPreset();
        if (currentPreset != lastPreset) {
            uint8_t ctrlBuf[7] = { 'O', 'P', 'T', 'I', OPTIFI_PKT_CONTROL, 0x12 /* OPTIFI_CTRL_SET_MODE */, static_cast<uint8_t>(currentPreset) };
            hardware->SendPacket(ctrlBuf, sizeof(ctrlBuf));
            std::cout << "[CORE] Sent Power Mode Control Packet: " << static_cast<int>(currentPreset) << std::endl;
            lastPreset = currentPreset;
        }

        int sleepUs = (currentPreset == optifi::core::DriverPreset::PERFORMANCE) ? 0 : 
                      (currentPreset == optifi::core::DriverPreset::BATTERY) ? 1000 : 100;

        // --- 1. HANDLE PACKETS FROM OS (TUN -> USB) ---
        uint32_t packetSize = 0;
        uint8_t* rawPacket = nullptr;
        bool hasOsPacket = false;
        
        for (int osReadCount = 0; osReadCount < 20; osReadCount++) {
            rawPacket = adapter->ReadPacket(&packetSize);
            if (rawPacket && packetSize >= 14) {
                hasOsPacket = true;
                // Sniff EVERYTHING for deep diagnostics
                // std::cout << "[TAP >> USB] " << packetSize << " bytes" << std::endl;

                if (packetSize + kOptifiHeaderSize > sizeof(usbTransferBuf)) {
                    std::cerr << "[TAP >> USB] Dropping oversize packet: " << packetSize << " bytes" << std::endl;
                    adapter->ReleasePacket(rawPacket);
                    continue;
                }

                usbTransferBuf[0] = 'O';
                usbTransferBuf[1] = 'P';
                usbTransferBuf[2] = 'T';
                usbTransferBuf[3] = 'I';
                usbTransferBuf[4] = OPTIFI_PKT_DATA;
                usbTransferBuf[5] = (uint8_t)(packetSize & 0xFF);
                usbTransferBuf[6] = (uint8_t)((packetSize >> 8) & 0xFF);
                memcpy(&usbTransferBuf[7], rawPacket, packetSize);
                
                hardware->SendPacket(usbTransferBuf, packetSize + 7);
                packetsOut++;
                ipcServer->BroadcastTelemetry(50, packetSize); // dummy processing time
                adapter->ReleasePacket(rawPacket);
            } else {
                break;
            }
        }

        // --- 2. HANDLE PACKETS FROM HARDWARE (USB -> TAP) ---
        enum class RxState { M0, M1, M2, M3, TYPE, LEN_LO, LEN_HI, PAYLOAD };
        static RxState rxState = RxState::M0;
        static uint8_t reassemblyBuffer[2048];
        static uint16_t expectedRxLen = 0;
        static uint16_t currentRxLen = 0;
        static uint8_t pendingType = 0;
        bool hasUsbPacket = false;

        int readLimit = 20;
        for (int i = 0; i < readLimit; i++) {
            int hwRead = hardware->ReadPacket(hwBuffer, sizeof(hwBuffer));
            if (hwRead > 0) {
                hasUsbPacket = true;
                uint8_t *ptr = hwBuffer;
                int remaining = hwRead;

                while (remaining > 0) {
                    switch (rxState) {
                    case RxState::M0: if (*ptr == 'O') rxState = RxState::M1; ptr++; remaining--; break;
                    case RxState::M1: rxState = (*ptr == 'P') ? RxState::M2 : RxState::M0; ptr++; remaining--; break;
                    case RxState::M2: rxState = (*ptr == 'T') ? RxState::M3 : RxState::M0; ptr++; remaining--; break;
                    case RxState::M3: rxState = (*ptr == 'I') ? RxState::TYPE : RxState::M0; ptr++; remaining--; break;
                    case RxState::TYPE:
                        pendingType = *ptr;
                        rxState = RxState::LEN_LO;
                        ptr++; remaining--;
                        break;
                    case RxState::LEN_LO:
                        expectedRxLen = *ptr;
                        ptr++; remaining--;
                        rxState = RxState::LEN_HI;
                        break;
                    case RxState::LEN_HI:
                        expectedRxLen |= static_cast<uint16_t>(*ptr) << 8;
                        ptr++; remaining--;
                        if (expectedRxLen == 0 || expectedRxLen > sizeof(reassemblyBuffer)) {
                            std::cerr << "[USB >> TAP] Dropping invalid frame length: " << expectedRxLen << std::endl;
                            expectedRxLen = 0; currentRxLen = 0; rxState = RxState::M0;
                        } else {
                            rxState = RxState::PAYLOAD;
                        }
                        break;
                    case RxState::PAYLOAD: {
                        int needed = expectedRxLen - currentRxLen;
                        int toCopy = (remaining > needed) ? needed : remaining;
                        memcpy(&reassemblyBuffer[currentRxLen], ptr, toCopy);
                        currentRxLen += toCopy;
                        ptr += toCopy;
                        remaining -= toCopy;

                        if (currentRxLen >= expectedRxLen) {
                            if (pendingType == OPTIFI_PKT_DATA) {
                                adapter->SendPacket(reassemblyBuffer, expectedRxLen);
                                packetsIn++;
                                ipcServer->BroadcastTelemetry(50, expectedRxLen);
                            }
                            expectedRxLen = 0; currentRxLen = 0; rxState = RxState::M0;
                        }
                        break;
                    }
                    }
                }
            } else break;
        }

        if (!hasOsPacket && !hasUsbPacket && sleepUs > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
        }
    }

    adapter->Shutdown();
    hardware->Disconnect();
    ipcServer->StopListening();
    return 0;
}
