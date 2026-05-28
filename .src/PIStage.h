// PIStage.h
#pragma once
#include <Windows.h>
#include <string>
#include <stdexcept>
#include <vector>

// ── PI GCS2 function pointer typedefs ─────────────────────────────────────

typedef int  (__stdcall *FP_ConnectUSB)       (const char* sSerialNum);
typedef int  (__stdcall *FP_ConnectRS232)     (int port, int baud);
typedef BOOL (__stdcall *FP_IsConnected)      (int id);
typedef BOOL (__stdcall *FP_CloseConnection)  (int id);

typedef BOOL (__stdcall *FP_MOV)  (int id, const char* axes, const double* values);
typedef BOOL (__stdcall *FP_SVO)  (int id, const char* axes, const BOOL* enabled);
typedef BOOL (__stdcall *FP_VEL)  (int id, const char* axes, const double* velocities);
typedef BOOL (__stdcall *FP_GcsCommandset) (int id, const char* command);
typedef BOOL (__stdcall *FP_qPOS) (int id, const char* axes, double* values);
typedef BOOL (__stdcall *FP_IsMoving)(int id, const char* axes, BOOL* moving);
typedef BOOL (__stdcall *FP_WTR)  (int id, int nTriggerInput, int nTimeout_ms, BOOL bIgnoreRange);

typedef BOOL (__stdcall *FP_WGO)  (int id, const char* axes, const int* conditions);

// CTO: SetTriggerOutput parameters
typedef BOOL (__stdcall *FP_CTO)  (int id,
                                  const int*    triggerlines,
                                  const int*    params,
                                  const double* values,
                                  int           nItems);
typedef BOOL (__stdcall *FP_TRO)  (int id,
                                  const int*  triggerlines,
                                  const BOOL* enabled,
                                  int         nItems);

// Data Recorder
typedef BOOL (__stdcall *FP_DRC)  (int id,
                                  const int*    tables,
                                  const char**  sources,
                                  const int*    options,
                                  int           nItems);
typedef BOOL (__stdcall *FP_DRT)  (int id, int triggerSource, int axis, double value);
typedef BOOL (__stdcall *FP_RTR)  (int id, int recordRate);
typedef BOOL (__stdcall *FP_DRR)  (int id,
                                  double*     data,
                                  int         startOffset,
                                  int         numValues,
                                  const int*  tables,
                                  int         nTables);
typedef int  (__stdcall *FP_GetError)   (int id);
typedef BOOL (__stdcall *FP_TranslateError)(int err, char* buf, int bufsize);

// ── PIStage class ──────────────────────────────────────────────────────────
class PIStage {
public:
    PIStage();
    ~PIStage();

    void loadDLL(const std::string& dllPath);
    void connect(const std::string& serialNum);
    void disconnect();

    void enableServo(const char* axis, bool enable);
    void moveAbs(const char* axis, double position);
    void setVelocity(const char* axes, const double* velocities);
    double getPos(const char* axis);
    void getPosMult(const char* axes, double* positions);
    void waitOnTarget(const char* axis, int timeoutMs = 10000);
    
    // Velocity loop execution
    void runVelocitySweep(double vNominal, double xStop, double yHold, const std::vector<double>& zProfile, double xStart, double xStep);

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
    HMODULE  hDll_  = nullptr;
    int      id_    = -1;

    // Function pointers
    FP_ConnectUSB       pConnectUSB       = nullptr;
    FP_IsConnected      pIsConnected      = nullptr;
    FP_CloseConnection  pCloseConnection  = nullptr;
    FP_MOV              pMOV              = nullptr;
    FP_SVO              pSVO              = nullptr;
    FP_VEL              pVEL              = nullptr;
    FP_GcsCommandset    pGcsCommandset    = nullptr;
    FP_qPOS             pqPOS             = nullptr;
    FP_IsMoving         pIsMoving         = nullptr;
    FP_WTR              pWTR              = nullptr;
    FP_WGO              pWGO              = nullptr;
    FP_CTO              pCTO              = nullptr;
    FP_TRO              pTRO              = nullptr;
    FP_DRC              pDRC              = nullptr;
    FP_DRT              pDRT              = nullptr;
    FP_RTR              pRTR              = nullptr;
    FP_DRR              pDRR              = nullptr;
    FP_GetError         pGetError         = nullptr;
    FP_TranslateError   pTranslateError   = nullptr;

    template<typename T>
    T loadProc(const char* name, bool required = true);
};