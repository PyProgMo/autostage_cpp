// RasterScan.cpp
#include "RasterScan.h"
#include "Logger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
// for timing: 
#include <windows.h>

namespace {
#include <cerrno>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif
constexpr double kStageTestMinNm = 0.0;
constexpr double kStageTestMaxNm = 300000.0;
constexpr double kStageTestStartNm = 1000.0;
constexpr double kStageTestTickMs = 10.0;
constexpr double kStageTestTickS = kStageTestTickMs / 1000.0;
constexpr double kRowCorrectedMaxCommandNmPerS = 5000.0;

struct RowCorrectedLogRow {
    double timeS = 0.0;
    double elapsedS = 0.0;
    double remainingS = 0.0;
    std::array<double, 3> referenceNm{{0.0, 0.0, 0.0}};
    std::array<double, 3> actualNm{{0.0, 0.0, 0.0}};
    std::array<double, 3> errorNm{{0.0, 0.0, 0.0}};
    std::array<double, 3> actualVelocityNmPerS{{0.0, 0.0, 0.0}};
    std::array<double, 3> actualAccelerationNmPerS2{{0.0, 0.0, 0.0}};
    std::array<double, 3> correction1NmPerS{{0.0, 0.0, 0.0}};
    std::array<double, 3> correction2NmPerS{{0.0, 0.0, 0.0}};
    std::array<double, 3> correction3NmPerS{{0.0, 0.0, 0.0}};
    std::array<double, 3> commandedVelocityNmPerS{{0.0, 0.0, 0.0}};
    bool boundaryHit = false;
};

double clampSymmetric(double value, double limit) {
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

std::string formatRowCorrectedLine(const RowCorrectedLogRow& row) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(3);
    oss << "rowcorr"
        << " t=" << row.timeS
        << " x_ref=" << row.referenceNm[0]
        << " x_act=" << row.actualNm[0]
        << " x_err=" << row.errorNm[0]
        << " vx_cmd=" << row.commandedVelocityNmPerS[0]
        << " vy_cmd=" << row.commandedVelocityNmPerS[1]
        << " vz_cmd=" << row.commandedVelocityNmPerS[2]
        << " c1x=" << row.correction1NmPerS[0]
        << " c2x=" << row.correction2NmPerS[0]
        << " c3x=" << row.correction3NmPerS[0]
        << " rem_s=" << row.remainingS
        << " boundary=" << (row.boundaryHit ? "hit" : "ok");
    return oss.str();
}

void writeRowCorrectedCsvHeader(std::ofstream& out) {
    out << "time_s,elapsed_s,remaining_s,";
    out << "ref_x_nm,ref_y_nm,ref_z_nm,act_x_nm,act_y_nm,act_z_nm,err_x_nm,err_y_nm,err_z_nm,";
    out << "act_vx_nm_s,act_vy_nm_s,act_vz_nm_s,act_ax_nm_s2,act_ay_nm_s2,act_az_nm_s2,";
    out << "c1x_nm_s,c1y_nm_s,c1z_nm_s,c2x_nm_s,c2y_nm_s,c2z_nm_s,c3x_nm_s,c3y_nm_s,c3z_nm_s,";
    out << "cmd_vx_nm_s,cmd_vy_nm_s,cmd_vz_nm_s,boundary_hit\n";
}

void writeRowCorrectedCsvRow(std::ofstream& out, const RowCorrectedLogRow& row) {
    out << row.timeS << ',' << row.elapsedS << ',' << row.remainingS << ',';
    out << row.referenceNm[0] << ',' << row.referenceNm[1] << ',' << row.referenceNm[2] << ',';
    out << row.actualNm[0] << ',' << row.actualNm[1] << ',' << row.actualNm[2] << ',';
    out << row.errorNm[0] << ',' << row.errorNm[1] << ',' << row.errorNm[2] << ',';
    out << row.actualVelocityNmPerS[0] << ',' << row.actualVelocityNmPerS[1] << ',' << row.actualVelocityNmPerS[2] << ',';
    out << row.actualAccelerationNmPerS2[0] << ',' << row.actualAccelerationNmPerS2[1] << ',' << row.actualAccelerationNmPerS2[2] << ',';
    out << row.correction1NmPerS[0] << ',' << row.correction1NmPerS[1] << ',' << row.correction1NmPerS[2] << ',';
    out << row.correction2NmPerS[0] << ',' << row.correction2NmPerS[1] << ',' << row.correction2NmPerS[2] << ',';
    out << row.correction3NmPerS[0] << ',' << row.correction3NmPerS[1] << ',' << row.correction3NmPerS[2] << ',';
    out << row.commandedVelocityNmPerS[0] << ',' << row.commandedVelocityNmPerS[1] << ',' << row.commandedVelocityNmPerS[2] << ',';
    out << (row.boundaryHit ? 1 : 0) << '\n';
    out.flush();
}

// Ensure the parent directory of `path` exists. Returns true if the
// directory existed or was successfully created. Only creates a single
// level (sufficient for the default "build/" path).
bool ensureParentDirExists(const std::string& path) {
    const auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return true;
    }
    const std::string dir = path.substr(0, pos);
#if defined(_WIN32)
    if (_mkdir(dir.c_str()) == 0) return true;
    if (errno == EEXIST) return true;
    return false;
#else
    if (mkdir(dir.c_str(), 0755) == 0) return true;
    if (errno == EEXIST) return true;
    return false;
#endif
}

