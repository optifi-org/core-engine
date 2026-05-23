#pragma once
#include <cstdint>
#include <chrono>

namespace optifi {
namespace telemetry {

struct PacketMetrics {
    uint64_t processingTimeMicroseconds;
    size_t bytesTransferred;
};

class Profiler {
public:
    Profiler() = default;
    void StartPacketTrace();
    void EndPacketTrace(size_t packetSizeBytes);
    PacketMetrics GetLastPacketMetrics() const;

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;
    PacketMetrics m_lastMetrics = {0, 0};
};

} // namespace telemetry
} // namespace optifi
