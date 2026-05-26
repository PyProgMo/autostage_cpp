#pragma once

#include "IpcStructs.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

class AppLogger {
public:
    static AppLogger& instance();

    void setPaths(const std::string& generalPath, const std::string& errorPath);
    void info(const std::string& message);
    void error(const std::string& message);

private:
    AppLogger() = default;

    void writeLocked(std::ofstream& file, const std::string& level, const std::string& message);
    void ensureOpenLocked();
    static std::string timestamp();

    std::mutex mutex_;
    std::ofstream generalFile_;
    std::ofstream errorFile_;
    std::string generalPath_ = "scan_log.txt";
    std::string errorPath_ = "error_log.txt";
    bool opened_ = false;
};

inline const char* ipcCommandName(IpcCommand command) {
    switch (command) {
    case IpcCommand::Connect: return "Connect";
    case IpcCommand::Disconnect: return "Disconnect";
    case IpcCommand::MoveAbs: return "MoveAbs";
    case IpcCommand::GetPos: return "GetPos";
    case IpcCommand::WaitOnTarget: return "WaitOnTarget";
    case IpcCommand::ConfigTriggerOut: return "ConfigTriggerOut";
    case IpcCommand::EnableTriggerOut: return "EnableTriggerOut";
    case IpcCommand::WaitTriggerIn: return "WaitTriggerIn";
    case IpcCommand::SetWaitOnGo: return "SetWaitOnGo";
    case IpcCommand::SetupDataRecorder: return "SetupDataRecorder";
    case IpcCommand::SetRecordTrigger: return "SetRecordTrigger";
    case IpcCommand::SetRecordRate: return "SetRecordRate";
    case IpcCommand::ReadRecorder: return "ReadRecorder";
    case IpcCommand::LoadDLL: return "LoadDLL";
    case IpcCommand::ExitServer: return "ExitServer";
        
        case IpcCommand::AndorLoadDLL: return "AndorLoadDLL";
        case IpcCommand::AndorInitialize: return "AndorInitialize";
        case IpcCommand::AndorSetReadMode: return "AndorSetReadMode";
        case IpcCommand::AndorSetAcquisitionMode: return "AndorSetAcquisitionMode";
        case IpcCommand::AndorSetExposureTime: return "AndorSetExposureTime";
        case IpcCommand::AndorSetTriggerMode: return "AndorSetTriggerMode";
        case IpcCommand::AndorSetImage: return "AndorSetImage";
        case IpcCommand::AndorStartAcquisition: return "AndorStartAcquisition";
        case IpcCommand::AndorAbortAcquisition: return "AndorAbortAcquisition";
        case IpcCommand::AndorWaitForAcquisition: return "AndorWaitForAcquisition";
        case IpcCommand::AndorGetAcquiredData16: return "AndorGetAcquiredData16";
        case IpcCommand::AndorGetStatus: return "AndorGetStatus";
        case IpcCommand::AndorShutDown: return "AndorShutDown";
    case IpcCommand::AndorSetKineticCycleTime: return "AndorSetKineticCycleTime";
    case IpcCommand::AndorSetNumberKinetics: return "AndorSetNumberKinetics";
    case IpcCommand::AndorGetImages16: return "AndorGetImages16";
    default: return "Unknown";
    }
}

inline std::string ipcMessageSummary(const IpcMessage& msg) {
    std::ostringstream oss;
    oss << ipcCommandName(msg.command)
        << " str='" << msg.strArg << "'"
        << " i=[" << msg.iArgs[0] << "," << msg.iArgs[1] << "]"
        << " d=[" << msg.dArgs[0] << "," << msg.dArgs[1] << "]"
        << " sz=" << msg.dataSize;
    return oss.str();
}
