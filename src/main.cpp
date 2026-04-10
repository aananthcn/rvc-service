#include <android/binder_process.h>
#include <log/log.h>
#include <signal.h>
#include <atomic>

#include "GearSelectionMonitor.h"
#include "PropertyController.h"
#include "GearConfig.h"

// ── Graceful shutdown on SIGTERM / SIGINT ─────────────────────────────────────
static std::atomic<bool> gRunning{true};

static void signalHandler(int /*sig*/) {
    gRunning.store(false);
}

// ── main ──────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/) {
    ALOGI("RearView Camera Service starting…");

    signal(SIGTERM, signalHandler);
    signal(SIGINT,  signalHandler);

    // Start a binder thread pool so VHAL can deliver onPropertyEvent callbacks.
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();

    // Property controller writes vendor.rvc.camera.active=1/0.
    // rvc_app watches this property and controls the camera pipeline.
    rearview::PropertyController propCtrl(rearview::RVC_PROP_CAMERA_ACTIVE);

    // Initialise to 0 so rvc_app starts in a known state (handles restarts).
    propCtrl.notifyNotReverse();

    // Gear monitor drives the property: reverse → "1", not reverse → "0".
    rearview::GearSelectionMonitor gearMonitor(
        /* onReverse    */ [&propCtrl]() { propCtrl.notifyReverse();    },
        /* onNotReverse */ [&propCtrl]() { propCtrl.notifyNotReverse(); }
    );

    if (!gearMonitor.start()) {
        ALOGE("Failed to start GearSelectionMonitor — exiting");
        return 1;
    }

    ALOGI("Service ready — waiting for GEAR_SELECTION events");

    while (gRunning.load()) {
        pause(); // sleep until any signal arrives
    }

    ALOGI("Shutdown signal received — cleaning up");
    gearMonitor.stop();
    propCtrl.notifyNotReverse(); // ensure rvc_app stops the stream on shutdown

    ALOGI("RearView Camera Service stopped");
    return 0;
}
