// PIStage.cpp
#include "PIStage.h"
#include "Logger.h"
#include <array>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstdio>

PIStage::PIStage() {
    // Constructor
}

template<typename T>
T PIStage::loadProc(const char* name, bool required) {
    std::vector<std::string> candidates;
    candidates.emplace_back(name);
    std::string sName(name);

    if (sName.rfind("PI_", 0) == 0) {
        std::string base = sName.substr(3);
        candidates.emplace_back("E7XX_" + base);
        candidates.emplace_back("E7XX_q" + base);
        candidates.emplace_back("q" + base);
        candidates.emplace_back(base);
    } else {
        candidates.emplace_back("E7XX_" + sName);
        candidates.emplace_back("E7XX_q" + sName);
        candidates.emplace_back("q" + sName);
    }

    for (const auto& c : candidates) {
        T fp = reinterpret_cast<T>(GetProcAddress(hDll_, c.c_str()));
        if (fp) return fp;
    }

    if (required) throw std::runtime_error(std::string("Cannot find: ") + name);
    return reinterpret_cast<T>(0);
}

void PIStage::loadDLL(const std::string& dllPath) {
    hDll_ = LoadLibraryA(dllPath.c_str());
    if (!hDll_) throw std::runtime_error("Failed to load PI DLL");

    // Load all function pointers
    pConnectUSB      = loadProc<FP_ConnectUSB>      ("PI_ConnectUSB");
    pIsConnected     = loadProc<FP_IsConnected>     ("PI_IsConnected");
    pCloseConnection = loadProc<FP_CloseConnection> ("PI_CloseConnection");
    pMOV             = loadProc<FP_MOV>             ("PI_MOV");
    pSVO             = loadProc<FP_SVO>             ("PI_SVO");
    pVEL             = loadProc<FP_VEL>             ("PI_VEL");
    pGcsCommandset   = loadProc<FP_GcsCommandset>   ("PI_GcsCommandset");
    pqPOS            = loadProc<FP_qPOS>            ("PI_qPOS");
    pIsMoving        = loadProc<FP_IsMoving>        ("PI_IsMoving");
    pWTR             = loadProc<FP_WTR>             ("PI_WTR");
    pWGO             = loadProc<FP_WGO>             ("PI_WGO");
    pCTO             = loadProc<FP_CTO>             ("PI_CTO");
    pTRO             = loadProc<FP_TRO>             ("PI_TRO", false); // optional on some E-7XX DLLs
    pDRC             = loadProc<FP_DRC>             ("PI_DRC");
    pDRT             = loadProc<FP_DRT>             ("PI_DRT");
    pRTR             = loadProc<FP_RTR>             ("PI_RTR");
    pDRR             = loadProc<FP_DRR>             ("PI_DRR");
    pGetError        = loadProc<FP_GetError>        ("PI_GetError");
    pTranslateError  = loadProc<FP_TranslateError>  ("PI_TranslateError");
}

void PIStage::connect(const std::string& serialNum) {
    id_ = pConnectUSB(serialNum.c_str());
    if (id_ < 0) {
        int err = pGetError ? pGetError(id_) : 0;
        char msg[256] = {};
        if (pTranslateError) pTranslateError(err, msg, sizeof(msg));
        std::string message = std::string("PI stage connection failed: code=") +
                              std::to_string(err) + " msg=" + msg +
                              " serial=" + serialNum;
        AppLogger::instance().error(message);
        throw std::runtime_error(message);
    }
}

void PIStage::enableServo(const char* axis, bool enable) {
    std::string cmd = std::string("SVO ") + axis + " " + (enable ? "1" : "0");
    if (!pGcsCommandset(id_, cmd.c_str())) checkError();
}

void PIStage::moveAbs(const char* axis, double position) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "MOV %s %.17g", axis, position);
    if (!pGcsCommandset(id_, buf)) checkError();
}

std::array<double, 3> PIStage::qpos() {
    std::array<double, 3> positions = {0.0, 0.0, 0.0};
    if (!pqPOS(id_, "1 2 3", positions.data())) checkError();
    return positions;
}

void PIStage::moveto(double x, double y, double z) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "MOV 1 %.17g 2 %.17g 3 %.17g", x, y, z);
    if (!pGcsCommandset(id_, buf)) checkError();
}

double PIStage::getPos(const char* axis) {
    double pos = 0.0;
    if (!pqPOS(id_, axis, &pos)) checkError();
    return pos;
}

void PIStage::getPosMult(const char* axes, double* positions) {
    if (!pqPOS(id_, axes, positions)) checkError();
}

void PIStage::setVelocity(const char* axes, const double* velocities) {
    if (!pVEL(id_, axes, velocities)) checkError();
}

void PIStage::adda(double vx, double vy, double vz) {
    const double velocities[3] = {vx, vy, vz};
    if (!pVEL(id_, "1 2 3", velocities)) checkError();
}

