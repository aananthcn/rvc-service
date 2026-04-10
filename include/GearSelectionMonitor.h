#pragma once

#include <aidl/android/hardware/automotive/vehicle/BnVehicleCallback.h>
#include <aidl/android/hardware/automotive/vehicle/IVehicle.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace rearview {

// Subscribes to the Vehicle HAL GEAR_SELECTION property and fires callbacks
// when the gear enters or leaves REVERSE. Uses edge detection to avoid
// duplicate open/close calls.
class GearSelectionMonitor {
public:
    using GearCallback = std::function<void()>;

    GearSelectionMonitor(GearCallback onReverse, GearCallback onNotReverse);
    ~GearSelectionMonitor();

    // Connect to VHAL and subscribe to GEAR_SELECTION events.
    bool start();

    // Unsubscribe and release VHAL handle.
    void stop();

private:
    bool connectToVhal();
    void readInitialGear();
    void evaluateGear(int32_t gear);

    // AIDL callback object registered with IVehicle::subscribe().
    class VhalCallback
        : public aidl::android::hardware::automotive::vehicle::BnVehicleCallback {
    public:
        explicit VhalCallback(GearSelectionMonitor& parent) : mParent(parent) {}

        ::ndk::ScopedAStatus onGetValues(
            const aidl::android::hardware::automotive::vehicle::GetValueResults& responses)
            override;

        ::ndk::ScopedAStatus onSetValues(
            const aidl::android::hardware::automotive::vehicle::SetValueResults& responses)
            override;

        ::ndk::ScopedAStatus onPropertyEvent(
            const aidl::android::hardware::automotive::vehicle::VehiclePropValues& propValues,
            int32_t sharedMemoryFileCount)
            override;

        ::ndk::ScopedAStatus onPropertySetError(
            const aidl::android::hardware::automotive::vehicle::VehiclePropErrors& errors)
            override;

        ::ndk::ScopedAStatus onSupportedValueChange(
            const std::vector<aidl::android::hardware::automotive::vehicle::PropIdAreaId>& propIdAreaIds)
            override;

    private:
        GearSelectionMonitor& mParent;
    };

    GearCallback mOnReverse;
    GearCallback mOnNotReverse;

    std::shared_ptr<aidl::android::hardware::automotive::vehicle::IVehicle> mVehicle;
    std::shared_ptr<VhalCallback> mCallback;

    // For synchronous initial gear read via async getValues.
    std::mutex              mInitMutex;
    std::condition_variable mInitCv;
    bool                    mInitDone{false};

    std::atomic<bool> mRunning{false};
    std::atomic<bool> mCurrentlyInReverse{false};
};

} // namespace rearview
