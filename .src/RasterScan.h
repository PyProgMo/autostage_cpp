// RasterScan.h
#pragma once
#include "PIStageProxy.h"
#include "AndorCameraProxy.h"
#include <vector>
#include <string>
#include <atomic>
#include <cstdint>

// ── Scan configuration ─────────────────────────────────────────────────────
struct ScanConfig {
    // Stage geometry
    double xStart     =  0.0;    // mm — scan line start
    double xStop      =  0.300;  // mm — scan line end (300 µm supported window)
    double xStep      =  0.001;   // mm — pixel pitch along X (= CTO step)
    double yStart     =  0.0;    // mm — first line
    double yStop      =  0.300;   // mm — last line (300 µm supported window)
    double yStep      =  0.001;   // mm — line pitch along Y

    // Trigger
    int    trigOutCh  =  1;      // CTO/TRO output channel index
    int    trigInCh   =  1;      // WTR input channel (if used)
    int    pulseUs    =  50;     // CTO pulse width in µs

    // WGO condition mask for X scan start
    // 0x1 = wait for trigger input line 1
    // 0x0 = no wait (immediate, SW-controlled)
    int    wgoMask    =  0x0;

    // Andor
    float  exposureS  =  0.001f; // seconds per spectrum

    // Data recorder
    bool   useRecorder = true;   // record actual X positions via DRR
    int    recorderRate = 4;     // record every N servo cycles

    // Output
    std::string outputPath = "scan_cube.raw";
    
    // Z Profile array (optional)
    std::vector<double> zProfile;
};

// ── Result container ───────────────────────────────────────────────────────
struct ScanResult {
    int nX;           // number of X points per line
    int nY;           // number of scan lines
    int nSpectralPx;  // spectral pixels per spectrum (from Andor detector width)

    // Hyperspectral cube: [yLine * nX + xPoint][spectralPixel]
    // Flattened for contiguous memory — use index helper below
    std::vector<uint16_t> cube;

    // Actual stage X positions recorded by DRR (empty if useRecorder=false)
    std::vector<double> recordedXPos;

    // Convenience accessor: cube[iy][ix][iLambda]
    uint16_t& at(int iy, int ix, int iLambda) {
        return cube[(iy * nX + ix) * nSpectralPx + iLambda];
    }
    const uint16_t& at(int iy, int ix, int iLambda) const {
        return cube[(iy * nX + ix) * nSpectralPx + iLambda];
    }
};

// ── RasterScan class ───────────────────────────────────────────────────────
class RasterScan {
public:
    RasterScan(PIStageProxy& stage, AndorCameraProxy& cam);

    // Diagnostic one-row test scan used by the console velocitytest command.
    static void runOneRowTest(PIStageProxy& stage, AndorCameraProxy& cam, double velocityNmPerS, double xDistanceNm, double stepsize_nm, bool logImportant = false, int tdead_perspec = 60);

    // Corrected one-row scan that follows a fixed duration target and can optionally log to a file.
    static void runRowCorrected(PIStageProxy& stage,
                                AndorCameraProxy& cam,
                                double durationS,
                                double xDistanceNm,
                                double stepsize_nm,
                                bool logImportant = false,
                                const std::string& logPath = "build/rowcorrected_log.csv");
    
    // Configure area scan, that scans an area looping over runRowCorrected for each line. This will validate the config and pre-calculate nX/nY.
    static void runAreaScan(ScanConfig& cfg, 
                        PIStageProxy& stage,
                        AndorCameraProxy& cam,
                        bool logImportant = true,
                        const std::string& logPathPrefix = "build/rowcorrected_line_", 
                        const std::string& outputCubePath = "build/scan_cube.raw", 
                        double durationSPerLine = 1.0,
                        double xDistanceNm = 1000.0,
                        double yDistanceNm = 1000.0, 
                        double zDistanceNm = 0.0, 
                        double rowdistanceNm = 100.0
            );                    

    // Validate config and pre-calculate nX / nY
    void configure(const ScanConfig& cfg);

    // Run the full scan — blocks until complete
    ScanResult run();

    // Save raw binary cube (header: int nX, int nY, int nPx; then uint16 data)
    static void saveCube(const ScanResult& result, const std::string& path);

    // Abort from another thread (sets atomic flag)
    void requestAbort();

    // ── Placeholder for future functionalities ──────────────────────────────
    void startrasterscan(); // legacy function to run scan without using ScanConfig struct (for quick testing)
    // TODO: Add callback mechanism for scan progress (e.g., updating UI)
    // void setProgressCallback(std::function<void(int currentLine, int totalLines)> cb);

    // TODO: Metadata management for the scan cube
    // void addScanMetadata(const std::string& key, const std::string& value);

    // TODO: Support for additional sensors along the pipe
    // void attachSecondarySensor(SensorProxy& sensor);
    // ────────────────────────────────────────────────────────────────────────

private:
    PIStageProxy& stage_;
    AndorCameraProxy& cam_;
    ScanConfig   cfg_;

    int nX_ = 0;
    int nY_ = 0;

    std::atomic<bool> abortRequested_{ false };

    void setupStageTriggers();
    void teardownStageTriggers();
    void scanLine(int iy, ScanResult& result);
};