void PIStage::runVelocitySweep(double vNominal, double xStop, double yHold, const std::vector<double>& zProfile, double xStart, double xStep) {
    // Basic boundaries check (units: micrometers)
    if (xStop > 300.0 || xStart < 0.0) throw std::runtime_error("X boundaries out of limits (0-300 um)");

    // Sweep Loop logic over ~5ms periods
    double Kp = 1.0, Ka = 0.0, Ki = 0.1;
    double C0 = 0.0;
    double C0_cap = vNominal * 0.2; // 20% cap
    
    double positions[3] = {0.0, 0.0, 0.0};
    double prev_vX = vNominal;
    double prev_X = xStart;
    DWORD prev_t = GetTickCount();

    // Make sure we are up to date before looping
    getPosMult("X Y Z", positions);

    while (positions[0] < xStop) {
        // Sleep to target ~5ms
        Sleep(5);

        DWORD current_t = GetTickCount();
        double dt = (current_t - prev_t) * 0.001;
        if (dt <= 0.0) dt = 0.005;

        // Query new positions
        getPosMult("X Y Z", positions);
        double X_k = positions[0];
        double Y_k = positions[1];

        // Velocity calc
        double vX = (X_k - prev_X) / dt;
        double aX = (vX - prev_vX) / dt;
        
        // --- X correction ---
        // 0th Order - C0 integrator
        C0 += Ki * (vNominal - vX);
        if (C0 > C0_cap) C0 = C0_cap;
        if (C0 < -C0_cap) C0 = -C0_cap;

        // 1st and 2nd
        double C1 = Kp * (vNominal - vX);
        double C2 = Ka * (0.0 - aX);

        double vCmdX = vNominal + C0 + C1 + C2;

        // --- Y hold ---
        double vCmdY = 2.0 * (yHold - Y_k); // simple P term hold
        
        // --- Z tracker ---
        double vCmdZ = 0.0;
        if (!zProfile.empty()) {
             // simplified lookup based on index
             int idx = (int)((X_k - xStart) / xStep);
             size_t profileIndex = static_cast<size_t>(idx);
             if (idx >= 0 && profileIndex + 1 < zProfile.size()) {
                 double grad = (zProfile[profileIndex + 1] - zProfile[profileIndex]) / xStep;
                 vCmdZ = grad * vX; 
             }
        }

        // Issue new commanded velocities
        double cmds[3] = {vCmdX, vCmdY, vCmdZ};
        setVelocity("X Y Z", cmds);

        // Update prevs
        prev_t = current_t;
        prev_X = X_k;
        prev_vX = vX;
    }
}

void PIStage::waitOnTarget(const char* axis, int timeoutMs) {
    auto t0 = GetTickCount64();
    BOOL moving = TRUE;
    while (moving) {
        pIsMoving(id_, axis, &moving);
        if (!moving) break;
        if ((int)(GetTickCount64() - t0) > timeoutMs)
            throw std::runtime_error("Stage timeout waiting on target");
        Sleep(1);
    }
}

void PIStage::waitForTriggerInput(int trigChannel, int timeoutMs) {
    // bIgnoreRange=FALSE: respects the configured trigger range
    if (!pWTR(id_, trigChannel, timeoutMs, FALSE)) checkError();
}

void PIStage::setWaitOnGo(const char* axis, int conditionMask) {
    if (!pWGO(id_, axis, &conditionMask)) checkError();
}

