// RasterScan.cpp
#include "RasterScan.h"
#include "Logger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <iomanip>
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
constexpr double kStageTestMaxNm = 300000.0; // unit: nm
double kStageTestStartNm = 1000.0;
constexpr double kStageTestTickMs = 10.0;
constexpr double kStageTestTickS = kStageTestTickMs / 1000.0;
// kRowCorrectedMaxCommandNmPerS is the maximum velocity command that will be sent to the stage during a row-corrected scan. This is a safety limit to prevent the stage from being commanded to move too fast, which could cause it to overshoot or behave unpredictably. The actual maximum velocity of the stage may be higher, but we limit it here for safety and stability of the scan.
constexpr double kRowCorrectedMaxCommandNmPerS = 10000.0;
}
static inline long long qpc_now();
static inline long long qpc_freq();

void waitUntilQpc(long long targetQpc, long long freq) {
    const long long yieldThreshold = std::max(1LL, freq / 1000LL);
    while (true) {
        const long long now = qpc_now();
        if (now >= targetQpc) {
            return;
        }

        const long long remaining = targetQpc - now;
        if (remaining > yieldThreshold) {
            Sleep(0);
            continue;
        }

        while (qpc_now() < targetQpc) {
        }
        return;
    }
}

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

static void startRowScanSimple(PIStageProxy& stage, 
        AndorCameraProxy& cam, 
        const std::array<double, 3>& pos, 
        double xDistanceNm, 
        double yNsteps, 
        double stepsize_nm, 
        // tiny dead time in ms, used to calculate velocity for the scan
        double tint_ms = 100,
        double tdead_perspec_ms = 100,
        bool logImportant = false, 
        const std::string& logPathPrefix = "build/rowcorrected_line_"
    )
    {
        // 1: approach: move to start position with velocity of pos-startpos/5
        auto cpos = stage.qpos(); // units: nm
        stage.adda((pos[0] - cpos[0]) / 5, (pos[1] - cpos[1]) / 5, (pos[2] - cpos[2]) / 5);
        stage.moveto(pos[0], pos[1], pos[2]);
        // 2: wait until we are at the start position
        int timeout = 6000; // 6 seconds timeout
        while (true) {
            cpos = stage.qpos();
            if (std::abs(cpos[0] - pos[0]) < 1.0 && std::abs(cpos[1] - pos[1]) < 1.0 && std::abs(cpos[2] - pos[2]) < 1.0) {
                break;
            }
            Sleep(10);
            timeout -= 10;
            if (timeout <= 0) {
                throw std::runtime_error("Timeout waiting for stage to reach start position");
            }
        }
        // 3: move to end position with constant velocity, measure a spectrum every stepsize_nm. Collect each row, save later. 
        // iterate over the yNsteps, for each, call runRowScanSimple, then move to the next y position.
        for (int i = 0; i < yNsteps; ++i) {
            double yPos = pos[1] + i * stepsize_nm;
            // first row, move in x-direction, then move in y-direction for the next row (backward and forward scanning can be implemented later)
            std::array<double, 3> rowStartPos = {pos[0], yPos, pos[2]};
            std::array<double, 3> rowEndPos = {pos[0] + xDistanceNm, yPos, pos[2]};
            if (i%2 == 1) {
                rowStartPos = {pos[0], yPos, pos[2]};
                rowEndPos = {pos[0] + xDistanceNm, yPos, pos[2]};
            } else {
                rowStartPos = {pos[0] + xDistanceNm, yPos, pos[2]};
                rowEndPos = {pos[0], yPos, pos[2]};
            }
            // move to the start position of the row within 100 ms, then start the row scan with constant velocity, measure and save a spectrum every stepsize_nm, optionally log to a file.
            cpos = stage.qpos();
            stage.adda((rowStartPos[0] - cpos[0]) / 0.1, (rowStartPos[1] - cpos[1]) / 0.1, (rowStartPos[2] - cpos[2]) / 0.1);
            // waite 200 ms for the stage to reach the start position
            Sleep(200);

            RasterScan::runRowScanSimple(stage, cam, rowStartPos, xDistanceNm, stepsize_nm, logImportant, logPathPrefix + "row_" + std::to_string(i) + ".csv");
        }

    }

