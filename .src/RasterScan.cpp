// RasterScan.cpp
#include "PIStageProxy.h"
#include "AndorCamera.h"
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>

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

    // Andor
    float  exposureS =  0.001f;// 1 ms per spectrum
    
    // Z Profile array (optional)
    std::vector<double> zProfile;
};

void runRasterScan(PIStageProxy& stage, AndorCamera& cam, const ScanConfig& cfg)
{
    int nX = (int)std::round((cfg.xStop - cfg.xStart) / cfg.xStep) + 1;
    int nY = (int)std::round((cfg.yStop - cfg.yStart) / cfg.yStep) + 1;
    int nPix = cam.getXPixels(); // spectral pixels per spectrum

    std::cout << "Scan: " << nX << " x " << nY
              << " points, " << nPix << " spectral px\n";

    // ── 1. Configure PI output trigger (CTO) ─────────────────────────────
    // Fire one TTL pulse every xStep mm along X → triggers one Andor exposure
    stage.configureTriggerOutput(cfg.trigCh, "X",
                                 cfg.xStart, cfg.xStep,
                                 cfg.xStop,  cfg.pulseUs);
    stage.enableTriggerOutput(cfg.trigCh, true);

    // ── 2. Configure Andor: FVB kinetic, fast-external trigger ───────────
    // nX spectra per line (one per CTO pulse)
    cam.configureFVBKinetic(cfg.exposureS, nX);

    // ── 3. Setup PI data recorder (optional: capture actual X positions) ──
    // Table 1: X actual position, option 2 = actual pos
    stage.setupDataRecorder(1, "X", 2);
    // Record every 4th servo cycle
    stage.setRecordRate(4);
    // Trigger recording when next move starts (source=3)
    stage.setRecordTrigger(3);

    // ── 4. Move stage to start position ───────────────────────────────────
    stage.moveAbs("X", cfg.xStart);
    stage.moveAbs("Y", cfg.yStart);
    stage.waitOnTarget("X");
    stage.waitOnTarget("Y");

    // Storage: [yLine][xPoint][spectralPx]
    std::vector<std::vector<std::vector<WORD>>> cube(
        nY, std::vector<std::vector<WORD>>(nX, std::vector<WORD>(nPix, 0)));

    // ── 5. Scan loop ───────────────────────────────────────────────────────
    // Pre-load Z-profile if present
    stage.uploadZProfile(cfg.zProfile);

    // Compute required nominal velocity: v = dx / dt, minus some overhead
    // For safety, say dt is exposure time + 1ms readout. 
    double dt_pixel = cfg.exposureS + 0.001;
    double vNominal = cfg.xStep / dt_pixel;

    for (int iy = 0; iy < nY; iy++) {
        double yPos = cfg.yStart + iy * cfg.yStep;

        // 5a. Move to line start & wait settled
        stage.moveAbs("Y", yPos);
        stage.moveAbs("X", cfg.xStart);
        if (!cfg.zProfile.empty()) stage.moveAbs("Z", cfg.zProfile[0]); // match start altitude
        stage.waitOnTarget("Y");
        stage.waitOnTarget("X");
        if (!cfg.zProfile.empty()) stage.waitOnTarget("Z");

        // 5b. Arm Andor — waiting for nX trigger pulses
        cam.startAcquisition();

        // 5c. Run velocity correction sweep (blocks until X >= xStop)
        stage.runVelocitySweep(vNominal, cfg.xStop, yPos, cfg.xStart, cfg.xStep);

        // 5f. Wait for Andor to finish collecting all nX spectra
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
    stage.enableTriggerOutput(cfg.trigCh, false);

    // ── 7. Read recorder data (actual stage positions) ────────────────────
    const int tables[] = { 1 };
    auto positions = stage.readRecorder(1, nX, tables, 1);
    std::cout << "Actual X[0]=" << positions[0]
              << " X[last]=" << positions.back() << " mm\n";

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
        cfg.xStop    = 0.300;   // 300 um window
        cfg.xStep    = 0.002;   // 2 µm steps
        cfg.yStart   = 0.0;
        cfg.yStop    = 0.300;
        cfg.yStep    = 0.002;
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