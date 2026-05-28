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
    // All position/step parameters passed here are in nanometres (nm) and will be converted to micrometers by the proxy
    void configureTriggerOutput(int channel, const char* axis,
                                double start_nm, double step_nm,
                                double stop_nm,  int pulseWidthUs);
    void enableTriggerOutput (int channel, bool enable);

    // Trigger input wait
    void waitForTriggerInput(int trigChannel, int timeoutMs = 5000);

    // WGO: gate next move on a condition
    void setWaitOnGo(const char* axis, int conditionMask);

    // Data recorder
    void setupDataRecorder(int table, const char* source, int option);
    // threshold is in nanometres (nm)
    void setRecordTrigger(int triggerSource, int axis = 0, double threshold_nm = 0.0);
    void setRecordRate(int cycleDiv);
    std::vector<double> readRecorder(int startOffset, int numValues,
                                     const int* tables, int nTables);

    void checkError();

private:
    void sendCommand(const struct IpcMessage& msg, struct IpcMessage& response);
    HANDLE hPipe_ = INVALID_HANDLE_VALUE;
};
