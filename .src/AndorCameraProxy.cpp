// AndorCameraProxy.cpp
#include "AndorCameraProxy.h"
#include "IpcStructs.h"
#include "Logger.h"
#include <opencv2/opencv.hpp>

#include <iostream>
#include <cstring>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace {

std::string joinPath(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    const char last = left.back();
    if (last == '\\' || last == '/') {
        return left + right;
    }
    return left + "\\" + right;
}

std::string executableDirectory() {
    char buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        throw std::runtime_error("AndorCameraProxy: failed to resolve executable directory");
    }

    std::string fullPath(buffer, length);
    size_t slash = fullPath.find_last_of("\\/");
    if (slash == std::string::npos) {
        return ".";
    }
    return fullPath.substr(0, slash);
}

void ensureDirectoryExists(const std::string& path) {
    if (path.empty()) {
        return;
    }

    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return;
    }

    size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos) {
        std::string parent = path.substr(0, slash);
        if (!(parent.size() == 2 && parent[1] == ':')) {
            ensureDirectoryExists(parent);
        }
    }

    CreateDirectoryA(path.c_str(), nullptr);
}

std::string timestampString() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto nowTime = clock::to_time_t(now);

    std::tm tm = {};
#if defined(_WIN32)
    localtime_s(&tm, &nowTime);
#else
    localtime_r(&nowTime, &tm);
#endif

    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << '_' << std::setw(3) << std::setfill('0') << millis.count();
    return oss.str();
}

std::string createMeasurementFolder() {
    const std::string baseDir = joinPath(executableDirectory(), "measurements");
    ensureDirectoryExists(baseDir);

    std::string folder = joinPath(baseDir, timestampString());
    ensureDirectoryExists(folder);
    return folder;
}

cv::Mat buildSpectrumImage(const std::vector<WORD>& spectra, int numSpectra, int pixelsPerSpectrum) {
    WORD maxVal = 0;
    for (WORD val : spectra) {
        if (val > maxVal) {
            maxVal = val;
        }
    }
    if (maxVal == 0) {
        maxVal = 1;
    }

    cv::Mat img(numSpectra, pixelsPerSpectrum, CV_8UC1);
    for (int i = 0; i < numSpectra; ++i) {
        for (int j = 0; j < pixelsPerSpectrum; ++j) {
            WORD val = spectra[i * pixelsPerSpectrum + j];
            img.at<uchar>(i, j) = static_cast<uchar>((val * 255) / maxVal);
        }
    }
    return img;
}

void writeSpectrumTxt(const std::string& filePath, const std::vector<WORD>& spectra, int numSpectra, int pixelsPerSpectrum) {
    std::ofstream outFile(filePath.c_str());
    if (!outFile) {
        throw std::runtime_error(std::string("AndorCameraProxy: failed to write TXT: ") + filePath);
    }

    for (int i = 0; i < numSpectra; ++i) {
        for (int j = 0; j < pixelsPerSpectrum; ++j) {
            outFile << spectra[i * pixelsPerSpectrum + j] << ' ';
        }
        outFile << '\n';
    }
}

} // namespace

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

void AndorCameraProxy::setReadMode(int mode) {
    IpcMessage req = {}, res = {};
    req.command = IpcCommand::AndorSetReadMode; req.iArgs[0] = mode;
    sendCommand(req, res);
}

void AndorCameraProxy::setAcquisitionMode(int mode) {
    IpcMessage req = {}, res = {};
    req.command = IpcCommand::AndorSetAcquisitionMode; req.iArgs[0] = mode;
    sendCommand(req, res);
}

void AndorCameraProxy::setExposureTime(float seconds) {
    IpcMessage req = {}, res = {};
    req.command = IpcCommand::AndorSetExposureTime; req.dArgs[0] = seconds;
    sendCommand(req, res);
}

void AndorCameraProxy::setTriggerMode(int mode) {
    IpcMessage req = {}, res = {};
    req.command = IpcCommand::AndorSetTriggerMode; req.iArgs[0] = mode;
    sendCommand(req, res);
}

