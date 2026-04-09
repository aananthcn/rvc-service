#pragma once

#include <cstdint>

// ============================================================
//  GearConfig.h  (rvc_service module)
//  VHAL constants used by GearSelectionMonitor and the
//  vendor property written to signal rvc_app.
// ============================================================

namespace rearview {

// ── Vehicle HAL ──────────────────────────────────────────────────────────────
// VehicleProperty::GEAR_SELECTION = 0x11400400 = 289408000
static constexpr int32_t PROP_GEAR_SELECTION = 0x11400400;

// VehicleGear::GEAR_REVERSE = 8
// (hardware/interfaces/automotive/vehicle/2.0/types.hal)
static constexpr int32_t GEAR_REVERSE = 8;

// ── Control property ─────────────────────────────────────────────────────────
// Vendor system property read by rvc_app.
// "1" = reverse engaged (start streaming); "0" = not reverse (stop streaming).
static constexpr const char* RVC_PROP_CAMERA_ACTIVE = "vendor.rvc.camera.active";

} // namespace rearview
