// RasterScanOneRow.cpp
#include "RasterScan.h"
#include "Logger.h"
#include <array>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
constexpr double kStageTestMinNm = 0.0;
constexpr double kStageTestMaxNm = 300000.0;
constexpr double kStageTestStartNm = 1000.0;
constexpr double kStageTestTickMs = 10.0;

std::string formatOneRowTestLine(double velocityNmPerS,
                                 double targetXNm,
                                 const std::array<double, 3>& posNm,
                                 double deltaNm,
                                 double measuredVelocityNmPerS,
                                 double queryLatencyMs,
                                 double iterationMs,
                                 double workMs,
                                 bool boundaryHit) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(3);
    oss << "rowtest"
        << " x=" << posNm[0]
        << " y=" << posNm[1]
        << " z=" << posNm[2]
        << " dx_nm=" << deltaNm
        << " v_cmd_nm_s=" << velocityNmPerS
        << " v_meas_nm_s=" << measuredVelocityNmPerS
        << " q_ms=" << queryLatencyMs
        << " it_ms=" << iterationMs
        << " work_ms=" << workMs
        << " target_x_nm=" << targetXNm
        << " boundary=" << (boundaryHit ? "hit" : "ok");
    return oss.str();
}
} // namespace

void RasterScan::runOneRowTest(PIStageProxy& stage, double velocityNmPerS, double xDistanceNm) {
    if (velocityNmPerS < 10.0 || velocityNmPerS > 10000.0) {
        throw std::runtime_error("Velocity must be between 10 and 10000 nm/s");
    }
    if (xDistanceNm <= 0.0) {
        throw std::runtime_error("X distance must be greater than 0 nm");
    }

    const double startX = kStageTestStartNm;
    const double startY = kStageTestStartNm;
    const double startZ = kStageTestStartNm;
    const double targetX = startX + xDistanceNm;

    if (startX < kStageTestMinNm || startX > kStageTestMaxNm ||
        startY < kStageTestMinNm || startY > kStageTestMaxNm ||
        startZ < kStageTestMinNm || startZ > kStageTestMaxNm) {
        throw std::runtime_error("Velocity test start position is out of bounds");
    }
    if (targetX < kStageTestMinNm || targetX > kStageTestMaxNm) {
        throw std::runtime_error("Velocity test target X is out of bounds");
    }

    stage.moveto(startX, startY, startZ);
    stage.waitOnTarget("X");
    stage.waitOnTarget("Y");
    stage.waitOnTarget("Z");

    stage.adda(velocityNmPerS, 0.0, 0.0);
    stage.moveto(targetX, startY, startZ);

    std::array<double, 3> previousPos = stage.qpos();
    const auto testStart = std::chrono::steady_clock::now();
    auto lastLoopStart = testStart;
    auto nextTick = testStart + std::chrono::milliseconds(static_cast<int>(kStageTestTickMs));

    const bool movingPositive = (targetX >= previousPos[0]);

    while (true) {
        std::this_thread::sleep_until(nextTick);
        nextTick += std::chrono::milliseconds(static_cast<int>(kStageTestTickMs));

        const auto loopStart = std::chrono::steady_clock::now();
        const double iterationMs = std::chrono::duration<double, std::milli>(loopStart - lastLoopStart).count();

        const auto queryStart = std::chrono::steady_clock::now();
        const std::array<double, 3> pos = stage.qpos();
        const auto queryEnd = std::chrono::steady_clock::now();
        const double queryLatencyMs = std::chrono::duration<double, std::milli>(queryEnd - queryStart).count();
        const double workMs = std::chrono::duration<double, std::milli>(queryEnd - loopStart).count();

        const double deltaNm = pos[0] - previousPos[0];
        const double measuredVelocityNmPerS = (iterationMs > 0.0) ? (deltaNm * 1000.0 / iterationMs) : 0.0;

        const bool boundaryHit =
            pos[0] < kStageTestMinNm || pos[0] > kStageTestMaxNm ||
            pos[1] < kStageTestMinNm || pos[1] > kStageTestMaxNm ||
            pos[2] < kStageTestMinNm || pos[2] > kStageTestMaxNm;

        const std::string line = formatOneRowTestLine(
            velocityNmPerS,
            targetX,
            pos,
            deltaNm,
            measuredVelocityNmPerS,
            queryLatencyMs,
            iterationMs,
            workMs,
            boundaryHit);

        std::cout << line << "\n";
        AppLogger::instance().info(line);

        if (boundaryHit) {
            AppLogger::instance().error("rowtest: boundary exceeded, halting stage");
            stage.halt();
            throw std::runtime_error("rowtest aborted: stage left safe bounds");
        }

        if ((movingPositive && pos[0] >= targetX) || (!movingPositive && pos[0] <= targetX)) {
            AppLogger::instance().info("rowtest: target reached");
            break;
        }

        previousPos = pos;
        lastLoopStart = loopStart;
    }
}
 