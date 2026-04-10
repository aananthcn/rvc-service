#include "GearSelectionMonitor.h"
#include "GearConfig.h"

#include <android/binder_manager.h>
#include <log/log.h>

#include <chrono>
#include <thread>

namespace rearview {

using aidl::android::hardware::automotive::vehicle::GetValueRequests;
using aidl::android::hardware::automotive::vehicle::GetValueRequest;
using aidl::android::hardware::automotive::vehicle::GetValueResults;
using aidl::android::hardware::automotive::vehicle::IVehicle;
using aidl::android::hardware::automotive::vehicle::PropIdAreaId;
using aidl::android::hardware::automotive::vehicle::SetValueResults;
using aidl::android::hardware::automotive::vehicle::StatusCode;
using aidl::android::hardware::automotive::vehicle::SubscribeOptions;
using aidl::android::hardware::automotive::vehicle::VehiclePropErrors;
using aidl::android::hardware::automotive::vehicle::VehiclePropValue;
using aidl::android::hardware::automotive::vehicle::VehiclePropValues;

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
        ALOGE("GearMonitor: Cannot connect to Vehicle HAL");
        return false;
    }

    mCallback = ::ndk::SharedRefBase::make<VhalCallback>(*this);

    SubscribeOptions opt;
    opt.propId     = PROP_GEAR_SELECTION;
    opt.sampleRate = 0.0f; // ON_CHANGE property

    auto status = mVehicle->subscribe(mCallback, {opt}, /*maxSharedMemoryFileCount=*/2);
    if (!status.isOk()) {
        ALOGE("GearMonitor: Failed to subscribe to GEAR_SELECTION: %s",
              status.getDescription().c_str());
        return false;
    }

    mRunning.store(true);
    ALOGI("GearMonitor: Subscribed to GEAR_SELECTION (propId=0x%08X)", PROP_GEAR_SELECTION);

    // Read initial gear so we handle boot-with-reverse-engaged correctly.
    readInitialGear();
    return true;
}

void GearSelectionMonitor::stop() {
    if (!mRunning.exchange(false)) return;

    if (mVehicle && mCallback) {
        mVehicle->unsubscribe(mCallback, {PROP_GEAR_SELECTION});
    }
    mVehicle.reset();
    mCallback.reset();
    ALOGI("GearMonitor: stopped");
}

// ── Internal ──────────────────────────────────────────────────────────────────

bool GearSelectionMonitor::connectToVhal() {
    constexpr int kMaxRetries      = 30;
    constexpr auto kRetryInterval  = std::chrono::seconds(1);
    static const char* kServiceName =
        "android.hardware.automotive.vehicle.IVehicle/default";

    for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
        auto binder = AServiceManager_checkService(kServiceName);
        if (binder != nullptr) {
            mVehicle = IVehicle::fromBinder(::ndk::SpAIBinder(binder));
            if (mVehicle != nullptr) {
                ALOGI("GearMonitor: connected to Vehicle HAL (attempt %d)", attempt);
                return true;
            }
        }
        ALOGW("GearMonitor: VHAL not ready, retrying (%d/%d)…", attempt, kMaxRetries);
        std::this_thread::sleep_for(kRetryInterval);
    }

    ALOGE("GearMonitor: Vehicle HAL unavailable after %d attempts", kMaxRetries);
    return false;
}

void GearSelectionMonitor::readInitialGear() {
    GetValueRequest req;
    req.requestId      = 1;
    req.prop.prop      = PROP_GEAR_SELECTION;
    req.prop.areaId    = 0;

    GetValueRequests requests;
    requests.payloads = {req};

    {
        std::lock_guard<std::mutex> lock(mInitMutex);
        mInitDone = false;
    }

    auto status = mVehicle->getValues(mCallback, requests);
    if (!status.isOk()) {
        ALOGW("GearMonitor: getValues failed for initial gear: %s",
              status.getDescription().c_str());
        return;
    }

    // Wait up to 2 seconds for the async response.
    std::unique_lock<std::mutex> lock(mInitMutex);
    mInitCv.wait_for(lock, std::chrono::seconds(2), [this] { return mInitDone; });
    if (!mInitDone) {
        ALOGW("GearMonitor: timed out waiting for initial gear value");
    }
}

void GearSelectionMonitor::evaluateGear(int32_t gear) {
    const bool inReverse  = (gear == GEAR_REVERSE);
    const bool wasReverse = mCurrentlyInReverse.exchange(inReverse);
    if (inReverse == wasReverse) return;

    if (inReverse) {
        ALOGI("GearMonitor: >>> REVERSE engaged — starting camera stream");
        if (mOnReverse) mOnReverse();
    } else {
        ALOGI("GearMonitor: >>> Out of REVERSE — stopping camera stream");
        if (mOnNotReverse) mOnNotReverse();
    }
}

// ── VhalCallback ─────────────────────────────────────────────────────────────

::ndk::ScopedAStatus GearSelectionMonitor::VhalCallback::onPropertyEvent(
        const VehiclePropValues& propValues, int32_t /*sharedMemoryFileCount*/) {
    for (const auto& val : propValues.payloads) {
        if (val.prop == PROP_GEAR_SELECTION && !val.value.int32Values.empty()) {
            ALOGD("GearMonitor: GEAR_SELECTION event: gear=%d", val.value.int32Values[0]);
            mParent.evaluateGear(val.value.int32Values[0]);
        }
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus GearSelectionMonitor::VhalCallback::onGetValues(
        const GetValueResults& responses) {
    for (const auto& result : responses.payloads) {
        if (result.status == StatusCode::OK && result.prop.has_value() &&
            result.prop->prop == PROP_GEAR_SELECTION &&
            !result.prop->value.int32Values.empty()) {
            ALOGI("GearMonitor: Initial gear value: %d", result.prop->value.int32Values[0]);
            mParent.evaluateGear(result.prop->value.int32Values[0]);
        }
    }
    {
        std::lock_guard<std::mutex> lock(mParent.mInitMutex);
        mParent.mInitDone = true;
    }
    mParent.mInitCv.notify_one();
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus GearSelectionMonitor::VhalCallback::onSetValues(
        const SetValueResults& /*responses*/) {
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus GearSelectionMonitor::VhalCallback::onPropertySetError(
        const VehiclePropErrors& errors) {
    for (const auto& err : errors.payloads) {
        ALOGE("GearMonitor: VHAL property set error: propId=0x%08X areaId=%d status=%d",
              err.propId, err.areaId, static_cast<int>(err.errorCode));
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus GearSelectionMonitor::VhalCallback::onSupportedValueChange(
        const std::vector<PropIdAreaId>& /*propIdAreaIds*/) {
    return ::ndk::ScopedAStatus::ok();
}

} // namespace rearview
