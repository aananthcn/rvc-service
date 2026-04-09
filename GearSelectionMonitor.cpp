#include "GearSelectionMonitor.h"
#include "CameraConfig.h"

#include <log/log.h>
#include <hidl/HidlSupport.h>

namespace rearview {

using android::hardware::hidl_vec;
using android::hardware::Return;
using android::hardware::automotive::vehicle::V2_0::IVehicle;
using android::hardware::automotive::vehicle::V2_0::VehiclePropValue;
using android::hardware::automotive::vehicle::V2_0::SubscribeOptions;
using android::hardware::automotive::vehicle::V2_0::SubscribeFlags;
using android::hardware::automotive::vehicle::V2_0::StatusCode;

// ── Constructor / Destructor ──────────────────────────────────────────────────

GearSelectionMonitor::GearSelectionMonitor(GearCallback onReverse,
                                           GearCallback onNotReverse)
    : mOnReverse(std::move(onReverse))
    , mOnNotReverse(std::move(onNotReverse)) {}

GearSelectionMonitor::~GearSelectionMonitor() {
    stop();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

bool GearSelectionMonitor::start() {
    if (mRunning.load()) return true;

    if (!connectToVhal()) {
        ALOGE("Cannot connect to Vehicle HAL");
        return false;
    }

    // Subscribe to GEAR_SELECTION property change events
    SubscribeOptions opt;
    opt.propId = PROP_GEAR_SELECTION;
    opt.flags  = SubscribeFlags::EVENTS_FROM_CAR;
    opt.sampleRate = 0.0f; // ON_CHANGE

    mCallback = new VhalCallback(*this);
    auto status = mVehicle->subscribe(mCallback, {opt});
    if (!status.isOk()) {
        ALOGE("Failed to subscribe to GEAR_SELECTION: %s",
              status.description().c_str());
        return false;
    }

    mRunning.store(true);
    ALOGI("Subscribed to GEAR_SELECTION (propId=0x%08X)", PROP_GEAR_SELECTION);

    // Read initial gear so we handle boot-with-reverse-engaged correctly
    readInitialGear();
    return true;
}

void GearSelectionMonitor::stop() {
    if (!mRunning.exchange(false)) return;

    if (mVehicle && mCallback) {
        mVehicle->unsubscribe(mCallback, PROP_GEAR_SELECTION);
    }
    mVehicle.clear();
    mCallback.clear();
    ALOGI("GearSelectionMonitor stopped");
}

// ── Internal ──────────────────────────────────────────────────────────────────

bool GearSelectionMonitor::connectToVhal() {
    mVehicle = IVehicle::getService();
    if (mVehicle == nullptr) {
        ALOGE("IVehicle::getService() returned null");
        return false;
    }
    ALOGI("Connected to Vehicle HAL");
    return true;
}

void GearSelectionMonitor::readInitialGear() {
    VehiclePropValue request;
    request.prop = PROP_GEAR_SELECTION;

    StatusCode outStatus = StatusCode::OK;
    mVehicle->get(request, [&](StatusCode status, const VehiclePropValue& val) {
        outStatus = status;
        if (status == StatusCode::OK && !val.value.int32Values.empty()) {
            ALOGI("Initial gear value: %d", val.value.int32Values[0]);
            evaluateGear(val.value.int32Values[0]);
        }
    });

    if (outStatus != StatusCode::OK) {
        ALOGW("Could not read initial GEAR_SELECTION (status=%d)",
              static_cast<int>(outStatus));
    }
}

void GearSelectionMonitor::evaluateGear(int32_t gear) {
    const bool inReverse = (gear == GEAR_REVERSE);
    // Only fire if the reverse state actually changes
    const bool wasReverse = mCurrentlyInReverse.exchange(inReverse);
    if (inReverse == wasReverse) return;

    if (inReverse) {
        ALOGI(">>> REVERSE engaged — starting camera stream");
        if (mOnReverse) mOnReverse();
    } else {
        ALOGI(">>> Out of REVERSE — stopping camera stream");
        if (mOnNotReverse) mOnNotReverse();
    }
}

// ── VhalCallback ─────────────────────────────────────────────────────────────

Return<void> GearSelectionMonitor::VhalCallback::onPropertyEvent(
        const hidl_vec<VehiclePropValue>& propValues) {
    for (const auto& val : propValues) {
        if (val.prop == PROP_GEAR_SELECTION && !val.value.int32Values.empty()) {
            const int32_t gear = val.value.int32Values[0];
            ALOGD("GEAR_SELECTION event: gear=%d", gear);
            mParent.evaluateGear(gear);
        }
    }
    return android::hardware::Void();
}

Return<void> GearSelectionMonitor::VhalCallback::onPropertySet(
        const VehiclePropValue& /*propValue*/) {
    return android::hardware::Void();
}

Return<void> GearSelectionMonitor::VhalCallback::onPropertySetError(
        StatusCode errorCode, int32_t propId, int32_t areaId) {
    ALOGE("VHAL property set error: propId=0x%08X areaId=%d errorCode=%d",
          propId, areaId, static_cast<int>(errorCode));
    return android::hardware::Void();
}

} // namespace rearview
