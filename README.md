# RearView Camera Service — AOSP Native C++ Service

A native C++ vendor service that subscribes to the Vehicle HAL
`GEAR_SELECTION` property and streams the rear camera as **H.264 over RTP/UDP**
to the Instrument Cluster (`192.168.10.10:5004`) whenever reverse is engaged.

---

## Where to place this code in the AOSP tree

```
<AOSP_ROOT>/
└── vendor/
    └── <your_oem>/                  ← e.g. vendor/acme/
        └── rvc_service/ ← drop this entire folder here
            ├── Android.bp
            ├── rvc_service.rc
            ├── src/
            │   ├── main.cpp
            │   ├── GearSelectionMonitor.cpp
            │   ├── CameraStreamManager.cpp
            │   └── RtpStreamer.cpp
            └── include/
                ├── CameraConfig.h
                ├── GearSelectionMonitor.h
                ├── CameraStreamManager.h
                └── RtpStreamer.h
```

**Why `vendor/`?**
Vendor modules have access to HIDL interfaces (Vehicle HAL), can be granted
`cameraserver` UID, and are linked from the board-specific `PRODUCT_PACKAGES`
list — exactly where OEM-specific hardware services belong.

---

## How to build

### 1 — Set up your AOSP build environment (once per shell session)

```bash
cd <AOSP_ROOT>
source build/envsetup.sh
lunch <your_target>          # e.g. lunch aosp_car_x86_64-userdebug
```

### 2 — Build just this module (fast iteration)

```bash
mmm vendor/<your_oem>/rvc_service
```

Or using `m` with the module name:

```bash
m rvc_service
```

The compiled binary lands at:

```
out/target/product/<device>/vendor/bin/rvc_service
```

### 3 — Full system image build

```bash
make -j$(nproc)
```

---

## How to add this service to your device's build

Open your board/device `device.mk` (e.g.
`device/<your_oem>/<board>/device.mk`) and add:

```makefile
PRODUCT_PACKAGES += \
    rvc_service
```

The `init_rc` field in `Android.bp` automatically installs
`rvc_service.rc` to `/vendor/etc/init/`, so `init` picks it
up on next boot.

---

## How to push and test during development (without a full flash)

> **Requires a userdebug or eng build with `BOARD_AVB_ENABLE := false`.**
> The RPi5 `BoardConfig.mk` already sets this. If you see
> "Device must be bootloader unlocked", your image was built before that
> change — rebuild and reflash.

```bash
# Remount vendor partition as writable
adb root
adb remount

# Push the new binary
adb push out/target/product/<device>/vendor/bin/rvc_service \
         /vendor/bin/rvc_service

# Push updated RC file if changed
adb push vendor/<your_oem>/rvc_service/rvc_service.rc \
         /vendor/etc/init/rvc_service.rc

# Restart the service
adb shell stop  rvc_service
adb shell start rvc_service

# Watch logcat output
adb logcat -s RearViewCameraSvc GearMonitor CameraStreamMgr RtpStreamer
```

---

## How to simulate a reverse gear event

```bash
# Inject GEAR_REVERSE (value = 8) via the VHAL injector
adb shell cmd car_service inject-vhal-event 0x11400400 8

# Shift back out of reverse (GEAR_DRIVE = 4)
adb shell cmd car_service inject-vhal-event 0x11400400 4
```

---

## How to receive the stream on the Instrument Cluster

### GStreamer (recommended)
```bash
gst-launch-1.0 \
  udpsrc port=5004 \
  caps="application/x-rtp,media=video,encoding-name=H264,payload=96" \
  ! rtph264depay ! avdec_h264 ! videoconvert ! autovideosink
```

### FFplay (quick test)
```bash
cat > rearview.sdp << 'EOF'
v=0
o=- 0 0 IN IP4 127.0.0.1
s=RearCamera
c=IN IP4 0.0.0.0
t=0 0
m=video 5004 RTP/AVP 96
a=rtpmap:96 H264/90000
EOF
ffplay -protocol_whitelist file,udp,rtp rearview.sdp
```

---

## Configuration

All tuneable values are in `include/CameraConfig.h`:

| Constant             | Default          | Description                        |
|----------------------|------------------|------------------------------------|
| `CAMERA_ID`          | `"0"`            | Camera HAL ID                      |
| `VIDEO_WIDTH/HEIGHT` | `1280 × 720`     | Capture resolution                 |
| `CAPTURE_FPS`        | `30`             | Target frame rate                  |
| `RTP_DEST_IP`        | `192.168.10.10`  | Instrument Cluster IP              |
| `RTP_DEST_PORT`      | `5004`           | Destination UDP port               |
| `VIDEO_BITRATE`      | `2 000 000`      | H.264 bitrate (bps)                |
| `I_FRAME_INTERVAL`   | `1`              | IDR keyframe interval (seconds)    |
| `RTP_MTU`            | `1400`           | Max RTP packet payload (bytes)     |
| `PROP_GEAR_SELECTION`| `0x11400400`     | VHAL property ID (do not change)   |
| `GEAR_REVERSE`       | `8`              | VHAL gear value (do not change)    |

---

## Architecture

```
init (rvc_service.rc)
  └── main()
        ├── GearSelectionMonitor
        │     IVehicle::getService()  [HIDL]
        │     subscribe(GEAR_SELECTION)
        │     onPropertyEvent() ─────────────────────────────────────┐
        │                                                             │
        └── CameraStreamManager                                       │
              ACameraManager_openCamera()  ◄── open() called on  ◄───┘
              ACameraCaptureSession ──► ANativeWindow (encoder surface)
              AMediaCodec (H.264 encoder, Surface input)
              encoderLoop() thread
                AMediaCodec_dequeueOutputBuffer()
                  └── RtpStreamer::sendAnnexB()
                        strip start codes → NAL units
                        single-NAL / FU-A packetisation (RFC 3984)
                        sendto() UDP ──► 192.168.10.10:5004
```

---

## Troubleshooting

| Symptom | Likely cause & fix |
|---|---|
| `adb remount` → "Device must be bootloader unlocked" or `adb push` → "Read-only file system" | Image was built with AVB enabled. Add `BOARD_AVB_ENABLE := false` to `BoardConfig.mk`, rebuild (`m rvc_service`), reflash the vendor image, then retry |
| `IVehicle::getService() returned null` | VHAL not running yet; check `vendor.vhal.initialized` property in `.rc` file |
| `ACameraManager_openCamera failed` | Wrong `CAMERA_ID`, or service doesn't have `camera` group — check `.rc` `group` line |
| Binary builds but crashes on device | Mismatched HIDL version; verify `android.hardware.automotive.vehicle@2.0` is in your device's VINTF |
| No UDP packets at cluster | Verify network route: `adb shell ping 192.168.10.10`; check `RTP_DEST_IP` in `CameraConfig.h` |
| Blurry / corrupted video | Increase `VIDEO_BITRATE` or check for packet loss with `tcpdump -i eth0 udp port 5004` |
