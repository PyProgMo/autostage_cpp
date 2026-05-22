// PIStageProxy.cpp
#include "PIStageProxy.h"
#include "IpcStructs.h"
#include <iostream>
#include <cstring>

PIStageProxy::PIStageProxy() {
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
            break;
        }

        if (GetLastError() != ERROR_PIPE_BUSY) {
            std::cerr << "Could not open pipe. Is StageServer running?\n";
            Sleep(1000);
            continue;
        }

        if (!WaitNamedPipeA(PIPE_NAME, 1000)) {
            std::cerr << "Could not open pipe: 1 second wait timed out.\n";
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
    std::cerr << "PIStageProxy: sending command " << static_cast<int>(msg.command) << "\n";
    // Robust write: ensure entire message is written
    const BYTE* outPtr = reinterpret_cast<const BYTE*>(&msg);
    size_t toWrite = sizeof(IpcMessage);
    DWORD bytesWritten = 0;
    while (toWrite > 0) {
        if (!WriteFile(hPipe_, outPtr, (DWORD)toWrite, &bytesWritten, NULL)) {
            DWORD err = GetLastError();
            throw std::runtime_error(std::string("Failed to write to pipe, GLE=") + std::to_string(err));
        }
        outPtr += bytesWritten;
        toWrite -= bytesWritten;
    }
    std::cerr << "PIStageProxy: write complete for command " << static_cast<int>(msg.command) << "\n";

    // Robust read: loop until we have the full response header
    BYTE* inPtr = reinterpret_cast<BYTE*>(&response);
    size_t toRead = sizeof(IpcMessage);
    DWORD bytesRead = 0;
    while (toRead > 0) {
        if (!ReadFile(hPipe_, inPtr, (DWORD)toRead, &bytesRead, NULL)) {
            DWORD err = GetLastError();
            throw std::runtime_error(std::string("Failed to read response from pipe, GLE=") + std::to_string(err));
        }
        if (bytesRead == 0) {
            throw std::runtime_error("Failed to read response from pipe: zero bytes read");
        }
        inPtr += bytesRead;
        toRead -= bytesRead;
    }

    std::cerr << "PIStageProxy: read complete for command " << static_cast<int>(msg.command) << "\n";

    if (response.status != 0) {
        throw std::runtime_error(std::string("PIStageProxy: Server returned error status: ") + std::to_string(response.status));
    }
}

void PIStageProxy::loadDLL(const std::string& dllPath) {
    IpcMessage req = {};
    req.command = IpcCommand::LoadDLL;
    strncpy(req.strArg, dllPath.c_str(), sizeof(req.strArg) - 1);
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::connect(const std::string& serialNum) {
    IpcMessage req = {};
    req.command = IpcCommand::Connect;
    strncpy(req.strArg, serialNum.c_str(), sizeof(req.strArg) - 1);
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::disconnect() {
    IpcMessage req = {};
    req.command = IpcCommand::Disconnect;
    
    IpcMessage res = {};
    try {
        sendCommand(req, res);
    } catch (...) {}
}

void PIStageProxy::moveAbs(const char* axis, double position) {
    IpcMessage req = {};
    req.command = IpcCommand::MoveAbs;
    strncpy(req.strArg, axis, sizeof(req.strArg) - 1);
    req.dArgs[0] = position;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

double PIStageProxy::getPos(const char* axis) {
    IpcMessage req = {};
    req.command = IpcCommand::GetPos;
    strncpy(req.strArg, axis, sizeof(req.strArg) - 1);
    
    IpcMessage res = {};
    sendCommand(req, res);
    return res.dArgs[0];
}

void PIStageProxy::waitOnTarget(const char* axis, int timeoutMs) {
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
    IpcMessage req = {};
    req.command = IpcCommand::EnableTriggerOut;
    req.iArgs[0] = channel;
    req.iArgs[1] = enable ? 1 : 0;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::waitForTriggerInput(int trigChannel, int timeoutMs) {
    IpcMessage req = {};
    req.command = IpcCommand::WaitTriggerIn;
    req.iArgs[0] = trigChannel;
    req.iArgs[1] = timeoutMs;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::setWaitOnGo(const char* axis, int conditionMask) {
    IpcMessage req = {};
    req.command = IpcCommand::SetWaitOnGo;
    strncpy(req.strArg, axis, sizeof(req.strArg) - 1);
    req.iArgs[0] = conditionMask;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::setupDataRecorder(int table, const char* source, int option) {
    IpcMessage req = {};
    req.command = IpcCommand::SetupDataRecorder;
    req.iArgs[0] = table;
    strncpy(req.strArg, source, sizeof(req.strArg) - 1);
    req.iArgs[1] = option;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::setRecordTrigger(int triggerSource, int axis, double thresholdMM) {
    IpcMessage req = {};
    req.command = IpcCommand::SetRecordTrigger;
    req.iArgs[0] = triggerSource;
    req.iArgs[1] = axis;
    req.dArgs[0] = thresholdMM;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void PIStageProxy::setRecordRate(int cycleDiv) {
    IpcMessage req = {};
    req.command = IpcCommand::SetRecordRate;
    req.iArgs[0] = cycleDiv;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

std::vector<double> PIStageProxy::readRecorder(int startOffset, int numValues,
                                 const int* tables, int nTables) {
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
            throw std::runtime_error("Failed to read variable data from pipe");
        }
    }
    return data;
}

void PIStageProxy::checkError() {
    // Errors are already thrown automatically on response.status != 0
}
