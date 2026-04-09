# CLAUDE.md — rvc_service

This file is read automatically by the `claude` CLI (Claude Code) whenever you
run it inside this directory. It tells Claude everything it needs to know about
the project so you can make changes with short, natural instructions.

---

## Project overview

`rvc_service` is a **native C++ vendor service** for Android Automotive OS
(AAOS). It runs as a persistent background process that:

1. Subscribes to the Vehicle HAL `GEAR_SELECTION` property via HIDL.
2. When the gear changes to **REVERSE**, opens the configured rear camera.
3. Encodes the camera feed as **H.264** using `AMediaCodec` (NDK).
4. Streams the encoded video as **RTP/UDP** (RFC 3984) to the Instrument
   Cluster at `192.168.10.10:5004`.
5. When the gear leaves REVERSE, stops the stream and releases the camera.

It is **not** an APK, not a Java/Kotlin app, and does not use Android Studio
or Gradle. It is built entirely inside the AOSP source tree with Soong
(`Android.bp`).

---

## AOSP tree placement

```
<AOSP_ROOT>/vendor/<oem>/rvc_service/
```

The service installs to `/vendor/bin/rvc_service` and is started by `init`
via `/vendor/etc/init/rvc_service.rc`.

To register it in a device build, add to `device.mk`:
```makefile
PRODUCT_PACKAGES += rvc_service
```

---

## File map

```
rvc_service/
├── CLAUDE.md                        ← you are here
├── Android.bp                       ← Soong build rules; defines cc_binary "rvc_service"
├── rvc_service.rc                   ← Android Init Language: boot trigger + privileges
├── main.cpp                         ← entry point; wires GearSelectionMonitor → CameraStreamManager
├── GearSelectionMonitor.cpp/.h      ← HIDL IVehicle client; fires onReverse / onNotReverse
├── CameraStreamManager.cpp/.h       ← Camera NDK + AMediaCodec pipeline
├── RtpStreamer.cpp/.h               ← Annex-B → NAL → RTP/UDP packetiser (RFC 3984)
└── include/
    └── CameraConfig.h               ← ALL tuneable constants (IP, port, resolution, …)
```

### Single source of truth for configuration

**`include/CameraConfig.h`** — edit this file for any hardware or network
change. Everything else reads from it; nothing else needs touching.

Key constants:

| Constant | Default | Meaning |
|---|---|---|
| `CAMERA_ID` | `"0"` | Camera HAL device ID |
| `VIDEO_WIDTH` / `VIDEO_HEIGHT` | `1280` / `720` | Capture resolution |
| `CAPTURE_FPS` | `30` | Target frame rate |
| `MIME_TYPE` | `"video/avc"` | H.264 codec MIME |
| `VIDEO_BITRATE` | `2'000'000` | Encoder bitrate (bps) |
| `I_FRAME_INTERVAL` | `1` | IDR keyframe interval (seconds) |
| `RTP_DEST_IP` | `"192.168.10.10"` | Instrument Cluster IP |
| `RTP_DEST_PORT` | `5004` | Destination UDP port |
| `RTP_MTU` | `1400` | Max RTP packet payload (bytes) |
| `RTP_PAYLOAD_TYPE` | `96` | RTP dynamic payload type for H.264 |
| `PROP_GEAR_SELECTION` | `0x11400400` | VHAL property ID — do not change |
| `GEAR_REVERSE` | `8` | VHAL gear value for reverse — do not change |

---

## C++ namespace

All classes live in the `rearview` namespace. Do not change the namespace
name — it is internal and not exposed to any other module.

---

## Architecture and data flow

```
init (rvc_service.rc)
  └── main()                              [main.cpp]
        │
        ├── GearSelectionMonitor          [GearSelectionMonitor.cpp]
        │     │  IVehicle::getService()   HIDL binder call
        │     │  subscribe(GEAR_SELECTION, ON_CHANGE)
        │     │  onPropertyEvent() ──────────────────────────┐
        │     │                                              │ gear == GEAR_REVERSE?
        │     │                                              ▼
        └── CameraStreamManager           [CameraStreamManager.cpp]
              │  open() / close()
              │
              ├── ACameraManager_openCamera()      Camera NDK
              ├── ACameraDevice_createCaptureSession()
              ├── ANativeWindow  ◄──── encoder input surface
              │
              ├── AMediaCodec (H.264 encoder, Surface input mode)
              │     encoderLoop() thread
              │       AMediaCodec_dequeueOutputBuffer()
              │         │  Annex-B byte stream
              │         ▼
              └── RtpStreamer              [RtpStreamer.cpp]
                    sendAnnexB()
                      strip 0x000001 / 0x00000001 start codes
                      → individual NAL units
                      if NAL ≤ MTU  → single-NAL RTP packet
                      if NAL >  MTU → FU-A fragmentation (RFC 3984 §5.8)
                      sendto() UDP ──────────► 192.168.10.10:5004
```

### Key design decisions to preserve

- **Surface-input encoding**: the Camera NDK renders directly into the
  `ANativeWindow` that `AMediaCodec` provides. There is no intermediate
  buffer copy. Do not change this to buffer-input mode.
