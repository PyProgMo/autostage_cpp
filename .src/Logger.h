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
    default: return "Unknown";
    }
}

inline std::string ipcMessageSummary(const IpcMessage& msg) {
    std::ostringstream oss;
    oss << ipcCommandName(msg.command)
        << " status=" << msg.status
        << " str='" << msg.strArg << "'"
        << " i0=" << msg.iArgs[0]
        << " i1=" << msg.iArgs[1]
        << " d0=" << msg.dArgs[0]
        << " d1=" << msg.dArgs[1]
        << " dataSize=" << msg.dataSize;
    return oss.str();
}