void PIStage::configureTriggerOutput(int channel, const char* axis,
                                     double startUM, double stepUM,
                                     double stopUM, int pulseWidthUs)
{
    // Prepare axis code (X=1, Y=2, Z=3)
    double axisCode = (axis[0] == 'X') ? 1.0 :
                      (axis[0] == 'Y') ? 2.0 : 3.0;

    const int lines[] = { channel, channel, channel, channel, channel, channel };

    // Enforce soft limits for our piezo nanostage: 0..300 µm
    const double SOFT_MIN = 0.0;
    const double SOFT_MAX = 300.0;
    if (startUM < SOFT_MIN) {
        AppLogger::instance().warn(std::string("PIStage: startUM below soft min, clamping: ") + std::to_string(startUM));
        startUM = SOFT_MIN;
    }
    if (stopUM > SOFT_MAX) {
        AppLogger::instance().warn(std::string("PIStage: stopUM above soft max, clamping: ") + std::to_string(stopUM));
        stopUM = SOFT_MAX;
    }

    // Try multiple parameter orderings — some firmware expects different layouts.
    const int paramsA[] = { 2, 1, 3, 4, 5, 6 };
    double valsA[] = { axisCode, 1.0, stepUM, startUM, stopUM, (double)pulseWidthUs };

    const int paramsB[] = { 1, 2, 3, 4, 5, 6 };
    double valsB[] = { 1.0, axisCode, stepUM, startUM, stopUM, (double)pulseWidthUs };

    // Candidate C: use axis as integer placed later
    const int paramsC[] = { 1, 3, 4, 5, 6, 2 };
    double valsC[] = { 1.0, stepUM, startUM, stopUM, (double)pulseWidthUs, axisCode };

    if (pCTO(id_, lines, paramsA, valsA, 6)) return;
    else {
        int err = pGetError ? pGetError(id_) : 0;
        char msg[256] = {};
        if (pTranslateError) pTranslateError(err, msg, sizeof(msg));
        AppLogger::instance().error(std::string("PIStage: CTO paramsA failed: code=") + std::to_string(err) + " msg=" + msg);
    }

    if (pCTO(id_, lines, paramsB, valsB, 6)) return;
    else {
        int err = pGetError ? pGetError(id_) : 0;
        char msg[256] = {};
        if (pTranslateError) pTranslateError(err, msg, sizeof(msg));
        AppLogger::instance().error(std::string("PIStage: CTO paramsB failed: code=") + std::to_string(err) + " msg=" + msg);
    }

    if (pCTO(id_, lines, paramsC, valsC, 6)) return;
    else {
        int err = pGetError ? pGetError(id_) : 0;
        char msg[256] = {};
        if (pTranslateError) pTranslateError(err, msg, sizeof(msg));
        AppLogger::instance().error(std::string("PIStage: CTO paramsC failed: code=") + std::to_string(err) + " msg=" + msg);
    }

    // E712-specific mapping: param order with axis label (2), mode (1=8 Position Distance), step (3), min (5), max (6), polarity (7)
    const int paramsE[] = { 2, 1, 3, 5, 6, 7 };
    // compute thresholds — make sure they are within soft limits
    double minThresh = startUM;
    double maxThresh = stopUM;
    if (minThresh < SOFT_MIN) minThresh = SOFT_MIN;
    if (maxThresh > SOFT_MAX) maxThresh = SOFT_MAX;
    double valsE[] = { axisCode, 8.0, stepUM, minThresh, maxThresh, 1.0 };

    if (pCTO(id_, lines, paramsE, valsE, 6)) return;
    else {
        int err = pGetError ? pGetError(id_) : 0;
        char msg[256] = {};
        if (pTranslateError) pTranslateError(err, msg, sizeof(msg));
        AppLogger::instance().error(std::string("PIStage: CTO E712 mapping failed: code=") + std::to_string(err) + " msg=" + msg);
    }

    // None succeeded — throw with PI error info
    checkError();
}

void PIStage::enableTriggerOutput(int channel, bool enable) {
    BOOL en = enable ? TRUE : FALSE;

    // First try CTO param 9 (TriggerOutActive) which some E-7XX firmwares use
    if (pCTO) {
        const int lines[] = { channel };
        const int params[] = { 9 }; // TriggerOutActive
        double vals[] = { enable ? 1.0 : 0.0 };
        if (pCTO(id_, lines, params, vals, 1)) return;
        else {
            int err = pGetError ? pGetError(id_) : 0;
            char msg[256] = {};
            if (pTranslateError) pTranslateError(err, msg, sizeof(msg));
            AppLogger::instance().error(std::string("PIStage: CTO set active failed: code=") + std::to_string(err) + " msg=" + msg);
        }
    }

    // Fall back to TRO if available
    if (pTRO) {
        if (!pTRO(id_, &channel, &en, 1)) checkError();
        return;
    }

    // Neither method succeeded
    checkError();
}

void PIStage::setupDataRecorder(int table, const char* source, int option) {
    const char* sources[] = { source };
    if (!pDRC(id_, &table, sources, &option, 1)) checkError();
}

void PIStage::setRecordTrigger(int triggerSource, int axis, double thresholdUM) {
    if (!pDRT(id_, triggerSource, axis, thresholdUM)) checkError();
}

void PIStage::setRecordRate(int cycleDiv) {
    if (!pRTR(id_, cycleDiv)) checkError();
}

std::vector<double> PIStage::readRecorder(int startOffset, int numValues,
                                           const int* tables, int nTables) {
    std::vector<double> buf(numValues * nTables);
    if (!pDRR(id_, buf.data(), startOffset, numValues, tables, nTables))
        checkError();
    return buf;
}

void PIStage::checkError() {
    int err = pGetError(id_);
    if (err != 0) {
        char msg[256] = {};
        pTranslateError(err, msg, sizeof(msg));
        std::string message = std::string("PI Error: code=") + std::to_string(err) + " message=" + msg;
        AppLogger::instance().error(message);
        throw std::runtime_error(message);
    }
}

void PIStage::disconnect() {
    if (id_ >= 0) { pCloseConnection(id_); id_ = -1; }
}

PIStage::~PIStage() { disconnect(); if (hDll_) FreeLibrary(hDll_); }