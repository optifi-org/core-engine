#pragma once
#include <functional>
#include <string>

struct libusb_device_handle;

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

    // Configures USB device hooks for the running OS
    static void ConfigureUsbDevice(struct libusb_device_handle* handle);
};

} // namespace core
} // namespace optifi
