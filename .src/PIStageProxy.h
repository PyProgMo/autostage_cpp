// PIStageProxy.h
#pragma once
#include <Windows.h>
#include <string>
#include <stdexcept>
#include <vector>

struct IpcMessage;

class PIStageProxy {
public:
    PIStageProxy();
    ~PIStageProxy();

    void loadDLL(const std::string& dllPath);
    void connect(const std::string& serialNum);
    void disconnect();

    void moveAbs(const char* axis, double position);
    double getPos(const char* axis);
    void waitOnTarget(const char* axis, int timeoutMs = 10000);

    // Advanced motion
    void runVelocitySweep(double vNominal, double xStop, double yHold, double xStart, double xStep);
    void uploadZProfile(const std::vector<double>& zProfile);

    // Trigger output
    void configureTriggerOutput(int channel, const char* axis,
                                double startMM, double stepMM,
                                double stopMM,  int pulseWidthUs);
    void enableTriggerOutput (int channel, bool enable);

    // Trigger input wait
    void waitForTriggerInput(int trigChannel, int timeoutMs = 5000);

    // WGO: gate next move on a condition
    void setWaitOnGo(const char* axis, int conditionMask);

    // Data recorder
    void setupDataRecorder(int table, const char* source, int option);
    void setRecordTrigger(int triggerSource, int axis = 0, double thresholdMM = 0.0);
    void setRecordRate(int cycleDiv);
    std::vector<double> readRecorder(int startOffset, int numValues,
                                     const int* tables, int nTables);

    void checkError();

private:
    void sendCommand(const struct IpcMessage& msg, struct IpcMessage& response);
    HANDLE hPipe_ = INVALID_HANDLE_VALUE;
};
