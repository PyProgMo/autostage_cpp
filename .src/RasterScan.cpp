// RasterScan.cpp
#include "PIStageProxy.h"
#include "AndorCamera.h"
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>
#include <stdexcept>

// ── Scan parameters ────────────────────────────────────────────────────────
struct ScanConfig {
    // Stage
    double xStart    =  0.0;   // mm
    double xStop     =  5.0;   // mm
    double xStep     =  0.001; // mm  → 1 µm/pixel
    double yStart    =  0.0;   // mm
    double yStop     =  2.0;   // mm
    double yStep     =  0.001; // mm  → 1 µm/line
    int    trigCh    =  1;     // CTO output channel
    int    trigInCh  =  1;     // WTR input channel (Andor "frame done")
    int    pulseUs   =  50;    // CTO pulse width in µs
    bool   useHardwareSync = true; // try CTO/TRO + WGO path first
    bool   allowSoftwareFallback = true; // fall back to unsynchronized scan if hardware sync fails
    bool   useRecorder = false; // enable only after recorder path is validated

    // Andor
    AndorCamera::TriggerMode triggerMode = AndorCamera::TriggerMode::FastExternal;
    float  exposureS =  0.001f;// 1 ms per spectrum
};

namespace {
constexpr double kMaxScanMm = 0.300;      // 300 µm
constexpr double kMinStepMm = 0.000002;   // 2 nm (manual lower bound)
constexpr double kWarnStepMm = 0.000010;  // 10 nm (practical noise floor)

void validateScanAxis(const char* name, double startMm, double stopMm, double stepMm) {
    if (startMm < 0.0 || stopMm < 0.0 || startMm > kMaxScanMm || stopMm > kMaxScanMm) {
        throw std::runtime_error(std::string(name) + ": scan range must stay within 0 to 0.300 mm");
    }
    if (stopMm < startMm) {
        throw std::runtime_error(std::string(name) + ": stop must be greater than or equal to start");
    }
    if (stepMm <= 0.0) {
        throw std::runtime_error(std::string(name) + ": step must be positive");
    }
    if (stepMm < kMinStepMm) {
        throw std::runtime_error(std::string(name) + ": step must be at least 2 nm");
    }
    if (stepMm < kWarnStepMm) {
        std::cerr << "Warning: " << name << " step " << (stepMm * 1e6)
                  << " nm is below the practical 10 nm noise floor\n";
    }
}

void validateScanConfig(const ScanConfig& cfg) {
    validateScanAxis("X", cfg.xStart, cfg.xStop, cfg.xStep);
    validateScanAxis("Y", cfg.yStart, cfg.yStop, cfg.yStep);
}
} // namespace

