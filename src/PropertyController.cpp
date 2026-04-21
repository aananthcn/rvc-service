#include "PropertyController.h"
#include <android-base/properties.h>
#include <log/log.h>

namespace rearview {

PropertyController::PropertyController(const std::string& propName)
    : mPropName(propName) {}

bool PropertyController::notifyReverse() {
    if (android::base::SetProperty(mPropName, "1")) {
        ALOGI("GearMonitor: set %s=1 (REVERSE)", mPropName.c_str());
        return true;
    }
    ALOGE("GearMonitor: failed to set %s=1 — state rolled back", mPropName.c_str());
    return false;
}

bool PropertyController::notifyNotReverse() {
    if (android::base::SetProperty(mPropName, "0")) {
        ALOGI("GearMonitor: set %s=0 (NOT REVERSE)", mPropName.c_str());
        return true;
    }
    ALOGE("GearMonitor: failed to set %s=0", mPropName.c_str());
    return false;
}

} // namespace rearview