void runRowCorrectedLoop(PIStageProxy& stage,
                         const std::array<double, 3>& startPos,
                         const std::array<double, 3>& targetPos,
                         double durationS,
                         bool logImportant,
                         const std::string& logPath) 
{
    const double deltaX = targetPos[0] - startPos[0];
    const double deltaY = targetPos[1] - startPos[1];
    const double deltaZ = targetPos[2] - startPos[2];
    
    const double refVx = deltaX / durationS;
    const double refVy = deltaY / durationS;
    const double refVz = deltaZ / durationS;
    const std::array<double, 3> referenceVelocity = {{refVx, refVy, refVz}};

    std::ofstream rowLog;
    if (logImportant) {
        rowLog.open(logPath.c_str(), std::ios::out | std::ios::app);
        if (!rowLog.is_open()) {
            ensureParentDirExists(logPath);
            rowLog.clear();
            rowLog.open(logPath.c_str(), std::ios::out | std::ios::app);
        }
        if (!rowLog.is_open()) {
            throw std::runtime_error("Failed to open row-corrected log file: " + logPath);
        }
        rowLog.seekp(0, std::ios::end);
        if (rowLog.tellp() == 0) {
            writeRowCorrectedCsvHeader(rowLog);
        }
    }

    std::array<double, 3> previousActual = startPos;
    std::array<double, 3> previousVelocity = referenceVelocity;
    std::array<double, 3> filteredActual = startPos;

    // Real-time thread scheduling adjustment
    SetThreadAffinityMask(GetCurrentThread(), 1 << 7); 
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    const double loopms = 5.0; 
    const long long freq = qpc_freq();
    const long long period = (long long)(freq * loopms / 1000.0);

    const long long startQpc = qpc_now();
    long long next = startQpc + period;
    
    // Prevent division-by-zero / spikes on the very first frame
    long long lastTickStart = startQpc; 

    // Compute explicit real-time allocation size to prevent hot-path heap allocations
    size_t expectedTicks = static_cast<size_t>((durationS * 1000.0) / loopms) + 500;
    std::vector<RowCorrectedLogRow> logBuffer;
    logBuffer.reserve(expectedTicks); 
    
    std::vector<std::string> warnBuffer;
    bool boundaryHit = false;

    AppLogger::instance().info("rowcorrected: loop initialized for " + std::to_string(durationS) + "s");

    while (true) {
        // High-precision spin lock
        while (qpc_now() < next) {}

        const long long tickStart = qpc_now();
        
        // Safeguard dtS from being exactly zero or negative due to system anomalies
        double dtS = (double)(tickStart - lastTickStart) / (double)freq;
        if (dtS <= 0.0) {
            dtS = loopms / 1000.0; 
        }
        
        lastTickStart = tickStart;
        next += period;

        const double elapsedS = (double)(tickStart - startQpc) / (double)freq;
        const double progress = std::min(elapsedS / durationS, 1.0);

        const std::array<double, 3> actual = stage.qpos();

        // Exponential smoothing (low-pass filter)
        const double alpha = 0.35;
        for (int axis = 0; axis < 3; ++axis) {
            filteredActual[axis] = alpha * actual[axis] + (1.0 - alpha) * filteredActual[axis];
        }

        RowCorrectedLogRow row;
        row.timeS      = elapsedS;
        row.elapsedS   = elapsedS;
        row.remainingS = std::max(durationS - elapsedS, 0.0);
        
        row.referenceNm[0] = startPos[0] + deltaX * progress;
        row.referenceNm[1] = startPos[1] + deltaY * progress;
        row.referenceNm[2] = startPos[2] + deltaZ * progress;
        
        row.actualNm   = actual;
        row.errorNm[0] = row.referenceNm[0] - filteredActual[0];
        row.errorNm[1] = row.referenceNm[1] - filteredActual[1];
        row.errorNm[2] = row.referenceNm[2] - filteredActual[2];

        // Numerical derivatives for physical state estimation
        for (int axis = 0; axis < 3; ++axis) {
            row.actualVelocityNmPerS[axis]      = (filteredActual[axis] - previousActual[axis]) / dtS;
            row.actualAccelerationNmPerS2[axis] = (row.actualVelocityNmPerS[axis] - previousVelocity[axis]) / dtS;
        }

        const double kp = 0.60; 
        const double kv = 0.80; 
        const double ka = 0.20; 
        const std::array<double, 3> referenceAcceleration = {{0.0, 0.0, 0.0}};

        for (int axis = 0; axis < 3; ++axis) {
            const double positionCorrection     = kp * row.errorNm[axis];
            const double velocityError          = referenceVelocity[axis] - row.actualVelocityNmPerS[axis];
            const double velocityCorrection     = kv * velocityError;
            const double accelerationError      = row.actualAccelerationNmPerS2[axis] - referenceAcceleration[axis];
            const double accelerationCorrection = -ka * accelerationError;

            row.correction1NmPerS[axis] = positionCorrection;
            row.correction2NmPerS[axis] = velocityCorrection;
            row.correction3NmPerS[axis] = accelerationCorrection;

            row.commandedVelocityNmPerS[axis] = referenceVelocity[axis]
                                              + row.correction1NmPerS[axis]
                                              + row.correction2NmPerS[axis]
                                              + row.correction3NmPerS[axis];
                                              
            row.commandedVelocityNmPerS[axis] = clampSymmetric(row.commandedVelocityNmPerS[axis], kRowCorrectedMaxCommandNmPerS);
        }

        row.boundaryHit =
            actual[0] < kStageTestMinNm || actual[0] > kStageTestMaxNm ||
            actual[1] < kStageTestMinNm || actual[1] > kStageTestMaxNm ||
            actual[2] < kStageTestMinNm || actual[2] > kStageTestMaxNm;

        // Hot Path Safe: Vector operations only, zero file systems operations
        logBuffer.push_back(row);

        previousActual   = filteredActual;
        previousVelocity = row.actualVelocityNmPerS;

        if (row.boundaryHit) {
            warnBuffer.push_back("rowcorrected: boundary exceeded, halting stage");
            stage.halt();
            boundaryHit = true;
            break;
        }

        // Output corrections to all 3 axes
        stage.adda(row.commandedVelocityNmPerS[0],
                   row.commandedVelocityNmPerS[1],
                   row.commandedVelocityNmPerS[2]);

        // if traveled by DS trigger the spectrometer here (optional optimization for very long lines, but adds complexity to the loop and timing)

        // Break at the bottom of the iteration to avoid skipping the final adda call
        if (elapsedS >= durationS) {
            break;
        }

        // Real-Time Overrun Monitor
        const long long bodyEnd = qpc_now();
        if (bodyEnd > next) {
            const long long skippedTicks = (bodyEnd - next) / period + 1;
            const double overrunMs = (double)(bodyEnd - tickStart) / (double)freq * 1000.0;
            std::ostringstream oss;
            oss << "Tick overrun: body took " << std::fixed << std::setprecision(3) << overrunMs
                << " ms (budget: " << loopms << " ms), skipping " << skippedTicks << " tick(s)";
            warnBuffer.push_back(oss.str());
            next += skippedTicks * period;
        }
    } // end while(true)

    // --- Post-processing: Offloaded heavy I/O operations outside the loop ---
    for (const auto& w : warnBuffer) {
        AppLogger::instance().warn(w);
    }
    
    for (const auto& r : logBuffer) {
        // Safe CSV generation outside the hot loop
        if (logImportant) {
            writeRowCorrectedCsvRow(rowLog, r);
        }
        const std::string line = formatRowCorrectedLine(r);
        std::cout << line << "\n";
        AppLogger::instance().info(line);
    }

    if (boundaryHit) {
        AppLogger::instance().error("rowcorrected: boundary exceeded, halting stage");
        throw std::runtime_error("rowcorrected aborted: stage left safe bounds");
    }

    // Safety shutdown state sequence
    stage.adda(0.0, 0.0, 0.0);
    AppLogger::instance().info("rowcorrected: completed cleanly");
}

