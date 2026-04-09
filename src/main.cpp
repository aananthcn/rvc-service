#include <log/log.h>
#include <signal.h>
#include <atomic>

#include "GearSelectionMonitor.h"
#include "CameraStreamManager.h"

// ── Graceful shutdown on SIGTERM / SIGINT ─────────────────────────────────────
static std::atomic<bool> gRunning{true};

static void signalHandler(int /*sig*/) {
    gRunning.store(false);
}

// ── main ──────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/) {
    ALOGI("RearView Camera Service starting…");

    // Install signal handlers so init can stop us cleanly
    signal(SIGTERM, signalHandler);
    signal(SIGINT,  signalHandler);

    // The camera manager lives for the entire service lifetime
    rearview::CameraStreamManager cameraStream;

    // The gear monitor drives the camera: reverse in → open, reverse out → close
    rearview::GearSelectionMonitor gearMonitor(
        /* onReverse    */ [&cameraStream]() { cameraStream.open();  },
        /* onNotReverse */ [&cameraStream]() { cameraStream.close(); }
    );

    if (!gearMonitor.start()) {
        ALOGE("Failed to start GearSelectionMonitor — exiting");
        return 1;
    }

    ALOGI("Service ready — waiting for GEAR_SELECTION events");

    // Block until the init system sends SIGTERM or the process is stopped
    while (gRunning.load()) {
        // pause() sleeps until any signal arrives; the handler sets gRunning=false
        pause();
    }

    ALOGI("Shutdown signal received — cleaning up");
    gearMonitor.stop();
    cameraStream.close();

    ALOGI("RearView Camera Service stopped");
    return 0;
}
