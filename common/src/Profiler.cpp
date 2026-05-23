#include "Profiler.h"

namespace optifi {
namespace telemetry {

void Profiler::StartPacketTrace() {
    m_startTime = std::chrono::high_resolution_clock::now();
}

void Profiler::EndPacketTrace(size_t packetSizeBytes) {
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_startTime);
    m_lastMetrics.processingTimeMicroseconds = duration.count();
    m_lastMetrics.bytesTransferred = packetSizeBytes;
}

PacketMetrics Profiler::GetLastPacketMetrics() const {
    return m_lastMetrics;
}

} // namespace telemetry
} // namespace optifi
