// IpcStructs.h
#pragma once
#include <stdint.h>

#define PIPE_NAME "\\\\.\\pipe\\PIStageIpcPipe"
#define ANDOR_PIPE_NAME "\\\\.\\pipe\\AndorIpcPipe"

enum class IpcCommand : int32_t {
    Connect = 1,
    Disconnect,
    MoveAbs,
    GetPos,
    WaitOnTarget,
    ConfigTriggerOut,
    EnableTriggerOut,
    WaitTriggerIn,
    SetWaitOnGo,
    SetupDataRecorder,
    SetRecordTrigger,
    SetRecordRate,
    ReadRecorder,
    LoadDLL,
    ExitServer,
    
    // Scan Commands
    RunVelocitySweep,
    UploadZProfile,
    
    // Andor commands
    AndorLoadDLL,
    AndorInitialize,
    AndorSetReadMode,
    AndorSetAcquisitionMode,
    AndorSetExposureTime,
    AndorSetTriggerMode,
    AndorSetImage,
    AndorStartAcquisition,
    AndorAbortAcquisition,
    AndorWaitForAcquisition,
    AndorGetAcquiredData16,
    AndorGetStatus,
    AndorShutDown,
    AndorSetKineticCycleTime,
    AndorSetNumberKinetics,
    AndorGetImages16,
    AndorConfigureSpectral,
    AndorConfigureFVBKinetic
};

#pragma pack(push, 1)

struct IpcMessage {
    IpcCommand command;
    int32_t    status;      // 0 = OK, non-zero = error
    char       strArg[256]; // e.g. serial num, dll name, or axis name
    
    int32_t    iArgs[8];
    double     dArgs[4];
    
    int32_t    dataSize;    // For variable-length data to follow
};

#pragma pack(pop)
