#include "PacketProcessor.h"
#include <algorithm>

namespace optifi {
namespace core {

bool PacketProcessor::IsIPv4(const uint8_t* packet, uint32_t size) const {
    if (size < 20) return false;
    return (packet[0] >> 4) == 4;
}

bool PacketProcessor::IsIcmpEchoRequest(const uint8_t* packet, uint32_t size) const {
    if (size < 34) return false;
    if (!IsIPv4(packet, size)) return false;
    if (packet[9] != 0x01) return false;
    int ipHeaderLen = (packet[0] & 0x0F) * 4;
    if (size < static_cast<uint32_t>(ipHeaderLen + 8)) return false;
    return (packet[ipHeaderLen] == 0x08);
}

std::vector<uint8_t> PacketProcessor::CreateIcmpReply(const uint8_t* request, uint32_t size) const {
    std::vector<uint8_t> reply(request, request + size);
    for (int i = 0; i < 4; i++) {
        std::swap(reply[12 + i], reply[16 + i]);
    }
    int ipHeaderLen = (request[0] & 0x0F) * 4;
    int icmpOffset = ipHeaderLen;
    reply[icmpOffset] = 0x00;
    reply[icmpOffset + 2] = 0x00;
    reply[icmpOffset + 3] = 0x00;
    uint16_t checksum = CalculateChecksum(&reply[icmpOffset], size - icmpOffset);
    reply[icmpOffset + 2] = (checksum >> 8) & 0xFF;
    reply[icmpOffset + 3] = checksum & 0xFF;
    return reply;
}

uint16_t PacketProcessor::CalculateChecksum(const uint8_t* data, size_t length) const {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; i += 2) {
        uint16_t word = (data[i] << 8) + (i + 1 < length ? data[i + 1] : 0);
        sum += word;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

} // namespace core
} // namespace optifi