// keep things simple: scan one row with constant velocity, measure and save a spectrum every stepsize_nm, optionally log to a file. This is a simpler version of runRowCorrected that does not attempt to correct for stage motion errors.
void RasterScan::runRowScanSimple(PIStageProxy& stage,
                                AndorCameraProxy& cam,
                                const std::array<double, 3>& startpos,
                                double xDistanceNm,
                                double stepsize_nm,
                                bool logImportant = false,
                                const std::string& logPath = "build/rowcorrected_log.csv",
                                double tint_ms = 100,
                                double tdead_perspec_ms = 100
                            ) 
    {   
        std::array<double, 3> qpos = stage.qpos();
        std::array<double, 3> endpos = {startpos[0] + xDistanceNm, startpos[1], startpos[2]};
        double totalDistanceNm = xDistanceNm;
        double timeperspec_ms = tint_ms + tdead_perspec_ms;
        double totalTime_ms = (totalDistanceNm / stepsize_nm) * timeperspec_ms;
        double velocityNmPerS = (totalDistanceNm / totalTime_ms) * 1000.0; // convert ms to s
        if (velocityNmPerS > kRowCorrectedMaxCommandNmPerS) {
            throw std::runtime_error("Requested velocity exceeds maximum allowed value");

        } else {
            AppLogger::instance().info("runRowScanSimple: moving from " + std::to_string(startpos[0]) + " to " + std::to_string(endpos[0]) + " nm at velocity " + std::to_string(velocityNmPerS) + " nm/s");
            stage.adda(velocityNmPerS, 0.0, 0.0);
            stage.moveto(endpos[0], endpos[1], endpos[2]);
            // loop over for loop to measure and save a spectrum every timeperspec_ms, optionally log to a file.
            for (double x = startpos[0]; x <= endpos[0]; x += stepsize_nm) {
                std::array<double, 3> currentPos = {x, startpos[1], startpos[2]};
                cam.AcquireSpecandSave("build/measurement", currentPos[0], currentPos[1], currentPos[2], "spec_" + std::to_string(static_cast<int>(x)));
                if (logImportant) {
                    std::ofstream logFile(logPath, std::ios::app);
                    if (!logFile.is_open()) {
                        ensureParentDirExists(logPath);
                        logFile.clear();
                        logFile.open(logPath, std::ios::app);
                    }
                    if (!logFile.is_open()) {
                        throw std::runtime_error("Failed to open log file: " + logPath);
                    }
                    logFile << "Measured spectrum at position: " << currentPos[0] << ", " << currentPos[1] << ", " << currentPos[2] << "\n";
                }
                Sleep(static_cast<DWORD>(timeperspec_ms));
            }
            
        }
    }

