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
                         const std::string& logPath) {
    const double deltaX = targetPos[0] - startPos[0];
    const double deltaY = targetPos[1] - startPos[1];
    const double deltaZ = targetPos[2] - startPos[2];
    const double refVx = deltaX / durationS;
    const double refVy = deltaY / durationS;
    const double refVz = deltaZ / durationS;
    const std::array<double, 3> referenceVelocity = {{refVx, refVy, refVz}};

    std::ofstream rowLog;
    if (logImportant) {
        // Open in append mode. If the parent directory does not exist,
        // attempt to create it and retry once. After opening, seek to the
        // end and use tellp()==0 to determine whether we should write the header.
        rowLog.open(logPath.c_str(), std::ios::out | std::ios::app);
        if (!rowLog.is_open()) {
            ensureParentDirExists(logPath);
            rowLog.clear();
            rowLog.open(logPath.c_str(), std::ios::out | std::ios::app);
        }
        if (!rowLog.is_open()) {
            throw std::runtime_error("Failed to open row-corrected log file: " + logPath);
        }
        // If the file is empty (just created), write the header.
        rowLog.seekp(0, std::ios::end);
        if (rowLog.tellp() == 0) {
            writeRowCorrectedCsvHeader(rowLog);
        }
    }

    std::array<double, 3> previousActual = startPos;
    std::array<double, 3> previousVelocity = referenceVelocity;
    std::array<double, 3> filteredActual = startPos;

    const auto startTime = std::chrono::steady_clock::now();
    auto lastTickTime = startTime;
    auto nextTickTime = startTime + std::chrono::milliseconds(static_cast<int>(kStageTestTickMs));

    AppLogger::instance().info(std::string("rowcorrected: start x0=") + std::to_string(startPos[0]) +
                               " y0=" + std::to_string(startPos[1]) +
                               " z0=" + std::to_string(startPos[2]) +
                               " dx_nm=" + std::to_string(deltaX) +
                               " duration_s=" + std::to_string(durationS) +
                               (logImportant ? " log=on" : " log=off"));

    while (true) {
        std::this_thread::sleep_until(nextTickTime);
        nextTickTime += std::chrono::milliseconds(static_cast<int>(kStageTestTickMs));

        const auto now = std::chrono::steady_clock::now();
        const double elapsedS = std::chrono::duration<double>(now - startTime).count();
        const double tickS = std::max(std::chrono::duration<double>(now - lastTickTime).count(), kStageTestTickS);
        const double progress = std::min(elapsedS / durationS, 1.0);
        const double remainingS = std::max(durationS - elapsedS, kStageTestTickS);

        const std::array<double, 3> actual = stage.qpos();

        // Smooth the 10 ms qpos uncertainty before differentiating it.
        const double alpha = 0.35;
        for (int axis = 0; axis < 3; ++axis) {
            filteredActual[axis] = alpha * actual[axis] + (1.0 - alpha) * filteredActual[axis];
        }

        RowCorrectedLogRow row;
        row.timeS = elapsedS;
        row.elapsedS = elapsedS;
        row.remainingS = remainingS;
        row.referenceNm[0] = startPos[0] + deltaX * progress;
        row.referenceNm[1] = startPos[1] + deltaY * progress;
        row.referenceNm[2] = startPos[2] + deltaZ * progress;
        row.actualNm = actual;
        row.errorNm[0] = row.referenceNm[0] - filteredActual[0];
        row.errorNm[1] = row.referenceNm[1] - filteredActual[1];
        row.errorNm[2] = row.referenceNm[2] - filteredActual[2];

        for (int axis = 0; axis < 3; ++axis) {
            row.actualVelocityNmPerS[axis] = (filteredActual[axis] - previousActual[axis]) / tickS;
            row.actualAccelerationNmPerS2[axis] = (row.actualVelocityNmPerS[axis] - previousVelocity[axis]) / tickS;
        }

        // 1st order correction: position error -> velocity correction.
        const double kp = 0.60;
        // 2nd order correction: velocity error -> velocity correction.
        const double kv = 0.80;
        // 3rd order correction: acceleration error -> velocity correction.
        const double ka = 0.20;

        const std::array<double, 3> referenceAcceleration = {{0.0, 0.0, 0.0}};

        for (int axis = 0; axis < 3; ++axis) {
            const double positionCorrection = kp * row.errorNm[axis] / remainingS;
            const double velocityError = referenceVelocity[axis] - row.actualVelocityNmPerS[axis];
            const double velocityCorrection = kv * velocityError;
            const double accelerationError = referenceAcceleration[axis] - row.actualAccelerationNmPerS2[axis];
            const double accelerationCorrection = ka * accelerationError * tickS;

            row.correction1NmPerS[axis] = positionCorrection;
            row.correction2NmPerS[axis] = velocityCorrection;
            row.correction3NmPerS[axis] = accelerationCorrection;

            row.commandedVelocityNmPerS[axis] = referenceVelocity[axis] +
                                                row.correction1NmPerS[axis] +
                                                row.correction2NmPerS[axis] +
                                                row.correction3NmPerS[axis];
            row.commandedVelocityNmPerS[axis] = clampSymmetric(row.commandedVelocityNmPerS[axis], kRowCorrectedMaxCommandNmPerS);
        }

        row.boundaryHit =
            actual[0] < kStageTestMinNm || actual[0] > kStageTestMaxNm ||
            actual[1] < kStageTestMinNm || actual[1] > kStageTestMaxNm ||
            actual[2] < kStageTestMinNm || actual[2] > kStageTestMaxNm;

        const std::string line = formatRowCorrectedLine(row);
        std::cout << line << "\n";
        AppLogger::instance().info(line);
        if (logImportant) {
            writeRowCorrectedCsvRow(rowLog, row);
        }

        if (row.boundaryHit) {
            AppLogger::instance().error("rowcorrected: boundary exceeded, halting stage");
            stage.halt();
            throw std::runtime_error("rowcorrected aborted: stage left safe bounds");
        }

        // Dummy CTO trigger for future spectral acquisition during motion.
        // if (std::abs(actual[0] - lastTriggerXNm) >= triggerStepNm) {
        //     stage.configureTriggerOutput(1, "X", actual[0], triggerStepNm, targetPos[0], 50);
        //     stage.enableTriggerOutput(1, true);
        // }

        if (elapsedS >= durationS) {
            break;
        }

        stage.adda(row.commandedVelocityNmPerS[0], 0.0, 0.0);

        previousVelocity = row.actualVelocityNmPerS;
        previousActual = filteredActual;
        lastTickTime = now;
    }

    stage.adda(0.0, 0.0, 0.0);
    AppLogger::instance().info("rowcorrected: completed");
}
} // namespace

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

    stage.moveto(startPos[0], startPos[1], startPos[2]);
    stage.waitOnTarget("1");
    stage.waitOnTarget("2");
    stage.waitOnTarget("3");

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