- **Encoder drain thread**: `encoderLoop()` runs on a dedicated `std::thread`
  so it never blocks camera callbacks. Keep it this way.
- **Edge-triggered gear logic**: `GearSelectionMonitor::evaluateGear()` only
  fires callbacks when the reverse state *changes*, never on repeated same
  values. This prevents double-open / double-close races.
- **`START_STICKY` equivalent**: the `.rc` file uses `class hal` so `init`
  will restart the service if it crashes. Do not remove `onrestart`.

---

## Build instructions

```bash
# From the AOSP root (run once per shell):
source build/envsetup.sh
lunch <your_target>

# Build only this module:
mmm vendor/<oem>/rvc_service

# Or by module name:
m rvc_service

# Output binary:
# out/target/product/<device>/vendor/bin/rvc_service
```

## Push and test without reflashing

```bash
adb root && adb remount
adb push out/target/product/<device>/vendor/bin/rvc_service /vendor/bin/rvc_service
adb shell stop  rvc_service
adb shell start rvc_service
adb logcat -s RearViewCameraSvc GearMonitor CameraStreamMgr RtpStreamer
```

## Simulate gear events

```bash
adb shell cmd car_service inject-vhal-event 0x11400400 8   # REVERSE
adb shell cmd car_service inject-vhal-event 0x11400400 4   # DRIVE
```

---

## Coding conventions

- **C++17**. Use `std::atomic`, `std::thread`, lambdas, structured bindings freely.
- **Error handling**: all AOSP/NDK API failures must be logged with `ALOGE` and
  propagate `false` / return early. Never silently ignore a failure.
- **Logging tags**: use the existing `LOG_TAG` macro set in `Android.bp`
  (`"RearViewCameraSvc"`). Add per-class prefixes in log messages, e.g.
  `ALOGI("GearMonitor: …")`, so logcat filters work.
- **Resource ownership**: every NDK object opened in `setupCameraSession()` /
  `setupEncoder()` must be released in `teardown()`. Follow the existing
  null-check-before-free pattern.
- **No exceptions**: AOSP native code does not use C++ exceptions. Use return
  codes and `bool` success flags.
- **No heap allocation in the hot path**: the encoder drain loop and
  `RtpStreamer::sendAnnexB` must not allocate per-frame. The `std::vector`
  inside `sendSingleNal` / `sendFuA` is acceptable for now but should be
  replaced with a pre-allocated ring buffer if latency becomes a concern.
- **Headers in `include/`**: all `.h` files go under `include/`. Add new
  headers there and they are automatically picked up via
  `local_include_dirs: ["include"]` in `Android.bp`.

---

## Dependencies (declared in Android.bp)

| Library | Used for |
|---|---|
| `liblog` | `ALOGI`, `ALOGE`, `ALOGW`, `ALOGD` |
| `libutils` / `libcutils` | Android utility types (`android::sp`, etc.) |
| `libbinder` | Binder IPC transport |
| `libbase` | AOSP base utilities |
| `libcamera2ndk` | `ACameraManager`, `ACameraDevice`, `ACameraCaptureSession` |
| `libmediandk` | `AMediaCodec`, `AMediaFormat` |
| `android.hardware.automotive.vehicle@2.0` | HIDL Vehicle HAL interface |
| `libhidlbase` | HIDL transport (`android::hardware::Return`, `hidl_vec`) |
| `libhardware` | Hardware abstraction layer base |

---

## What NOT to do

- Do not add Java/Kotlin files. This is a pure native service.
- Do not add a `jni/` subdirectory or JNI bindings.
- Do not introduce new external dependencies not already in AOSP.
- Do not change the VHAL property ID (`0x11400400`) or gear value (`8`) —
  these are platform-defined constants from
  `hardware/interfaces/automotive/vehicle/2.0/types.hal`.
- Do not change the `vendor: true` flag in `Android.bp` — removing it would
  move the binary to `/system/bin` and break SELinux and VINTF.
- Do not use `std::cout` or `printf` for logging — always use `ALOGI` etc.

---

## Common tasks (examples for Claude)

> "Change the destination IP to 10.0.0.5"
→ Edit `RTP_DEST_IP` in `include/CameraConfig.h`.

> "Increase bitrate to 4 Mbps"
→ Edit `VIDEO_BITRATE` in `include/CameraConfig.h`.

> "Add support for listening on a second camera when in reverse"
→ Add a second `CameraStreamManager` instance in `main.cpp` and update
  `GearSelectionMonitor` callbacks. Add a second `CAMERA_ID_2` constant to
  `CameraConfig.h`.

> "Replace HIDL with AIDL Vehicle HAL (Android 12+)"
→ Update `GearSelectionMonitor.h/.cpp` to use
  `android.hardware.automotive.vehicle-V2-ndk` and adjust `Android.bp`
  shared_libs accordingly.

> "Add a watchdog that restarts streaming if no frames arrive for 2 seconds"
→ Add a `std::chrono::steady_clock` timestamp in `encoderLoop()` and call
  `close()` + `open()` if it expires.