void runRowCorrectedLoop(PIStageProxy& stage,
                         AndorCameraProxy& cam,
                         const std::array<double, 3>& startPos,
                         const std::array<double, 3>& targetPos,
                         double stepsize_nm,
                         int measurementtime_ms, 
                         double durationmS, // total duration of the scan in ms
                         bool logImportant,
                         const std::string& logPath) 
{
    if (durationmS <= 0.0) {
        throw std::runtime_error("runRowCorrectedLoop requires a positive duration");
    }

    const double deltaX = targetPos[0] - startPos[0]; // units: nm
    const double deltaY = targetPos[1] - startPos[1];
    const double deltaZ = targetPos[2] - startPos[2];
    
    const double refVx = deltaX / durationmS; // units: nm / ms = nm/ms = mum/s
    const double refVy = deltaY / durationmS;
    const double refVz = deltaZ / durationmS;
    const std::array<double, 3> referenceVelocity = {{refVx, refVy, refVz}};
    double lastspecX = startPos[0];
    double lastspecY = startPos[1];
    double lastspecZ = startPos[2];
    double currentpos[3] = {startPos[0], startPos[1], startPos[2]};


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

    std::array<double, 3> previousRawActual = startPos;
    std::array<double, 3> previousRawVelocity = referenceVelocity;
    std::array<double, 3> filteredActual = startPos;

    // Real-time thread scheduling adjustment
    SetThreadAffinityMask(GetCurrentThread(), 1 << 7); 
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    const double loopms = 5.0; 
    const long long freq = qpc_freq();
    if (freq <= 0) {
        throw std::runtime_error("QueryPerformanceFrequency returned an invalid value");
    }
    const long long period = (long long)(freq * loopms / 1000.0);
    if (period <= 0) {
        throw std::runtime_error("Row-corrected loop period is invalid");
    }

    const long long startQpc = qpc_now();
    long long tickIndex = 0;
    long long lastTickStart = startQpc;

    // example calculation expected ticks for durationS = 1000 ms, loopms = 5 ms: expectedTicks = 1000 / 5 + 2048 = 4048

    // Compute explicit real-time allocation size to prevent hot-path heap allocations.
    // Keep extra headroom so longer-than-expected runs can keep logging without reallocating.
    const size_t expectedTicks = static_cast<size_t>((durationmS) / loopms) + 2048;
    const size_t maxLogRows = expectedTicks * 2;
    std::deque<RowCorrectedLogRow> logBuffer;
    
    std::vector<std::string> warnBuffer;
    warnBuffer.reserve(expectedTicks / 4 + 64);
    bool boundaryHit = false;
    size_t dtFallbackCount = 0;
    size_t logTruncationCount = 0;

    AppLogger::instance().info("rowcorrected: loop initialized for " + std::to_string(durationmS) + "ms");
    stage.moveto(targetPos[0], targetPos[1], targetPos[2]);

    int specNum = 0;

    std::string measurementFolder = "build/measurement_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::string filename = "spec_" + std::to_string(specNum);

    // tell the spectrometer to measure by calling cam->AcquireSpecandSave(measurementFolder, filename);
    auto pos = stage.qpos();

    cam.AcquireSpecandSave(measurementFolder, pos[0], pos[1], pos[2], filename);
    lastspecX = stage.getPos("1")*1000; // Convert to nanometers
    specNum++;

    while (true) {
        const long long nowQpc = qpc_now();
        const long long expectedTickIndex = ((nowQpc - startQpc) / period) + 1;
        if (expectedTickIndex > tickIndex + 1) {
            tickIndex = expectedTickIndex - 1;
        }

        ++tickIndex;
        const long long next = startQpc + tickIndex * period;

        // Hybrid wait: yield while far away, spin only near the deadline.
        waitUntilQpc(next, freq);

        const long long tickStart = qpc_now();
        
        // Safeguard dtS from being exactly zero or negative due to system anomalies
        double dtS = (double)(tickStart - lastTickStart) / (double)freq;
        if (dtS <= 0.0) {
            dtS = loopms / 1000.0;
            ++dtFallbackCount;
        }
        
        lastTickStart = tickStart;

        // elapsedS is the total time since the start of the row-corrected loop, in seconds
        const double elapsedS = (double)(tickStart - startQpc) / (double)freq;
        const double progress = std::min(elapsedS / durationmS/1000, 1.0);

        const std::array<double, 3> actual = stage.qpos();

        // Exponential smoothing (low-pass filter)
        const double alpha = 0.35;
        for (int axis = 0; axis < 3; ++axis) {
            filteredActual[axis] = alpha * actual[axis] + (1.0 - alpha) * filteredActual[axis];
        }

        RowCorrectedLogRow row;
        row.timeS      = elapsedS;
        row.elapsedS   = elapsedS;
        row.remainingS = std::max(durationmS/1000 - elapsedS, 0.0);
        
        row.referenceNm[0] = startPos[0] + deltaX * progress;
        row.referenceNm[1] = startPos[1] + deltaY * progress;
        row.referenceNm[2] = startPos[2] + deltaZ * progress;
        
        row.actualNm   = actual;
        row.errorNm[0] = row.referenceNm[0] - filteredActual[0];
        row.errorNm[1] = row.referenceNm[1] - filteredActual[1];
        row.errorNm[2] = row.referenceNm[2] - filteredActual[2];

        // Numerical derivatives for physical state estimation
        for (int axis = 0; axis < 3; ++axis) {
            row.actualVelocityNmPerS[axis]      = (actual[axis] - previousRawActual[axis]) / dtS;
            row.actualAccelerationNmPerS2[axis] = (row.actualVelocityNmPerS[axis] - previousRawVelocity[axis]) / dtS;
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

        const bool rawBoundaryHit =
            actual[0] < kStageTestMinNm || actual[0] > kStageTestMaxNm ||
            actual[1] < kStageTestMinNm || actual[1] > kStageTestMaxNm ||
            actual[2] < kStageTestMinNm || actual[2] > kStageTestMaxNm;
        const bool filteredBoundaryHit =
            filteredActual[0] < kStageTestMinNm || filteredActual[0] > kStageTestMaxNm ||
            filteredActual[1] < kStageTestMinNm || filteredActual[1] > kStageTestMaxNm ||
            filteredActual[2] < kStageTestMinNm || filteredActual[2] > kStageTestMaxNm;
        row.boundaryHit = rawBoundaryHit || filteredBoundaryHit;

        // Hot Path Safe: Vector operations only, zero file systems operations
        if (logBuffer.size() >= maxLogRows) {
            logBuffer.pop_front();
            ++logTruncationCount;
        }
        logBuffer.push_back(row);

        previousRawActual   = actual;
        previousRawVelocity = row.actualVelocityNmPerS;

        if (row.boundaryHit) {
            warnBuffer.push_back("rowcorrected: boundary exceeded, halting stage");
            stage.halt();
            boundaryHit = true;
            break;
        }

        // Output corrections to all 3 axes
        stage.adda(std::abs(row.commandedVelocityNmPerS[0]),
                   std::abs(row.commandedVelocityNmPerS[1]),
                   std::abs(row.commandedVelocityNmPerS[2]));

        // if traveled by DS trigger the spectrometer here (optional optimization for very long lines, but adds complexity to the loop and timing)
        if (std::abs(actual[0] - lastspecX) >= stepsize_nm) {
            lastspecX = actual[0];
            // update metadata: call stage.qpos() and put result into double xPosNm, yPosNm, zPosNm; 
            std::array<double, 3> posNm = stage.qpos();
            // put posNm into metadata: xPosNm, yPosNm, zPosNm

            // Trigger spectrometer acquisition here
            // measure, but please in another thread!!!

            //cam.AcquireSpecandSave(measurementFolder, posNm[0], posNm[1], posNm[2], "spec_" + std::to_string(specNum) + ".txt");

            // call cam.AcquireSpecandSave in a separate thread to avoid blocking the main loop
            std::thread([&cam, measurementFolder, posNm, specNum]() {
                std::string filename = "spec_" + std::to_string(specNum) + ".txt";
                cam.AcquireSpecandSave(measurementFolder, posNm[0], posNm[1], posNm[2], filename);
            }).detach();

            specNum++;
            lastspecX = actual[0];
        }

        // Break at the bottom of the iteration to avoid skipping the final adda call
        if (elapsedS >= durationmS/1000) {
            break;
        }

        // Real-Time Overrun Monitor
        const long long bodyEnd = qpc_now();
        if (bodyEnd > next) {
            const long long overdueTicks = (bodyEnd - next) / period + 1;
            const double overrunMs = (double)(bodyEnd - tickStart) / (double)freq * 1000.0;
            std::ostringstream oss;
            oss << "Tick overrun: body took " << std::fixed << std::setprecision(3) << overrunMs
                << " ms (budget: " << loopms << " ms), overdue by " << overdueTicks << " tick(s)";
            warnBuffer.push_back(oss.str());
        }
    } // end while(true)

    // wait until all cam.AcquireSpecandSave threads are done before proceeding to log and cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // wait for 500 ms to allow any detached threads to finish (adjust as needed)

    if (dtFallbackCount > 0) {
        warnBuffer.push_back("rowcorrected: non-positive dtS encountered " + std::to_string(dtFallbackCount) + " time(s)");
    }
    if (logTruncationCount > 0) {
        warnBuffer.push_back("rowcorrected: log buffer truncated " + std::to_string(logTruncationCount) + " time(s)");
    }

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

RasterScan::RasterScan(PIStageProxy& stage, AndorCameraProxy& cam)
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

void RasterScan::runOneRowTest(PIStageProxy& stage, AndorCameraProxy& cam, double t_measure, double xDistanceNm, double stepsize_nm, bool logImportant, int tdead_perspec_ms) {
    //velocity v = x/t, x=stepsize_nm, t = t_measure+tdead_perspec*1.2; // add 20% margin to dead time
    const double velocityNmPerS = stepsize_nm / ((t_measure + tdead_perspec_ms * 1.2)); // velocity in nm/ms = mum/s with 20% margin to dead time

    // debug: print the calculated velocity and the input parameters
    std::cout << "Calculated velocity: " << velocityNmPerS << " nm/s" << std::endl;
    std::cout << "Input parameters: t_measure=" << t_measure << ", xDistanceNm=" << xDistanceNm << ", stepsize_nm=" << stepsize_nm << ", tdead_perspec=" << tdead_perspec_ms << std::endl;

    if (velocityNmPerS < 0.001 || velocityNmPerS > 100.0) {
        throw std::runtime_error("Velocity must be between 0.001 and 100.0 nm/s");
    }
    if (xDistanceNm <= 0.0) {
        throw std::runtime_error("X distance must be greater than 0 nm");
    }

    // init spectrometer with t_measure and external trigger, then run the row-corrected loop with the calculated parameters
    cam.setExposureTime(t_measure / 1000.0); // set exposure time in seconds
    cam.setTriggerMode(0); // set trigger mode internal

    const double durationMs = xDistanceNm / velocityNmPerS; // x / (nm/ms) = ms
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
    currentpos[0] = stage.getPos("1")*1000; currentpos[1] = stage.getPos("2")*1000; currentpos[2] = stage.getPos("3")*1000;
    // take 10 seconds to get to the start position, then wait there for 1 second before starting the test move
    stage.adda( // y factor 500? unit conversion nm to mum but use double velocity *2/1000
        std::abs((startPos[0] - currentpos[0])/10), // / 10.0, 
        std::abs((startPos[1] - currentpos[1])/10), // / 10.0,
        std::abs((startPos[2] - currentpos[2])/10) // / 10.0
    );    
    stage.moveto(startPos[0], startPos[1], startPos[2]);
    std::this_thread::sleep_for(std::chrono::seconds(11));

    // check if star position within tolerance of 20 nm, if not again set velocity and move to start position
    currentpos[0] = stage.getPos("1")*1000; currentpos[1] = stage.getPos("2")*1000; currentpos[2] = stage.getPos("3")*1000;
    if (std::abs(currentpos[0] - startPos[0]) > 20.0 || std::abs(currentpos[1] - startPos[1]) > 20.0 || std::abs(currentpos[2] - startPos[2]) > 20.0) {
        // log this
        std::cerr << "Start position is out of tolerance. Retry approach 2nd time. " << std::endl;

        stage.adda(
            std::abs((startPos[0] - currentpos[0])/10), // / 10.0,
            std::abs((startPos[1] - currentpos[1])/10), // / 10.0,
            std::abs((startPos[2] - currentpos[2])/10) // / 10.0
        );
        stage.moveto(startPos[0], startPos[1], startPos[2]);
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    //stage.adda(std::abs(velocityNmPerS), 0.0, 0.0); // will be set later
    // build name of the rowcorrected_log file with timestamp: rowcorrected_log_YYYYMMDD_HHMMSS.csv
    const auto t = std::time(nullptr);
    const auto tm = *std::localtime(&t);
    std::ostringstream oss;
    std::string filename = "build/rowcorrected_log_" + std::to_string(tm.tm_year + 1900) +
        std::to_string(tm.tm_mon + 1) +
        std::to_string(tm.tm_mday) + "_" +
        std::to_string(tm.tm_hour) +
        std::to_string(tm.tm_min) +
        std::to_string(tm.tm_sec) + ".csv";
    //oss << filename;
    oss << filename;//_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".csv";

    // before start motion, set velocity to 0
    stage.adda(0.0, 0.0, 0.0);

    double measurementtime_ms = t_measure; // measurement time in ms, passed as parameter

    // debugging: print input args for runRowCorrectedLoop
    
    std::cout << "Running row-corrected loop with parameters:" << std::endl;
    std::cout << "  startPos: (" << startPos[0] << ", " << startPos[1] << ", " << startPos[2] << ")" << std::endl;
    std::cout << "  targetPos: (" << targetPos[0] << ", " << targetPos[1] << ", " << targetPos[2] << ")" << std::endl;
    std::cout << "  stepsize_nm: " << stepsize_nm << std::endl;
    std::cout << "  measurementtime_ms: " << measurementtime_ms << std::endl;
    std::cout << "  durationMs: " << durationMs << std::endl;
    std::cout << "  logImportant: " << (logImportant ? "true" : "false") << std::endl;

    // print current position of the stage
    std::array<double, 3> currentStagePos = stage.qpos();
    std::cout << "Current stage position: (" << currentStagePos[0] << ", " << currentStagePos[1] << ", " << currentStagePos[2] << ")" << std::endl;

    runRowCorrectedLoop(stage, cam, startPos, targetPos, stepsize_nm, measurementtime_ms, durationMs, logImportant, oss.str());
}

void RasterScan::runRowCorrected(PIStageProxy& stage,
                                 AndorCameraProxy& cam,
                                 double durationS,
                                 double xDistanceNm, 
                                 double stepsize_nm,
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

    stage.adda(std::abs(xDistanceNm) / durationS, 0.0, 0.0);

    int measurementtime_ms = durationS;

    runRowCorrectedLoop(stage, cam, startPos, targetPos, stepsize_nm, measurementtime_ms, durationS, logImportant, logPath);
}

void RasterScan::runAreaScan(
    ScanConfig& cfg,
    PIStageProxy& stage,
    AndorCameraProxy& cam,
    bool logImportant,
    const std::string& logPathPrefix,
    const std::string& outputCubePath,
    double durationSPerLine,
    double xDistanceNm,
    double yDistanceNm,
    double zpositionNm,
    double rowdistanceNm
    ){
    // Validate config and pre-calculate nX / nY
    if (durationSPerLine <= 0.0) {
        throw std::runtime_error("Duration per line must be greater than 0 seconds");
    }
    if (xDistanceNm <= 0.0) {
        throw std::runtime_error("X distance must be greater than 0 nm");
    }
    if (yDistanceNm < 0.0) {
        throw std::runtime_error("Y distance must be greater than or equal to 0 nm");
    }
    if (rowdistanceNm <= 0.0) {
        throw std::runtime_error("Row distance must be greater than 0 nm");
    }
    const int nX = static_cast<int>(std::round(xDistanceNm / cfg.xStep)) + 1;
    const int nY = static_cast<int>(std::round(yDistanceNm / rowdistanceNm)) + 1;
    if (nX <= 0 || nY <= 0) {
        throw std::runtime_error("Calculated scan dimensions are invalid");
    }
    AppLogger::instance().info("Area scan configuration: nX=" + std::to_string(nX) + ", nY=" + std::to_string(nY));

    // Loop over lines, running runRowCorrected for each line with the appropriate start/stop positions and durations
    std::array<double, 3> lineStartPos = {{cfg.xStart, cfg.yStart, zpositionNm}};
    std::array<double, 3> lineTargetPos = {{cfg.xStop, cfg.yStart + 10, zpositionNm}};
    for (int iy = 0; iy < nY; ++iy) {
        const double yOffset = iy * rowdistanceNm;
        if (iy %2 == 0) { // switch direction every other line to avoid long flyback times
            lineStartPos = {{cfg.xStart, cfg.yStart + yOffset, zpositionNm}};
            lineTargetPos = {{cfg.xStop, cfg.yStart + yOffset, zpositionNm}};
        }
        else {
            lineStartPos = {{cfg.xStop, cfg.yStart + yOffset, zpositionNm}};
            lineTargetPos = {{cfg.xStart, cfg.yStart + yOffset, zpositionNm}};
        }

        const std::string logPath = logImportant ? (logPathPrefix + std::to_string(iy) + ".csv") : "";

        // move stage on Start position and wait for it to settle before starting the row-corrected loop
        stage.moveto(lineStartPos[0], lineStartPos[1], lineStartPos[2]);
        stage.waitOnTarget("1");
        stage.waitOnTarget("2");
        stage.waitOnTarget("3");

        double stepsize_nm = 10.0; // Example value, replace with actual step size
        int measurementtime_ms = 150; // Example value, replace with actual measurement time

        runRowCorrectedLoop(stage, cam, lineStartPos, lineTargetPos, stepsize_nm, measurementtime_ms, durationSPerLine, logImportant, logPath);
    }

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
