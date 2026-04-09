#pragma once

#include <android/hardware/automotive/vehicle/2.0/IVehicle.h>
#include <android/hardware/automotive/vehicle/2.0/IVehicleCallback.h>

#include <utils/StrongPointer.h>
#include <atomic>
#include <functional>

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

    // Inner HIDL callback object registered with IVehicle::subscribe().
    struct VhalCallback
        : public android::hardware::automotive::vehicle::V2_0::IVehicleCallback {

        explicit VhalCallback(GearSelectionMonitor& parent) : mParent(parent) {}

        android::hardware::Return<void> onPropertyEvent(
            const android::hardware::hidl_vec<
                android::hardware::automotive::vehicle::V2_0::VehiclePropValue>& propValues)
            override;

        android::hardware::Return<void> onPropertySet(
            const android::hardware::automotive::vehicle::V2_0::VehiclePropValue& propValue)
            override;

        android::hardware::Return<void> onPropertySetError(
            android::hardware::automotive::vehicle::V2_0::StatusCode errorCode,
            int32_t propId,
            int32_t areaId)
            override;

    private:
        GearSelectionMonitor& mParent;
    };

    GearCallback mOnReverse;
    GearCallback mOnNotReverse;

    android::sp<android::hardware::automotive::vehicle::V2_0::IVehicle> mVehicle;
    android::sp<VhalCallback> mCallback;

    std::atomic<bool> mRunning{false};
    std::atomic<bool> mCurrentlyInReverse{false};
};

} // namespace rearview
