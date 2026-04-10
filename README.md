# rvc_service — Gear Monitor Daemon

A native C++ **vendor** service that subscribes to the Vehicle HAL
`GEAR_SELECTION` property (via AIDL V4) and writes the system property
`vendor.rvc.camera.active=1/0` to signal `rvc_app` whenever the gear
enters or leaves REVERSE.

`rvc_service` is one half of a two-process architecture:

```
rvc_service  (vendor)  ── vendor.rvc.camera.active ──►  rvc_app  (system)
  VHAL gear monitor                                  Camera + encoder + RTP
```

---

## Tree placement

```
<AOSP_ROOT>/vendor/brcm/rvc-service/
├── Android.bp
├── rvc_service.rc
├── sepolicy/
│   ├── rvc_service.te
│   ├── property.te
│   ├── property_contexts
│   └── file_contexts
├── src/
│   ├── main.cpp
│   ├── GearSelectionMonitor.cpp
│   └── PropertyController.cpp
└── include/
    ├── GearConfig.h
    ├── GearSelectionMonitor.h
    └── PropertyController.h
```

`BoardConfig.mk` must include the sepolicy directory:

```makefile
BOARD_SEPOLICY_DIRS += vendor/brcm/rvc-service/sepolicy
```

---

## Build

```bash
source build/envsetup.sh
lunch <your_target>

# Module only (fast iteration)
mmm vendor/brcm/rvc-service

# Output
out/target/product/<device>/vendor/bin/rvc_service
```

---

## Device integration

In `device/<oem>/<board>/device.mk`:

```makefile
PRODUCT_PACKAGES += rvc_service
```

The `init_rc` entry in `Android.bp` installs `rvc_service.rc` to
`/vendor/etc/init/` automatically.

---

## Push and test without reflashing

```bash
adb root
adb shell mount -o remount,rw /vendor

adb push out/target/product/<device>/vendor/bin/rvc_service \
         /vendor/bin/rvc_service

adb shell stop  rvc_service
adb shell start rvc_service

adb logcat -s RearViewCameraSvc
```

> `/vendor` is a separate ext4 partition on RPi5 (`mmcblk0p6`), so
> remounting it independently works. The system partition (`mmcblk0p5`)
> is mounted as root (`/`) — use `mount -o remount,rw /` to make
> `/system/bin` writable when pushing `rvc_app`.

---

## Simulate gear events

```bash
# Engage reverse
adb shell cmd car_service inject-vhal-event 0x11400400 8

# Leave reverse (drive)
adb shell cmd car_service inject-vhal-event 0x11400400 4
```

Expected logcat output:

```
I RearViewCameraSvc: GearMonitor: connected to Vehicle HAL (attempt 1)
I RearViewCameraSvc: GearMonitor: Subscribed to GEAR_SELECTION (propId=0x11400400)
I RearViewCameraSvc: GearMonitor: >>> REVERSE engaged — starting camera stream
I RearViewCameraSvc: GearMonitor: set vendor.rvc.camera.active=1 (REVERSE)
I RearViewCameraSvc: GearMonitor: >>> Out of REVERSE — stopping camera stream
I RearViewCameraSvc: GearMonitor: set vendor.rvc.camera.active=0 (NOT REVERSE)
```

---

## Configuration

All VHAL constants are in `include/GearConfig.h`:

| Constant | Value | Description |
|---|---|---|
| `PROP_GEAR_SELECTION` | `0x11400400` | VHAL property ID — do not change |
| `GEAR_REVERSE` | `8` | VHAL gear value for reverse — do not change |
| `RVC_PROP_CAMERA_ACTIVE` | `vendor.rvc.camera.active` | Property written to signal rvc_app |

---

## Architecture

```
init  (rvc_service.rc)
  on property:init.svc.vendor.vehicle-hal-default=running
    └── main()
          ABinderProcess_startThreadPool()          ← binder thread for callbacks
          PropertyController::notifyNotReverse()    ← initialise property to "0"
          GearSelectionMonitor::start()
            AServiceManager_checkService(           ← AIDL V4, retry up to 30s
              "android.hardware.automotive.vehicle.IVehicle/default")
            IVehicle::subscribe(GEAR_SELECTION, ON_CHANGE)
            IVehicle::getValues(GEAR_SELECTION)     ← read initial gear (async)
            │
            │  onPropertyEvent() ← delivered on binder thread
            │    evaluateGear(gear)
            │      gear == 8?
            │        yes → PropertyController::notifyReverse()
            │                SetProperty("vendor.rvc.camera.active", "1")
            │        no  → PropertyController::notifyNotReverse()
            │                SetProperty("vendor.rvc.camera.active", "0")
            │
            └── main() pauses on signal
```

---

## Troubleshooting

| Symptom | Likely cause & fix |
|---|---|
| No logcat output after `start rvc_service` | Logs are from boot; `stop` then `start` to see fresh output |
| `VHAL not ready, retrying` loop | Service started before VHAL; the `.rc` trigger `on property:init.svc.vendor.vehicle-hal-default=running` fires it at the right time after reflash |
| `Vehicle HAL unavailable after 30 attempts` | VHAL service name mismatch; verify with `adb shell service list \| grep -i vehicle` |
| `avc: denied { call } … servicemanager` in logcat | SELinux audit (permissive); add `binder_use(rvc_service)` and `allow rvc_service hal_vehicle_service:service_manager find` to `sepolicy/rvc_service.te` and rebuild |
| Gear events received but `vendor.rvc.camera.active` not changing | Check `set_prop(rvc_service, vendor_rvc_prop)` is in `sepolicy/rvc_service.te` |
| Property changes but `rvc_app` does not react | Check `rvc_app` is running: `adb shell getprop init.svc.rvc_app` |
