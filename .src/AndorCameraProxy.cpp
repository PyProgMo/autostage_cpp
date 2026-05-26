// AndorCameraProxy.cpp
#include "AndorCameraProxy.h"
#include "IpcStructs.h"
#include "Logger.h"

#include <iostream>
#include <cstring>

AndorCameraProxy::AndorCameraProxy() {
    AppLogger::instance().info(std::string("AndorCameraProxy: connecting to pipe ") + ANDOR_PIPE_NAME);
    while (1) {
        hPipe_ = CreateFileA(
            ANDOR_PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);

        if (hPipe_ != INVALID_HANDLE_VALUE) {
            AppLogger::instance().info("AndorCameraProxy: connected to andor pipe");
            break;
        }

        if (GetLastError() != ERROR_PIPE_BUSY) {
            AppLogger::instance().error("AndorCameraProxy: could not open pipe. Is SpectrometerServer running?");
            Sleep(1000);
            continue;
        }

        if (!WaitNamedPipeA(ANDOR_PIPE_NAME, 1000)) {
            AppLogger::instance().error("AndorCameraProxy: could not open pipe: 1 second wait timed out");
            continue;
        }
    }
}

AndorCameraProxy::~AndorCameraProxy() {
    if (hPipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe_);
        hPipe_ = INVALID_HANDLE_VALUE;
    }
}

void AndorCameraProxy::sendCommand(const IpcMessage& msg, IpcMessage& response) {
    const BYTE* outPtr = reinterpret_cast<const BYTE*>(&msg);
    size_t toWrite = sizeof(IpcMessage);
    DWORD bytesWritten = 0;
    while (toWrite > 0) {
        if (!WriteFile(hPipe_, outPtr, (DWORD)toWrite, &bytesWritten, NULL)) {
            throw std::runtime_error("AndorCameraProxy: failed to write to pipe");
        }
        outPtr += bytesWritten;
        toWrite -= bytesWritten;
    }

    BYTE* inPtr = reinterpret_cast<BYTE*>(&response);
    size_t toRead = sizeof(IpcMessage);
    DWORD bytesRead = 0;
    while (toRead > 0) {
        if (!ReadFile(hPipe_, inPtr, (DWORD)toRead, &bytesRead, NULL)) {
            throw std::runtime_error("AndorCameraProxy: failed to read response header from pipe");
        }
        if (bytesRead == 0) throw std::runtime_error("AndorCameraProxy: zero bytes read");
        inPtr += bytesRead;
        toRead -= bytesRead;
    }

    if (response.status != 0) {
        throw std::runtime_error(std::string("AndorCameraProxy: server returned error: ") + response.strArg);
    }
}

void AndorCameraProxy::loadDLL(const std::string& dllPath) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorLoadDLL;
    strncpy(req.strArg, dllPath.c_str(), sizeof(req.strArg) - 1);
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void AndorCameraProxy::initialize(const std::string& iniDir) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorInitialize;
    strncpy(req.strArg, iniDir.c_str(), sizeof(req.strArg) - 1);
    
    IpcMessage res = {};
    sendCommand(req, res);
    
    xpix_ = res.iArgs[0];
    ypix_ = res.iArgs[1];
}

void AndorCameraProxy::shutdown() {
    IpcMessage req = {};
    req.command = IpcCommand::AndorShutDown;
    IpcMessage res = {};
    try {
        sendCommand(req, res);
    } catch (...) {}
}

void AndorCameraProxy::configureSpectral(AndorCamera::ReadMode readMode, AndorCamera::TriggerMode trigMode, float exposureSeconds, int numSpectra) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorSetAcquisitionMode; // Custom proxy mapping
    req.iArgs[0] = static_cast<int>(readMode);
    req.iArgs[1] = static_cast<int>(trigMode);
    req.iArgs[2] = numSpectra;
    req.dArgs[0] = exposureSeconds;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void AndorCameraProxy::configureFVBKinetic(float exposureSeconds, int numLines) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorSetKineticCycleTime; // custom proxy mapping
    req.dArgs[0] = exposureSeconds;
    req.iArgs[0] = numLines;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void AndorCameraProxy::startAcquisition() {
    IpcMessage req = {};
    req.command = IpcCommand::AndorStartAcquisition;
    IpcMessage res = {};
    sendCommand(req, res);
}

void AndorCameraProxy::abortAcquisition() {
    IpcMessage req = {};
    req.command = IpcCommand::AndorAbortAcquisition;
    IpcMessage res = {};
    sendCommand(req, res);
}

void AndorCameraProxy::waitForAcquisition() {
    IpcMessage req = {};
    req.command = IpcCommand::AndorWaitForAcquisition;
    IpcMessage res = {};
    sendCommand(req, res);
}

std::vector<WORD> AndorCameraProxy::getAllSpectra(int numSpectra, int pixelsPerSpectrum) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorGetImages16;
    req.iArgs[0] = numSpectra;
    req.iArgs[1] = pixelsPerSpectrum;
    
    IpcMessage res = {};
    sendCommand(req, res);
    
    std::vector<WORD> obj(res.dataSize / sizeof(WORD));
    
    if (res.dataSize > 0) {
        BYTE* inPtr = reinterpret_cast<BYTE*>(obj.data());
        size_t toRead = res.dataSize;
        DWORD bytesRead = 0;
        while (toRead > 0) {
            if (!ReadFile(hPipe_, inPtr, (DWORD)toRead, &bytesRead, NULL)) {
                throw std::runtime_error("AndorCameraProxy: failed to read payload from pipe");
            }
            if (bytesRead == 0) throw std::runtime_error("AndorCameraProxy: zero bytes read on payload");
            inPtr += bytesRead;
            toRead -= bytesRead;
        }
    }
    return obj;
}