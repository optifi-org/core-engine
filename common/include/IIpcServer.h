#pragma once
#include <string>
#include <cstdint>

namespace optifi {
namespace core {

enum class DriverPreset {
    PERFORMANCE = 0,
    BATTERY     = 1,
    BALANCED    = 2
};

class IIpcServer {
public:
    virtual ~IIpcServer() = default;
    virtual bool StartListening() = 0;
    virtual void StopListening() = 0;
    virtual DriverPreset GetCurrentPreset() const = 0;
    virtual bool BroadcastTelemetry(uint64_t cpuCostUs, size_t packetSizeBytes) = 0;
};

} // namespace core
} // namespace optifi
