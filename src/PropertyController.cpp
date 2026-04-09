#include "PropertyController.h"
#include <android-base/properties.h>
#include <log/log.h>

namespace rearview {

PropertyController::PropertyController(const std::string& propName)
    : mPropName(propName) {}

void PropertyController::notifyReverse() {
    if (android::base::SetProperty(mPropName, "1")) {
        ALOGI("GearMonitor: set %s=1 (REVERSE)", mPropName.c_str());
    } else {
        ALOGE("GearMonitor: failed to set %s=1", mPropName.c_str());
    }
}

void PropertyController::notifyNotReverse() {
    if (android::base::SetProperty(mPropName, "0")) {
        ALOGI("GearMonitor: set %s=0 (NOT REVERSE)", mPropName.c_str());
    } else {
        ALOGE("GearMonitor: failed to set %s=0", mPropName.c_str());
    }
}

} // namespace rearview
