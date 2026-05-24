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
constexpr uint8_t OPTIFI_PKT_CREDIT = 0x03;
constexpr int32_t kMaxCredits = 32;
constexpr size_t kOptifiHeaderSize = 3;
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
    
    // --- STATIC WINDOW (CREDIT-BASED FLOW CONTROL) ---
    // Start with 32 credits (matching ESP32's usb_rx_queue size)
    std::atomic<int32_t> credits(kMaxCredits);

    while (g_Running) {
        time_t now = time(nullptr);

        // Periodic logging of stats
        if (now - lastLogTime >= 1) {
            std::cout << "[CORE STATS] TX: " << packetsOut << " packets | RX: " << packetsIn << " packets | CREDITS: " << credits.load() << std::endl;
            ipcServer->BroadcastMessage("BRIDGE_STATS|" + std::to_string(packetsOut) + "|" + std::to_string(packetsIn) + "|" + std::to_string(credits.load()));
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
        int sleepUs = (currentPreset == optifi::core::DriverPreset::PERFORMANCE) ? 0 : 100;

        // --- 1. HANDLE PACKETS FROM OS (TUN -> USB) ---
        uint32_t packetSize = 0;
        uint8_t* rawPacket = nullptr;
        
        if (credits.load() > 0) {
            rawPacket = adapter->ReadPacket(&packetSize);
            if (rawPacket && packetSize >= 14) {
                // Sniff EVERYTHING for deep diagnostics
                std::cout << "[TAP >> USB] ";
                for(int i=0; i<14; i++) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)rawPacket[i] << " ";
                std::cout << std::dec << " (" << packetSize << " bytes)" << std::endl;

                if (packetSize + kOptifiHeaderSize > sizeof(usbTransferBuf)) {
                    std::cerr << "[TAP >> USB] Dropping oversize packet: " << packetSize << " bytes" << std::endl;
                    adapter->ReleasePacket(rawPacket);
                    continue;
                }

                usbTransferBuf[0] = OPTIFI_PKT_DATA;
                usbTransferBuf[1] = (uint8_t)(packetSize & 0xFF);
                usbTransferBuf[2] = (uint8_t)((packetSize >> 8) & 0xFF);
                memcpy(&usbTransferBuf[3], rawPacket, packetSize);
                
                hardware->SendPacket(usbTransferBuf, packetSize + 3);
                packetsOut++;
                credits--; 
                ipcServer->BroadcastTelemetry(50, packetSize); // 50us dummy processing time for telemetry graph
                adapter->ReleasePacket(rawPacket);
            }
        }

        // --- 2. HANDLE PACKETS FROM HARDWARE (USB -> TAP) ---
        enum class RxState { TYPE, LEN_LO, LEN_HI, PAYLOAD };
        static RxState rxState = RxState::TYPE;
        static uint8_t reassemblyBuffer[2048];
        static uint16_t expectedRxLen = 0;
        static uint16_t currentRxLen = 0;

        int readLimit = 20;
        for (int i = 0; i < readLimit; i++) {
            int hwRead = hardware->ReadPacket(hwBuffer, sizeof(hwBuffer));
            if (hwRead > 0) {
                uint8_t *ptr = hwBuffer;
                int remaining = hwRead;

                while (remaining > 0) {
                    switch (rxState) {
                    case RxState::TYPE:
                        if (*ptr == OPTIFI_PKT_DATA) {
                            expectedRxLen = 0;
                            currentRxLen = 0;
                            rxState = RxState::LEN_LO;
                        } else if (*ptr == OPTIFI_PKT_CREDIT) {
                            int32_t current = credits.load();
                            while (current < kMaxCredits && !credits.compare_exchange_weak(current, current + 1)) {}
                        }
                        ptr++;
                        remaining--;
                        break;
                    case RxState::LEN_LO:
                        expectedRxLen = *ptr;
                        ptr++;
                        remaining--;
                        rxState = RxState::LEN_HI;
                        break;
                    case RxState::LEN_HI:
                        expectedRxLen |= static_cast<uint16_t>(*ptr) << 8;
                        ptr++;
                        remaining--;
                        if (expectedRxLen == 0 || expectedRxLen > sizeof(reassemblyBuffer)) {
                            std::cerr << "[USB >> TAP] Dropping invalid frame length: " << expectedRxLen << std::endl;
                            expectedRxLen = 0;
                            currentRxLen = 0;
                            rxState = RxState::TYPE;
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
                            adapter->SendPacket(reassemblyBuffer, expectedRxLen);
                            packetsIn++;
                            ipcServer->BroadcastTelemetry(50, expectedRxLen);
                            
                            std::cout << "[USB >> TAP] Packet received (" << expectedRxLen << " bytes): ";
                            for(int i=0; i<14 && i<expectedRxLen; i++) 
                                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)reassemblyBuffer[i] << " ";
                            std::cout << std::dec << std::endl;

                            expectedRxLen = 0;
                            currentRxLen = 0;
                            rxState = RxState::TYPE;
                        }
                        break;
                    }
                    }
                }
            } else break;
        }

        if (!rawPacket && sleepUs > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
        }
    }

    adapter->Shutdown();
    hardware->Disconnect();
    ipcServer->StopListening();
    return 0;
}
