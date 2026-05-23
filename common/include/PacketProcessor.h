#pragma once
#include <vector>
#include <cstdint>

namespace optifi {
namespace core {

class PacketProcessor {
public:
    PacketProcessor() = default;
    bool IsIPv4(const uint8_t* packet, uint32_t size) const;
    bool IsIcmpEchoRequest(const uint8_t* packet, uint32_t size) const;
    std::vector<uint8_t> CreateIcmpReply(const uint8_t* requestPacket, uint32_t size) const;

private:
    uint16_t CalculateChecksum(const uint8_t* data, size_t length) const;
};

} // namespace core
} // namespace optifi
