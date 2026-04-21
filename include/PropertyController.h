#pragma once

#include <string>

namespace rearview {

// Signals start/stop to rvc_app by writing a vendor system property.
// Uses android::base::SetProperty which is non-blocking and safe to
// call from any thread (including the VHAL callback thread).
class PropertyController {
public:
    explicit PropertyController(const std::string& propName);

    // Write "1" → rvc_app opens camera and begins streaming.
    // Returns false if SetProperty fails.
    bool notifyReverse();

    // Write "0" → rvc_app stops streaming and releases camera.
    bool notifyNotReverse();

private:
    const std::string mPropName;
};

} // namespace rearview
