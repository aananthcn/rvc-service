#pragma once

// ============================================================
//  CameraConfig.h
//  All tuneable settings for the rear-view camera service.
//  Edit this file and rebuild — nothing else needs changing.
// ============================================================

namespace rearview {

// ── Camera ───────────────────────────────────────────────────────────────────
// Camera ID as seen by the Android camera HAL (usually "0" for the first
// physical camera, but check your device's camera HAL configuration).
static constexpr const char* CAMERA_ID       = "0";
static constexpr int         VIDEO_WIDTH     = 1280;
static constexpr int         VIDEO_HEIGHT    = 720;
static constexpr int         CAPTURE_FPS     = 30;

// ── H.264 Encoding ───────────────────────────────────────────────────────────
static constexpr const char* MIME_TYPE       = "video/avc";
static constexpr int         VIDEO_BITRATE   = 2'000'000;   // 2 Mbps
static constexpr int         I_FRAME_INTERVAL = 1;          // IDR every 1 s

// ── RTP / Network ────────────────────────────────────────────────────────────
static constexpr const char* RTP_DEST_IP     = "192.168.10.10";
static constexpr int         RTP_DEST_PORT   = 5004;
static constexpr int         RTP_LOCAL_PORT  = 0;           // OS-assigned

// RTP payload type for H.264 (RFC 6184)
static constexpr uint8_t     RTP_PAYLOAD_TYPE = 96;

// Maximum RTP packet payload — keep below network MTU (1500) minus IP+UDP headers
static constexpr int         RTP_MTU          = 1400;

// ── Vehicle HAL ──────────────────────────────────────────────────────────────
// VehicleProperty::GEAR_SELECTION = 0x11400400 = 289408000
static constexpr int32_t PROP_GEAR_SELECTION  = 0x11400400;

// VehicleGear::GEAR_REVERSE = 8  (from
//   hardware/interfaces/automotive/vehicle/2.0/types.hal)
static constexpr int32_t GEAR_REVERSE         = 8;

} // namespace rearview