RasterScan::RasterScan(PIStageProxy& stage, AndorCamera& cam)
    : stage_(stage), cam_(cam) {}

void RasterScan::configure(const ScanConfig& cfg) {
    cfg_ = cfg;
    nX_ = static_cast<int>(std::round((cfg_.xStop - cfg_.xStart) / cfg_.xStep)) + 1;
    nY_ = static_cast<int>(std::round((cfg_.yStop - cfg_.yStart) / cfg_.yStep)) + 1;
    if (nX_ <= 0 || nY_ <= 0) {
        throw std::runtime_error("RasterScan::configure produced invalid scan dimensions");
    }
}

ScanResult RasterScan::run() {
    throw std::runtime_error("RasterScan::run is not wired yet; use the console scan command for full scans");
}

void RasterScan::saveCube(const ScanResult& result, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("RasterScan::saveCube failed to open output file: " + path);
    }

    out.write(reinterpret_cast<const char*>(&result.nX), sizeof(int));
    out.write(reinterpret_cast<const char*>(&result.nY), sizeof(int));
    out.write(reinterpret_cast<const char*>(&result.nSpectralPx), sizeof(int));
    out.write(reinterpret_cast<const char*>(result.cube.data()),
              static_cast<std::streamsize>(result.cube.size() * sizeof(uint16_t)));
}