void runRasterScan(PIStageProxy& stage, AndorCamera& cam, const ScanConfig& cfg)
{
    validateScanConfig(cfg);

    int nX = (int)std::round((cfg.xStop - cfg.xStart) / cfg.xStep) + 1;
    int nY = (int)std::round((cfg.yStop - cfg.yStart) / cfg.yStep) + 1;
    int nPix = cam.getXPixels(); // spectral pixels per spectrum

    std::cout << "Scan: " << nX << " x " << nY
              << " points, " << nPix << " spectral px\n";

    bool hardwareSync = cfg.useHardwareSync;

    // ── 1. Configure PI output trigger (CTO) ─────────────────────────────
    // Fire one TTL pulse every xStep mm along X → triggers one Andor exposure
    if (hardwareSync) {
        try {
            stage.configureTriggerOutput(cfg.trigCh, "X",
                                         cfg.xStart, cfg.xStep,
                                         cfg.xStop,  cfg.pulseUs);
            stage.enableTriggerOutput(cfg.trigCh, true);
        } catch (const std::exception& e) {
            std::cerr << "Hardware sync unavailable: " << e.what() << "\n";
            if (!cfg.allowSoftwareFallback) throw;
            hardwareSync = false;
        }
    }

    // ── 2. Configure Andor: FVB kinetic with trigger fallback ────────────
    // Prefer the requested mode, but fall back if this driver/camera rejects it.
    try {
        cam.configureFVBKinetic(cfg.exposureS, nX, cfg.triggerMode);
    } catch (const std::exception& e) {
        std::cerr << "Andor trigger setup failed: " << e.what() << "\n";
        if (!cfg.allowSoftwareFallback) throw;
        hardwareSync = false;
        cam.configureFVBKinetic(cfg.exposureS, nX, AndorCamera::TriggerMode::Internal);
    }

    // ── 3. Setup PI data recorder (optional: capture actual X positions) ──
    if (cfg.useRecorder) {
        // Table 1: X actual position, option 2 = actual pos
        stage.setupDataRecorder(1, "X", 2);
        // Record every 4th servo cycle
        stage.setRecordRate(4);
        // Trigger recording when next move starts (source=3)
        stage.setRecordTrigger(3);
    }

    // ── 4. Move stage to start position ───────────────────────────────────
    stage.moveAbs("X", cfg.xStart);
    stage.moveAbs("Y", cfg.yStart);
    stage.waitOnTarget("X");
    stage.waitOnTarget("Y");

    // Storage: [yLine][xPoint][spectralPx]
    std::vector<std::vector<std::vector<WORD>>> cube(
        nY, std::vector<std::vector<WORD>>(nX, std::vector<WORD>(nPix, 0)));

    // ── 5. Scan loop ───────────────────────────────────────────────────────
    for (int iy = 0; iy < nY; iy++) {
        double yPos = cfg.yStart + iy * cfg.yStep;

        // 5a. Move to line start & wait settled
        stage.moveAbs("Y", yPos);
        stage.moveAbs("X", cfg.xStart);
        stage.waitOnTarget("Y");
        stage.waitOnTarget("X");

        // 5b. Arm Andor — waiting for nX trigger pulses, or free-running fallback
        cam.startAcquisition();

        // 5c. Arm stage: WGO only if hardware sync is active
        if (hardwareSync) {
            // conditionMask 0x1 = wait for trigger input line 1
            stage.setWaitOnGo("X", 0x1);
        }

        // 5d. Issue X move — stage waits for WGO condition if enabled
        stage.moveAbs("X", cfg.xStop);
        // (stage is now armed but not yet moving)

        // 5e. At this point you would send a SW trigger or wait for HW start.
        //     If using a physical start pulse, the stage releases automatically.
        //     For a pure SW start, send a software trigger here:
        //     stage.SWT(id_, "X", 1);  // if using software WGO trigger

        // 5f. Wait for Andor to finish collecting all nX spectra
        //     WaitForAcquisition blocks until the kinetic series is done
        cam.waitForAcquisition();

        // 5g. Read the completed line of spectra
        std::vector<WORD> lineData = cam.getAllSpectra(nX, nPix);
        for (int ix = 0; ix < nX; ix++)
            cube[iy][ix].assign(lineData.begin() + ix * nPix,
                                lineData.begin() + (ix + 1) * nPix);

        // 5h. Wait for stage to reach end of line
        stage.waitOnTarget("X");

        std::cout << "Line " << iy + 1 << "/" << nY << " done\n";
    }

    // ── 6. Disable output trigger ─────────────────────────────────────────
    if (hardwareSync) {
        stage.enableTriggerOutput(cfg.trigCh, false);
    }

    // ── 7. Read recorder data (actual stage positions) ────────────────────
    if (cfg.useRecorder) {
        const int tables[] = { 1 };
        auto positions = stage.readRecorder(1, nX, tables, 1);
        std::cout << "Actual X[0]=" << positions[0]
                  << " X[last]=" << positions.back() << " mm\n";
    }

    // ── 8. Save hyperspectral cube (raw binary) ───────────────────────────
    std::ofstream out("scan_cube.raw", std::ios::binary);
    out.write(reinterpret_cast<const char*>(&nX),  sizeof(int));
    out.write(reinterpret_cast<const char*>(&nY),  sizeof(int));
    out.write(reinterpret_cast<const char*>(&nPix),sizeof(int));
    for (auto& line : cube)
        for (auto& spec : line)
            out.write(reinterpret_cast<const char*>(spec.data()),
                      spec.size() * sizeof(WORD));
    std::cout << "Saved scan_cube.raw\n";
}

// ── main ────────────────────────────────────────────────────────────────────
int main() {
    try {
        PIStageProxy stage;
        stage.loadDLL("E7XX_GCS2_DLL.dll");
        stage.connect("109021162");

        AndorCamera cam;
        cam.loadDLL("atmcd64d.dll");
        cam.initialize();

        ScanConfig cfg;
        cfg.xStart   = 0.0;
        cfg.xStop    = 0.300;   // 300 µm range limit
        cfg.xStep    = 0.001;   // 1 µm steps (safe default)
        cfg.yStart   = 0.0;
        cfg.yStop    = 0.300;   // 300 µm range limit
        cfg.yStep    = 0.001;
        cfg.exposureS = 0.002f; // 2 ms

        runRasterScan(stage, cam, cfg);

        cam.shutdown();
        stage.disconnect();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}