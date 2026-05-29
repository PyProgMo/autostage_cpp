// PIStageProxy.cpp
#include "PIStageProxy.h"
#include "IpcStructs.h"
#include "Logger.h"

#include <array>
#include <iostream>
#include <cstring>
#include <sstream>

PIStageProxy::PIStageProxy() {
    AppLogger::instance().info(std::string("PIStageProxy: connecting to pipe ") + PIPE_NAME);
    // Attempt to connect to the named pipe 
    // Wait until the server is available
    while (1) {
        hPipe_ = CreateFileA(
            PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);

        if (hPipe_ != INVALID_HANDLE_VALUE) {
            AppLogger::instance().info("PIStageProxy: connected to stage pipe");
            break;
        }

        if (GetLastError() != ERROR_PIPE_BUSY) {
            AppLogger::instance().error("PIStageProxy: could not open pipe. Is StageServer running?");
            Sleep(1000);
            continue;
        }

        if (!WaitNamedPipeA(PIPE_NAME, 1000)) {
            AppLogger::instance().error("PIStageProxy: could not open pipe: 1 second wait timed out");
            continue;
        }
    }
}

PIStageProxy::~PIStageProxy() {
    if (hPipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe_);
        hPipe_ = INVALID_HANDLE_VALUE;
    }
}

void PIStageProxy::sendCommand(const IpcMessage& msg, IpcMessage& response) {
    AppLogger::instance().info(std::string("PIStageProxy: sending command ") + ipcMessageSummary(msg));
    // Robust write: ensure entire message is written
    const BYTE* outPtr = reinterpret_cast<const BYTE*>(&msg);
    size_t toWrite = sizeof(IpcMessage);
    DWORD bytesWritten = 0;
    while (toWrite > 0) {
        if (!WriteFile(hPipe_, outPtr, (DWORD)toWrite, &bytesWritten, NULL)) {
            DWORD err = GetLastError();
            std::string message = std::string("PIStageProxy: failed to write to pipe, GLE=") + std::to_string(err) +
                                  " cmd=" + ipcCommandName(msg.command);
            AppLogger::instance().error(message);
            throw std::runtime_error(message);
        }
        outPtr += bytesWritten;
        toWrite -= bytesWritten;
    }
    AppLogger::instance().info(std::string("PIStageProxy: write complete for command ") + ipcCommandName(msg.command));

    // Robust read: loop until we have the full response header
    BYTE* inPtr = reinterpret_cast<BYTE*>(&response);
    size_t toRead = sizeof(IpcMessage);
    DWORD bytesRead = 0;
    while (toRead > 0) {
        if (!ReadFile(hPipe_, inPtr, (DWORD)toRead, &bytesRead, NULL)) {
            DWORD err = GetLastError();
            std::string message = std::string("PIStageProxy: failed to read response from pipe, GLE=") + std::to_string(err) +
                                  " cmd=" + ipcCommandName(msg.command);
            AppLogger::instance().error(message);
            throw std::runtime_error(message);
        }
        if (bytesRead == 0) {
            std::string message = std::string("PIStageProxy: failed to read response from pipe: zero bytes read cmd=") +
                                  ipcCommandName(msg.command);
            AppLogger::instance().error(message);
            throw std::runtime_error(message);
        }
        inPtr += bytesRead;
        toRead -= bytesRead;
    }

    AppLogger::instance().info(std::string("PIStageProxy: read complete for command ") + ipcCommandName(msg.command) +
                               " status=" + std::to_string(response.status));

    if (response.status != 0) {
        std::string message = std::string("PIStageProxy: server returned error status: ") + std::to_string(response.status) +
                              " cmd=" + ipcCommandName(msg.command) +
                              " detail='" + response.strArg + "'";
        AppLogger::instance().error(message);
        throw std::runtime_error(message);
    }
}

