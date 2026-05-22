// PIStage.cpp
#include "PIStage.h"
#include "Logger.h"
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstdio>

PIStage::PIStage() {
    // Constructor
}

template<typename T>
T PIStage::loadProc(const char* name) {
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

    throw std::runtime_error(std::string("Cannot find: ") + name);
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
    pGcsCommandset   = loadProc<FP_GcsCommandset>   ("PI_GcsCommandset");
    pqPOS            = loadProc<FP_qPOS>            ("PI_qPOS");
    pIsMoving        = loadProc<FP_IsMoving>        ("PI_IsMoving");
    pWTR             = loadProc<FP_WTR>             ("PI_WTR");
    pWGO             = loadProc<FP_WGO>             ("PI_WGO");
    pCTO             = loadProc<FP_CTO>             ("PI_CTO");
    pTRO             = loadProc<FP_TRO>             ("PI_TRO");
    pDRC             = loadProc<FP_DRC>             ("PI_DRC");
    pDRT             = loadProc<FP_DRT>             ("PI_DRT");
    pRTR             = loadProc<FP_RTR>             ("PI_RTR");
    pDRR             = loadProc<FP_DRR>             ("PI_DRR");
    pGetError        = loadProc<FP_GetError>        ("PI_GetError");
    pTranslateError  = loadProc<FP_TranslateError>  ("PI_TranslateError");
}

void PIStage::connect(const std::string& serialNum) {
    id_ = pConnectUSB(serialNum.c_str());
    if (id_ < 0) throw std::runtime_error("PI stage connection failed");
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

double PIStage::getPos(const char* axis) {
    double pos = 0.0;
    if (!pqPOS(id_, axis, &pos)) checkError();
    return pos;
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
                                     double startMM, double stepMM,
                                     double stopMM, int pulseWidthUs)
{
    // Prepare axis code (X=1, Y=2, Z=3)
    double axisCode = (axis[0] == 'X') ? 1.0 :
                      (axis[0] == 'Y') ? 2.0 : 3.0;

    const int lines[] = { channel, channel, channel, channel, channel, channel };

    // Try multiple parameter orderings — some firmware expects different layouts.
    const int paramsA[] = { 2, 1, 3, 4, 5, 6 };
    double valsA[] = { axisCode, 1.0, stepMM, startMM, stopMM, (double)pulseWidthUs };

    const int paramsB[] = { 1, 2, 3, 4, 5, 6 };
    double valsB[] = { 1.0, axisCode, stepMM, startMM, stopMM, (double)pulseWidthUs };

    // Candidate C: use axis as integer placed later
    const int paramsC[] = { 1, 3, 4, 5, 6, 2 };
    double valsC[] = { 1.0, stepMM, startMM, stopMM, (double)pulseWidthUs, axisCode };

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

    // None succeeded — throw with PI error info
    checkError();
}

void PIStage::enableTriggerOutput(int channel, bool enable) {
    BOOL en = enable ? TRUE : FALSE;
    if (!pTRO(id_, &channel, &en, 1)) checkError();
}

void PIStage::setupDataRecorder(int table, const char* source, int option) {
    const char* sources[] = { source };
    if (!pDRC(id_, &table, sources, &option, 1)) checkError();
}

void PIStage::setRecordTrigger(int triggerSource, int axis, double thresholdMM) {
    if (!pDRT(id_, triggerSource, axis, thresholdMM)) checkError();
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