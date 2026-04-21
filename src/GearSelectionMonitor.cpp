#include "GearSelectionMonitor.h"
#include "GearConfig.h"

#include <android/binder_manager.h>
#include <log/log.h>

#include <chrono>
#include <mutex>
#include <thread>


namespace rearview {

using aidl::android::hardware::automotive::vehicle::GetValueRequest;
using aidl::android::hardware::automotive::vehicle::GetValueRequests;
using aidl::android::hardware::automotive::vehicle::GetValueResults;
using aidl::android::hardware::automotive::vehicle::IVehicle;
using aidl::android::hardware::automotive::vehicle::PropIdAreaId;
using aidl::android::hardware::automotive::vehicle::SetValueResults;
using aidl::android::hardware::automotive::vehicle::StatusCode;
using aidl::android::hardware::automotive::vehicle::SubscribeOptions;
using aidl::android::hardware::automotive::vehicle::VehiclePropErrors;
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
    mPollThread = std::thread(&GearSelectionMonitor::pollLoop, this);
    return true;
}

void GearSelectionMonitor::stop() {
    if (!mRunning.exchange(false)) return;

    mPollCv.notify_one();
    if (mPollThread.joinable()) mPollThread.join();

    if (mVehicle && mCallback) {
        mVehicle->unsubscribe(mCallback, {PROP_GEAR_SELECTION});
    }
    mVehicle.reset();
    mCallback.reset();
    ALOGI("GearMonitor: stopped");
}

// ── Internal ──────────────────────────────────────────────────────────────────

void GearSelectionMonitor::pollLoop() {
    using namespace std::chrono_literals;
    while (mRunning.load()) {
        std::unique_lock<std::mutex> lk(mPollMutex);
        mPollCv.wait_for(lk, 500ms, [this] { return !mRunning.load(); });
        if (!mRunning.load()) break;

        GetValueRequest req;
        req.requestId   = 1;
        req.prop.prop   = PROP_GEAR_SELECTION;
        req.prop.areaId = 0;
        GetValueRequests requests;
        requests.payloads = {req};
        if (mVehicle) mVehicle->getValues(mCallback, requests);
    }
}

bool GearSelectionMonitor::connectToVhal() {
    static const char* kServiceName =
        "android.hardware.automotive.vehicle.IVehicle/default";

    ALOGI("GearMonitor: waiting for Vehicle HAL…");
    auto binder = AServiceManager_waitForService(kServiceName);
    if (binder != nullptr) {
        mVehicle = IVehicle::fromBinder(::ndk::SpAIBinder(binder));
        if (mVehicle != nullptr) {
            ALOGI("GearMonitor: connected to Vehicle HAL");
            return true;
        }
    }
    ALOGE("GearMonitor: Vehicle HAL unavailable");
    return false;
}


void GearSelectionMonitor::evaluateGear(int32_t gear) {
    const bool inReverse  = (gear == GEAR_REVERSE);
    const bool wasReverse = mCurrentlyInReverse.exchange(inReverse);
    if (inReverse == wasReverse) {
        ALOGD("GearMonitor: gear=%d no-op (already %s)",
              gear, inReverse ? "REVERSE" : "NOT-REVERSE");
        return;
    }

    if (inReverse) {
        ALOGI("GearMonitor: >>> REVERSE engaged — starting camera stream");
        bool ok = mOnReverse ? mOnReverse() : true;
        if (!ok) {
            // SetProperty failed — roll back so the next REVERSE event retries
            // instead of being silently dropped as a no-op.
            mCurrentlyInReverse.store(false);
            ALOGE("GearMonitor: REVERSE callback failed — state rolled back for retry");
        }
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
            mParent.evaluateGear(result.prop->value.int32Values[0]);
        }
    }
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