void RasterScan::requestAbort() {
    abortRequested_.store(true);
}

void RasterScan::setupStageTriggers() {}

void RasterScan::teardownStageTriggers() {}

void RasterScan::scanLine(int, ScanResult&) {}

void RasterScan::startrasterscan() {
    throw std::runtime_error("RasterScan::startrasterscan is legacy and not implemented");
}

void RasterScan::runOneRowTest(PIStageProxy& stage, double velocityNmPerS, double xDistanceNm) {
    if (velocityNmPerS < 10.0 || velocityNmPerS > 10000.0) {
        throw std::runtime_error("Velocity must be between 10 and 10000 nm/s");
    }
    if (xDistanceNm <= 0.0) {
        throw std::runtime_error("X distance must be greater than 0 nm");
    }

    const double durationS = xDistanceNm / velocityNmPerS;
    const std::array<double, 3> startPos = {{kStageTestStartNm, kStageTestStartNm, kStageTestStartNm}};
    const std::array<double, 3> targetPos = {{kStageTestStartNm + xDistanceNm, kStageTestStartNm, kStageTestStartNm}};

    if (startPos[0] < kStageTestMinNm || startPos[0] > kStageTestMaxNm ||
        startPos[1] < kStageTestMinNm || startPos[1] > kStageTestMaxNm ||
        startPos[2] < kStageTestMinNm || startPos[2] > kStageTestMaxNm ||
        targetPos[0] < kStageTestMinNm || targetPos[0] > kStageTestMaxNm) {
        throw std::runtime_error("Velocity test position is out of bounds");
    }
    // to move on target: calculate the required velocity to cover the distance in the specified duration, then command a move with that velocity and wait for completion
    double currentpos[3];
    currentpos[0] = stage.getPos("1"); currentpos[1] = stage.getPos("2"); currentpos[2] = stage.getPos("3");
    // take 10 seconds to get to the start position, then wait there for 1 second before starting the test move
    stage.adda((startPos[0] - currentpos[0]) / 10.0, (startPos[1] - currentpos[1]) / 10.0, (startPos[2] - currentpos[2]) / 10.0);
    std::this_thread::sleep_for(std::chrono::seconds(11));

    //stage.moveto(startPos[0], startPos[1], startPos[2]);


    stage.adda(velocityNmPerS, 0.0, 0.0);
    stage.moveto(targetPos[0], targetPos[1], targetPos[2]);

    runRowCorrectedLoop(stage, startPos, targetPos, durationS, false, "build/rowcorrected_log.csv");
}

void RasterScan::runRowCorrected(PIStageProxy& stage,
                                 double durationS,
                                 double xDistanceNm,
                                 bool logImportant,
                                 const std::string& logPath) {
    if (durationS <= 0.0) {
        throw std::runtime_error("Duration must be greater than 0 seconds");
    }
    if (xDistanceNm <= 0.0) {
        throw std::runtime_error("X distance must be greater than 0 nm");
    }

    const std::array<double, 3> startPos = stage.qpos();
    const std::array<double, 3> targetPos = {{startPos[0] + xDistanceNm, startPos[1], startPos[2]}};

    if (startPos[0] < kStageTestMinNm || startPos[0] > kStageTestMaxNm ||
        startPos[1] < kStageTestMinNm || startPos[1] > kStageTestMaxNm ||
        startPos[2] < kStageTestMinNm || startPos[2] > kStageTestMaxNm ||
        targetPos[0] < kStageTestMinNm || targetPos[0] > kStageTestMaxNm) {
        throw std::runtime_error("Row-corrected scan positions are out of bounds");
    }

    stage.moveto(startPos[0], startPos[1], startPos[2]);
    stage.waitOnTarget("1");
    stage.waitOnTarget("2");
    stage.waitOnTarget("3");

    stage.adda(xDistanceNm / durationS, 0.0, 0.0);
    stage.moveto(targetPos[0], targetPos[1], targetPos[2]);

    runRowCorrectedLoop(stage, startPos, targetPos, durationS, logImportant, logPath);
}

static inline long long qpc_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static inline long long qpc_freq() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
}
