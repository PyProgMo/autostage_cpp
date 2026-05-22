// PIStage.cpp
#include "PIStage.h"
#include <vector>
#include <iostream>
#include <stdexcept>

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

void PIStage::moveAbs(const char* axis, double position) {
    if (!pMOV(id_, axis, &position)) checkError();
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
    // CTO takes arrays of (triggerline, paramID, value) triples
    // paramID: 2=Axis, 1=TrigMode, 3=StepSize, 4=StartPos, 5=StopPos, 6=PulseWidth
    const int    lines[]  = { channel, channel, channel, channel, channel, channel };
    const int    params[] = {       2,        1,        3,        4,        5,        6 };

    // Param 2 (axis) needs special handling — axis is a string, 
    // but PI encodes X=1, Y=2, Z=3 as a double
    double axisCode = (axis[0] == 'X') ? 1.0 :
                      (axis[0] == 'Y') ? 2.0 : 3.0;
    // Set TrigMode to 1 (typical hardware trigger mode) instead of 0
    double valsFixed[] = { axisCode, 1.0, stepMM, startMM, stopMM,
                           (double)pulseWidthUs };

    if (!pCTO(id_, lines, params, valsFixed, 6)) checkError();
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
        throw std::runtime_error(std::string("PI Error: ") + msg);
    }
}

void PIStage::disconnect() {
    if (id_ >= 0) { pCloseConnection(id_); id_ = -1; }
}

PIStage::~PIStage() { disconnect(); if (hDll_) FreeLibrary(hDll_); }