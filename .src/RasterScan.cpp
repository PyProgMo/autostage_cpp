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
    double xStart    =  0.0;   // nm
    double xStop     =  5.0;   // nm
    double zPos      =  0.0;   // nm
    double xStep     =  0.001; // nm  → 1e-3 nm (use larger defaults in main)
    double yStart    =  0.0;   // nm
    double yStop     =  2.0;   // nm
    double yStep     =  0.001; // nm
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
    // nX is also the number of spectra per line, since we trigger one spectrum per xStep
    int nY = (int)std::round((cfg.yStop - cfg.yStart) / cfg.yStep) + 1;
    // nY is also the number of lines in the scan
    int nPix = cam.getXPixels(); // spectral pixels per spectrum

    std::cout << "Scan: " << nX << " x " << nY
              << " points, " << nPix << " spectral px\n";

    // ── 1. Configure PI output trigger (CTO) ─────────────────────────────
    // Fire one TTL pulse every xStep (converted to mm) along X → triggers one Andor exposure
    // pass nm directly; proxy will convert to µm
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
    // Pre-load Z-profile if present (convert nm -> mm for stage API)
    if (!cfg.zProfile.empty()) {
        // pass nm zProfile directly; proxy will convert to µm
        stage.uploadZProfile(cfg.zProfile);
    }

    // Compute required nominal velocity: v = dx / dt, minus some overhead
    // For safety, say dt is exposure time + 1ms readout. 
    double dt_pixel = cfg.exposureS + 0.001;
    // vNominal in mm/s: convert xStep from nm -> mm first
    // vNominal in nm/s (cfg.xStep is nm): proxy will convert to µm/s
    double vNominal = cfg.xStep / dt_pixel;

    for (int iy = 0; iy < nY; iy++) {
        double yPos = cfg.yStart + iy * cfg.yStep;

        // 5a. Move to line start & wait settled (convert nm -> mm)
        stage.moveAbs("Y", yPos);
        stage.moveAbs("X", cfg.xStart);
        if (!cfg.zProfile.empty()) stage.moveAbs("Z", cfg.zProfile[0]); // match start altitude
        stage.waitOnTarget("Y");
        stage.waitOnTarget("X");
        if (!cfg.zProfile.empty()) stage.waitOnTarget("Z");

        // 5b. Arm Andor — waiting for nX trigger pulses
        cam.startAcquisition();

        // 5c. Run velocity correction sweep (blocks until X >= xStop)
        stage.runVelocitySweep(vNominal,
                   cfg.xStop,
                   yPos,
                   cfg.xStart,
                   cfg.xStep);

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
    // positions are returned in µm from the stage API; print in nm for user
    std::cout << "Actual X[0]=" << positions[0] * 1e3
              << " X[last]=" << positions.back() * 1e3 << " nm\n";

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

void autostagescan(PIStageProxy& stage, AndorCamera& cam, const ScanConfig& cfg)
{
    int nX = (int)std::round((cfg.xStop - cfg.xStart) / cfg.xStep) + 1;
    // nX is also the number of spectra per line, since we trigger one spectrum per xStep
    int nY = (int)std::round((cfg.yStop - cfg.yStart) / cfg.yStep) + 1;
    // nY is also the number of lines in the scan
    int nPix = cam.getXPixels(); // spectral pixels per spectrum
    
    // ── 1. Configure PI output trigger (CTO) ─────────────────────────────
    // sepctrometer set trigger to external, THIS script will fire the trigger. 
    // it will be the brain that decides when to fire the trigger, and how many spectra to acquire per line (nX)
    cam.setTriggerMode(1); // external trigger mode

    // move stage to start position (pass nm to proxy which will convert to µm)
    stage.moveAbs("X", cfg.xStart);
    stage.moveAbs("Y", cfg.yStart);


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
        // Defaults are in nanometres (nm)
        cfg.xStart   = 0.0;
        cfg.xStop    = 300000.0;   // 300000 nm = 300 µm window
        cfg.xStep    = 2000.0;     // 2000 nm = 2 µm steps
        cfg.yStart   = 0.0;
        cfg.yStop    = 300000.0;
        cfg.yStep    = 2000.0;
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