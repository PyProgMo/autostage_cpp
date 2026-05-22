// PIStageProxy.cpp
#include "PIStageProxy.h"
#include "IpcStructs.h"
#include "Logger.h"

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
    AppLogger::instance().info(std::string("PIStageProxy: moveAbs axis=") + axis + " position=" + std::to_string(position));
    IpcMessage req = {};
    req.command = IpcCommand::MoveAbs;
    strncpy(req.strArg, axis, sizeof(req.strArg) - 1);
    req.dArgs[0] = position;
    
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
                            double startMM, double stepMM,
                            double stopMM,  int pulseWidthUs) {
    AppLogger::instance().info(std::string("PIStageProxy: configureTriggerOutput channel=") + std::to_string(channel) +
                               " axis=" + axis +
                               " startMM=" + std::to_string(startMM) +
                               " stepMM=" + std::to_string(stepMM) +
                               " stopMM=" + std::to_string(stopMM) +
                               " pulseWidthUs=" + std::to_string(pulseWidthUs));
    IpcMessage req = {};
    req.command = IpcCommand::ConfigTriggerOut;
    req.iArgs[0] = channel;
    strncpy(req.strArg, axis, sizeof(req.strArg) - 1);
    req.dArgs[0] = startMM;
    req.dArgs[1] = stepMM;
    req.dArgs[2] = stopMM;
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

void PIStageProxy::setRecordTrigger(int triggerSource, int axis, double thresholdMM) {
    AppLogger::instance().info(std::string("PIStageProxy: setRecordTrigger triggerSource=") + std::to_string(triggerSource) +
                               " axis=" + std::to_string(axis) +
                               " thresholdMM=" + std::to_string(thresholdMM));
    IpcMessage req = {};
    req.command = IpcCommand::SetRecordTrigger;
    req.iArgs[0] = triggerSource;
    req.iArgs[1] = axis;
    req.dArgs[0] = thresholdMM;
    
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
