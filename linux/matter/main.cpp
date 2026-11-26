/* exhaustd with Matter: the connectedhomeip Linux app (commissioning, BLE,
 * Thread via ot-br-posix, persistent fabric storage) owns the main thread
 * and its event loop; the controller loop (../src/controller.c) runs on
 * its own thread, started once the Matter server is up. */

#include <AppMain.h>

#include <lib/support/CHIPArgParser.hpp>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/PlatformManager.h>

#include <atomic>
#include <thread>

#include "controller.h"

using namespace chip;
using namespace chip::ArgParser;

bool MatterFactoryResetPending(); // matter_chip.cpp

namespace {

controller_opts_t sOpts = CONTROLLER_OPTS_DEFAULT;
std::thread sControllerThread;
int sExitStatus = 0;
std::atomic<bool> sShuttingDown{ false };

enum : uint16_t
{
    kOptGpiochip = 0x1000,
    kOptUart,
    kOptStateDir,
    kOptIface,
};

bool HandleOption(const char * program, OptionSet * options, int id, const char * name, const char * value)
{
    switch (id)
    {
    case kOptGpiochip:
        sOpts.gpiochip = value;
        return true;
    case kOptUart:
        sOpts.uart = value;
        return true;
    case kOptStateDir:
        sOpts.state_dir = value;
        return true;
    case kOptIface:
        sOpts.iface = value;
        return true;
    default:
        PrintArgError("%s: unknown option: %s\n", program, name);
        return false;
    }
}

OptionDef sOptionDefs[] = {
    { "gpiochip", kArgumentRequired, kOptGpiochip },
    { "uart", kArgumentRequired, kOptUart },
    { "state-dir", kArgumentRequired, kOptStateDir },
    { "iface", kArgumentRequired, kOptIface },
    {},
};

OptionSet sOptions = {
    HandleOption,
    sOptionDefs,
    "EXHAUSTD OPTIONS",
    "  --gpiochip <path>   GPIO character device (default /dev/gpiochip0)\n"
    "  --uart <path>       dimmer bridge tty (default /dev/serial0)\n"
    "  --state-dir <dir>   calibration + user state (default /var/lib/exhaustd)\n"
    "  --iface <name>      interface for the MAC-derived hostname (default wlan0)\n",
};

void ControllerThread()
{
    sExitStatus = controller_run(&sOpts);
    // The controller stopped on its own: take the Matter stack down with it
    // so systemd restarts both. A factory reset stops the loop itself once
    // the reset has run.
    if (!sShuttingDown && !MatterFactoryResetPending())
    {
        DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) { DeviceLayer::PlatformMgr().StopEventLoopTask(); });
    }
}

} // namespace

void ApplicationInit()
{
    sControllerThread = std::thread(ControllerThread);
}

void ApplicationShutdown()
{
    // SIGTERM/SIGINT stop the event loop first; then the controller winds
    // down (fan off before the MUA relay opens).
    sShuttingDown = true;
    controller_request_stop();
    if (sControllerThread.joinable())
    {
        sControllerThread.join();
    }
}

int main(int argc, char * argv[])
{
    if (ChipLinuxAppInit(argc, argv, &sOptions) != 0)
    {
        return -1;
    }
    ChipLinuxAppMainLoop();
    return sExitStatus;
}
