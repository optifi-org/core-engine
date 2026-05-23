#pragma once
#include <cstdint>
#include <string>

namespace optifi {
namespace core {

class IAdapter {
public:
    virtual ~IAdapter() = default;
    virtual bool Initialize() = 0;
    virtual bool StartSession(uint32_t bufferSize) = 0;
    virtual void Shutdown() = 0;
    virtual uint8_t* ReadPacket(uint32_t* outPacketSize) = 0;
    virtual void ReleasePacket(const uint8_t* packet) = 0;
    virtual bool SendPacket(const uint8_t* packet, uint32_t packetSize) = 0;
};

} // namespace core
} // namespace optifi