void PIStageProxy::loadDLL(const std::string& dllPath) {
    AppLogger::instance().info(std::string("PIStageProxy: loadDLL path='") + dllPath + "'");
    IpcMessage req = {};
    req.command = IpcCommand::LoadDLL;
    strncpy(req.strArg, dllPath.c_str(), sizeof(req.strArg) - 1);
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::connect(const std::string& serialNum) {
    AppLogger::instance().info(std::string("PIStageProxy: connect serial='") + serialNum + "'");
    IpcMessage req = {};
    req.command = IpcCommand::Connect;
    strncpy(req.strArg, serialNum.c_str(), sizeof(req.strArg) - 1);
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::disconnect() {
    AppLogger::instance().info("PIStageProxy: disconnect request");
    IpcMessage req = {};
    req.command = IpcCommand::Disconnect;
    
    IpcMessage res = {};
    try {
        sendCommand(req, res);
    } catch (const std::exception& e) {
        AppLogger::instance().error(std::string("PIStageProxy: disconnect failed: ") + e.what());
    }
}

void PIStageProxy::moveAbs(const char* axis, double position) {
    AppLogger::instance().info(std::string("PIStageProxy: moveAbs axis=") + axis + " position_nm=" + std::to_string(position));
    // Convert from nm -> µm for stage API
    double position_um = position / 1e3;
    IpcMessage req = {};
    req.command = IpcCommand::MoveAbs;
    strncpy(req.strArg, axis, sizeof(req.strArg) - 1);
    req.dArgs[0] = position_um;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

double PIStageProxy::getPos(const char* axis) {
    AppLogger::instance().info(std::string("PIStageProxy: getPos axis=") + axis);
    IpcMessage req = {};
    req.command = IpcCommand::GetPos;
    strncpy(req.strArg, axis, sizeof(req.strArg) - 1);
    
    IpcMessage res = {};
    sendCommand(req, res);
    AppLogger::instance().info(std::string("PIStageProxy: getPos axis=") + axis + " value=" + std::to_string(res.dArgs[0]));
    return res.dArgs[0];
}

std::array<double, 3> PIStageProxy::qpos() {
    AppLogger::instance().info("PIStageProxy: qpos");
    IpcMessage req = {};
    req.command = IpcCommand::QueryPosTuple;

    IpcMessage res = {};
    sendCommand(req, res);

    std::array<double, 3> positions_nm = {
        res.dArgs[0] * 1e3,
        res.dArgs[1] * 1e3,
        res.dArgs[2] * 1e3
    };
    return positions_nm;
}

void PIStageProxy::moveto(double x, double y, double z) {
    AppLogger::instance().info(std::string("PIStageProxy: moveto x_nm=") + std::to_string(x) +
                               " y_nm=" + std::to_string(y) +
                               " z_nm=" + std::to_string(z));
    IpcMessage req = {};
    req.command = IpcCommand::MoveTuple;
    req.dArgs[0] = x / 1e3;
    req.dArgs[1] = y / 1e3;
    req.dArgs[2] = z / 1e3;

    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::adda(double vx, double vy, double vz) {
    AppLogger::instance().info(std::string("PIStageProxy: adda vx_nm_s=") + std::to_string(vx) +
                               " vy_nm_s=" + std::to_string(vy) +
                               " vz_nm_s=" + std::to_string(vz));
    IpcMessage req = {};
    req.command = IpcCommand::SetVelocityTuple;
    req.dArgs[0] = vx / 1e3;
    req.dArgs[1] = vy / 1e3;
    req.dArgs[2] = vz / 1e3;

    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::halt() {
    AppLogger::instance().info("PIStageProxy: halt requested");
    IpcMessage req = {};
    req.command = IpcCommand::Halt;

    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::runVelocitySweep(double vNominal, double xStop, double yHold, double xStart, double xStep) {
    AppLogger::instance().info("PIStageProxy: runVelocitySweep");
    IpcMessage req = {};
    req.command = IpcCommand::RunVelocitySweep;
    // Convert inputs from nm (and nm/s) to µm (and µm/s)
    double vNominal_um_s = vNominal / 1e3;
    double xStop_um = xStop / 1e3;
    double yHold_um = yHold / 1e3;
    double xStart_um = xStart / 1e3;
    double xStep_um = xStep / 1e3;

    req.dArgs[0] = vNominal_um_s;
    req.dArgs[1] = xStop_um;
    req.dArgs[2] = yHold_um;
    req.dArgs[3] = xStart_um;
    
    std::string stepStr = std::to_string(xStep_um);
    strncpy(req.strArg, stepStr.c_str(), sizeof(req.strArg) - 1);

    IpcMessage res;
    sendCommand(req, res);
}

void PIStageProxy::uploadZProfile(const std::vector<double>& zProfile) {
    AppLogger::instance().info("PIStageProxy: uploadZProfile");
    IpcMessage req = {};
    req.command = IpcCommand::UploadZProfile;
    // Convert zProfile from nm -> µm for stage API
    std::vector<double> z_um;
    z_um.reserve(zProfile.size());
    for (double v : zProfile) z_um.push_back(v / 1e3);
    req.dataSize = (int32_t)(z_um.size() * sizeof(double));

    // Send header first
    DWORD bytesWritten = 0;
    if (!WriteFile(hPipe_, &req, sizeof(req), &bytesWritten, NULL)) {
        throw std::runtime_error("PIStageProxy IPC write cmd failed");
    }

    // Send payload if any
    if (req.dataSize > 0) {
        DWORD expectedBytes = static_cast<DWORD>(req.dataSize);
        if (!WriteFile(hPipe_, z_um.data(), expectedBytes, &bytesWritten, NULL) || bytesWritten != expectedBytes) {
            throw std::runtime_error("PIStageProxy IPC write payload failed");
        }
    }

    // Wait for response header
    IpcMessage res = {};
    DWORD bytesRead = 0;
    if (!ReadFile(hPipe_, &res, sizeof(res), &bytesRead, NULL)) {
        throw std::runtime_error("PIStageProxy IPC read response failed");
    }

    if (res.status != 0) {
        throw std::runtime_error(std::string("Stage Server returned error code ") + std::to_string(res.status));
    }
}

void PIStageProxy::waitOnTarget(const char* axis, int timeoutMs) {
    AppLogger::instance().info(std::string("PIStageProxy: waitOnTarget axis=") + axis + " timeoutMs=" + std::to_string(timeoutMs));
    IpcMessage req = {};
    req.command = IpcCommand::WaitOnTarget;
    strncpy(req.strArg, axis, sizeof(req.strArg) - 1);
    req.iArgs[0] = timeoutMs;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::configureTriggerOutput(int channel, const char* axis,
                            double start_nm, double step_nm,
                            double stop_nm,  int pulseWidthUs) {
    AppLogger::instance().info(std::string("PIStageProxy: configureTriggerOutput channel=") + std::to_string(channel) +
                               " axis=" + axis +
                               " start_nm=" + std::to_string(start_nm) +
                               " step_nm=" + std::to_string(step_nm) +
                               " stop_nm=" + std::to_string(stop_nm) +
                               " pulseWidthUs=" + std::to_string(pulseWidthUs));
    // convert nm -> µm
    double start_um = start_nm / 1e3;
    double step_um = step_nm / 1e3;
    double stop_um = stop_nm / 1e3;
    IpcMessage req = {};
    req.command = IpcCommand::ConfigTriggerOut;
    req.iArgs[0] = channel;
    strncpy(req.strArg, axis, sizeof(req.strArg) - 1);
    req.dArgs[0] = start_um;
    req.dArgs[1] = step_um;
    req.dArgs[2] = stop_um;
    req.iArgs[1] = pulseWidthUs;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::enableTriggerOutput(int channel, bool enable) {
    AppLogger::instance().info(std::string("PIStageProxy: enableTriggerOutput channel=") + std::to_string(channel) +
                               " enable=" + (enable ? "true" : "false"));
    IpcMessage req = {};
    req.command = IpcCommand::EnableTriggerOut;
    req.iArgs[0] = channel;
    req.iArgs[1] = enable ? 1 : 0;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::waitForTriggerInput(int trigChannel, int timeoutMs) {
    AppLogger::instance().info(std::string("PIStageProxy: waitForTriggerInput channel=") + std::to_string(trigChannel) +
                               " timeoutMs=" + std::to_string(timeoutMs));
    IpcMessage req = {};
    req.command = IpcCommand::WaitTriggerIn;
    req.iArgs[0] = trigChannel;
    req.iArgs[1] = timeoutMs;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::setWaitOnGo(const char* axis, int conditionMask) {
    AppLogger::instance().info(std::string("PIStageProxy: setWaitOnGo axis=") + axis + " mask=" + std::to_string(conditionMask));
    IpcMessage req = {};
    req.command = IpcCommand::SetWaitOnGo;
    strncpy(req.strArg, axis, sizeof(req.strArg) - 1);
    req.iArgs[0] = conditionMask;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::setupDataRecorder(int table, const char* source, int option) {
    AppLogger::instance().info(std::string("PIStageProxy: setupDataRecorder table=") + std::to_string(table) +
                               " source=" + source +
                               " option=" + std::to_string(option));
    IpcMessage req = {};
    req.command = IpcCommand::SetupDataRecorder;
    req.iArgs[0] = table;
    strncpy(req.strArg, source, sizeof(req.strArg) - 1);
    req.iArgs[1] = option;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::setRecordTrigger(int triggerSource, int axis, double threshold_nm) {
    AppLogger::instance().info(std::string("PIStageProxy: setRecordTrigger triggerSource=") + std::to_string(triggerSource) +
                               " axis=" + std::to_string(axis) +
                               " threshold_nm=" + std::to_string(threshold_nm));
    IpcMessage req = {};
    req.command = IpcCommand::SetRecordTrigger;
    req.iArgs[0] = triggerSource;
    req.iArgs[1] = axis;
    // convert nm -> µm
    req.dArgs[0] = threshold_nm / 1e3;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::setRecordRate(int cycleDiv) {
    AppLogger::instance().info(std::string("PIStageProxy: setRecordRate cycleDiv=") + std::to_string(cycleDiv));
    IpcMessage req = {};
    req.command = IpcCommand::SetRecordRate;
    req.iArgs[0] = cycleDiv;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

std::vector<double> PIStageProxy::readRecorder(int startOffset, int numValues,
                                 const int* tables, int nTables) {
    AppLogger::instance().info(std::string("PIStageProxy: readRecorder startOffset=") + std::to_string(startOffset) +
                               " numValues=" + std::to_string(numValues) +
                               " nTables=" + std::to_string(nTables));
    IpcMessage req = {};
    req.command = IpcCommand::ReadRecorder;
    req.iArgs[0] = startOffset;
    req.iArgs[1] = numValues;
    req.iArgs[2] = nTables;
    for (int i = 0; i < nTables && i < 4; ++i) {
        req.iArgs[3 + i] = tables[i];
    }
    
    IpcMessage res = {};
    sendCommand(req, res);
    
    std::vector<double> data;
    if (res.dataSize > 0) {
        data.resize(res.dataSize / sizeof(double));
        DWORD bytesRead = 0;
        if (!ReadFile(hPipe_, data.data(), res.dataSize, &bytesRead, NULL)) {
            std::string message = "PIStageProxy: failed to read variable data from pipe";
            AppLogger::instance().error(message);
            throw std::runtime_error(message);
        }
        AppLogger::instance().info(std::string("PIStageProxy: readRecorder payload bytes=") + std::to_string(bytesRead));
    }
    return data;
}

void PIStageProxy::checkError() {
    // Errors are already thrown automatically on response.status != 0
}