void AndorCameraProxy::setImage(int hbin, int vbin, int hstart, int hend, int vstart, int vend) {
    IpcMessage req = {}, res = {};
    req.command = IpcCommand::AndorSetImage;
    req.iArgs[0] = hbin; req.iArgs[1] = vbin; req.iArgs[2] = hstart;
    req.iArgs[3] = hend; req.iArgs[4] = vstart; req.iArgs[5] = vend;
    sendCommand(req, res);
}

int AndorCameraProxy::getStatus() {
    IpcMessage req = {}, res = {};
    req.command = IpcCommand::AndorGetStatus;
    sendCommand(req, res);
    return res.iArgs[0];
}

void AndorCameraProxy::setKineticCycleTime(float seconds) {
    IpcMessage req = {}, res = {};
    req.command = IpcCommand::AndorSetKineticCycleTime; req.dArgs[0] = seconds;
    sendCommand(req, res);
}

void AndorCameraProxy::setNumberKinetics(int numKin) {
    IpcMessage req = {}, res = {};
    req.command = IpcCommand::AndorSetNumberKinetics; req.iArgs[0] = numKin;
    sendCommand(req, res);
}

void AndorCameraProxy::configureSpectral(AndorCamera::ReadMode readMode, AndorCamera::TriggerMode trigMode, float exposureSeconds, int numSpectra) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorConfigureSpectral;
    req.iArgs[0] = static_cast<int>(readMode);
    req.iArgs[1] = static_cast<int>(trigMode);
    req.iArgs[2] = numSpectra;
    req.dArgs[0] = exposureSeconds;
    
    IpcMessage res = {};
    sendCommand(req, res);
}

void AndorCameraProxy::configureFVBKinetic(float exposureSeconds, int numLines) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorConfigureFVBKinetic;
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

// new test function: 
void AndorCameraProxy::testAcquireAndSave(const std::vector<WORD>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename) {
    if (numSpectra <= 0 || pixelsPerSpectrum <= 0) {
        throw std::runtime_error("AndorCameraProxy: invalid spectrum image dimensions");
    }
    if (spectra.empty() || spectra.size() < static_cast<size_t>(numSpectra) * static_cast<size_t>(pixelsPerSpectrum)) {
        throw std::runtime_error("AndorCameraProxy: testAcquireAndSave requires non-empty spectrum data");
    }

    const std::string measurementFolder = createMeasurementFolder();

    if (numSpectra == 1) {
        const std::string stem = joinPath(measurementFolder, filename.empty() ? "spectrum" : filename);
        cv::Mat img = buildSpectrumImage(spectra, numSpectra, pixelsPerSpectrum);

        if (!cv::imwrite(stem + ".png", img)) {
            throw std::runtime_error(std::string("AndorCameraProxy: failed to write PNG: ") + stem);
        }

        writeSpectrumTxt(stem + ".txt", spectra, numSpectra, pixelsPerSpectrum);
        return;
    }

    for (int frame = 0; frame < numSpectra; ++frame) {
        std::vector<WORD> frameData(spectra.begin() + (frame * pixelsPerSpectrum),
                                    spectra.begin() + ((frame + 1) * pixelsPerSpectrum));
        std::ostringstream name;
        name << (filename.empty() ? "frame" : filename) << '_' << std::setw(3) << std::setfill('0') << frame;
        const std::string stem = joinPath(measurementFolder, name.str());
        cv::Mat img = buildSpectrumImage(frameData, 1, pixelsPerSpectrum);

        if (!cv::imwrite(stem + ".png", img)) {
            throw std::runtime_error(std::string("AndorCameraProxy: failed to write PNG: ") + stem);
        }

        writeSpectrumTxt(stem + ".txt", frameData, 1, pixelsPerSpectrum);
    }
}

// overload that acquires data and calls the above test function
void AndorCameraProxy::testAcquireAndSave(float exposureS, const std::string& filename) {
    configureSpectral(AndorCamera::ReadMode::FVB, AndorCamera::TriggerMode::Internal, exposureS, 1);
    startAcquisition();
    waitForAcquisition();

    std::vector<WORD> spectra = getAllSpectra(1, getXPixels());
    testAcquireAndSave(spectra, 1, getXPixels(), filename);
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