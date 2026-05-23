#pragma once
#include <functional>
#include <string>

namespace optifi {
namespace core {

struct PlatformConfig {
    std::string ipcPath;
    std::string adapterName;
    std::string staticIp;
    std::string subnetMask;
};

class PlatformUtils {
public:
    // Sets up Ctrl+C / SIGINT handlers in a way the OS understands
    static void SetupSignalHandler(std::function<void()> onShutdown);

    // Returns configuration specific to the running OS
    static PlatformConfig GetDefaultConfig();
};

} // namespace core
} // namespace optifi
