/* matter.h on connectedhomeip — spec §6. Endpoints come from
 * exhaust-fan.zap: EP1 Fan (Fan Control), EP2 Dimmable Light (On/Off +
 * Level Control).
 *
 * Threading: attribute-change callbacks run on the CHIP thread and only
 * queue the latest remote value; matter_poll() delivers it on the
 * controller thread. Reports from the controller take the CHIP stack lock,
 * and the change callback ignores its own writes while they run.
 */

#include "matter.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/server/Server.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/ConfigurationManager.h>
#include <platform/PlatformManager.h>

#include <atomic>
#include <mutex>

using namespace chip;
using namespace chip::app::Clusters;

namespace {

constexpr EndpointId kFanEndpoint   = 1;
constexpr EndpointId kLightEndpoint = 2;

// Ember encodes null in a nullable uint8 as 0xFF.
constexpr uint8_t kNullU8 = 0xFF;

matter_callbacks_t sCallbacks;

// Latest remote write per channel, coalesced: the controller only needs
// the newest target.
std::mutex sPendingMutex;
struct
{
    bool fan;
    uint8_t fanPercent;
    bool light;
    bool lightOn;
    uint8_t lightPercent;
} sPending;

// Set while the controller thread writes attributes (under the stack lock),
// so the change callback doesn't echo local changes back as remote writes.
bool sReporting = false;

std::atomic<bool> sFactoryResetPending{ false };

uint8_t LevelToPercent(uint8_t level)
{
    return static_cast<uint8_t>((level * 100 + 127) / 254);
}

uint8_t PercentToLevel(uint8_t percent)
{
    uint8_t level = static_cast<uint8_t>((percent * 254 + 50) / 100);
    return level ? level : 1;
}

} // namespace

void MatterPostAttributeChangeCallback(const app::ConcreteAttributePath & path, uint8_t type, uint16_t size, uint8_t * value)
{
    if (sReporting || size < 1)
    {
        return;
    }

    if (path.mEndpointId == kFanEndpoint && path.mClusterId == FanControl::Id &&
        path.mAttributeId == FanControl::Attributes::PercentSetting::Id)
    {
        if (*value == kNullU8)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(sPendingMutex);
        sPending.fan        = true;
        sPending.fanPercent = *value;
        return;
    }

    if (path.mEndpointId != kLightEndpoint)
    {
        return;
    }

    bool on       = false;
    uint8_t level = kNullU8;
    if (path.mClusterId == OnOff::Id && path.mAttributeId == OnOff::Attributes::OnOff::Id)
    {
        on = *value != 0;
        app::DataModel::Nullable<uint8_t> current;
        if (LevelControl::Attributes::CurrentLevel::Get(kLightEndpoint, current) == Protocols::InteractionModel::Status::Success &&
            !current.IsNull())
        {
            level = current.Value();
        }
    }
    else if (path.mClusterId == LevelControl::Id && path.mAttributeId == LevelControl::Attributes::CurrentLevel::Id)
    {
        level = *value;
        // A level change while off (e.g. Off-with-transition moving to the
        // minimum) must not turn the light on.
        if (OnOff::Attributes::OnOff::Get(kLightEndpoint, &on) != Protocols::InteractionModel::Status::Success)
        {
            return;
        }
    }
    else
    {
        return;
    }

    std::lock_guard<std::mutex> lock(sPendingMutex);
    sPending.light        = true;
    sPending.lightOn      = on;
    sPending.lightPercent = level == kNullU8 ? 0 : LevelToPercent(level); // 0 = keep current
}

extern "C" int matter_start(const matter_callbacks_t * cbs)
{
    sCallbacks = *cbs;
    ChipLogProgress(AppServer, "exhaustd: fan on EP%u, light on EP%u", kFanEndpoint, kLightEndpoint);
    return 0;
}

extern "C" void matter_poll(void)
{
    decltype(sPending) pending;
    {
        std::lock_guard<std::mutex> lock(sPendingMutex);
        pending  = sPending;
        sPending = {};
    }
    if (pending.fan && sCallbacks.on_fan_percent)
    {
        sCallbacks.on_fan_percent(pending.fanPercent);
    }
    if (pending.light && sCallbacks.on_light_state)
    {
        sCallbacks.on_light_state(pending.lightOn, pending.lightPercent);
    }
}

extern "C" void matter_report_fan_percent(uint8_t percent)
{
    DeviceLayer::StackLock lock;
    sReporting = true;
    // PercentSetting too, so a wall-switch change is visible as the new
    // setting, not just the current speed.
    FanControl::Attributes::PercentSetting::Set(kFanEndpoint, percent);
    FanControl::Attributes::PercentCurrent::Set(kFanEndpoint, percent);
    sReporting = false;
}

extern "C" void matter_report_light(bool on, uint8_t percent)
{
    DeviceLayer::StackLock lock;
    sReporting = true;
    OnOff::Attributes::OnOff::Set(kLightEndpoint, on);
    if (on)
    {
        LevelControl::Attributes::CurrentLevel::Set(kLightEndpoint, PercentToLevel(percent));
    }
    sReporting = false;
}

extern "C" bool matter_is_commissioned(void)
{
    DeviceLayer::StackLock lock;
    return Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

extern "C" void matter_factory_reset(void)
{
    sFactoryResetPending = true;
    // The same steps as Server::ScheduleFactoryReset(), then stop the event
    // loop once they have run (work items run in order), so the process
    // exits and systemd restarts it factory-fresh.
    DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
        Server::GetInstance().GetFabricTable().DeleteAllFabrics();
        DeviceLayer::PlatformMgr().HandleServerShuttingDown();
        DeviceLayer::ConfigurationMgr().InitiateFactoryReset(); // clears config + KVS, Thread dataset
        DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) { DeviceLayer::PlatformMgr().StopEventLoopTask(); });
    });
}

bool MatterFactoryResetPending()
{
    return sFactoryResetPending;
}
