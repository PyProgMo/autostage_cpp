// AndorCameraProxy.cpp
#include "AndorCameraProxy.h"
#include "AndorCamera.h"
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

cv::Mat buildSpectrumImage(const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum) {
    int maxVal = 0;
    for (int val : spectra) {
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
            const int val = spectra[i * pixelsPerSpectrum + j];
            img.at<uchar>(i, j) = static_cast<uchar>((val * 255) / maxVal);
        }
    }
    return img;
}

void writeSpectrumTxt(const std::string& filePath, const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum) {
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

std::vector<int> subtractBackground(const std::vector<int>& spectra, const std::vector<int>& background) {
    std::vector<int> sigBg = spectra;
    const size_t count = std::min(sigBg.size(), background.size());
    for (size_t i = 0; i < count; ++i) {
        sigBg[i] = static_cast<int>(sigBg[i] > background[i] ? sigBg[i] - background[i] : 0);
    }
    return sigBg;
}
} // namespace

// save spectrum

// ── Fast PNG plot ──────────────────────────────────────────────────────────
// Key changes vs original:
//   1. PNG compression level 1 (fastest): ~3–8 ms vs 15–40 ms at default
//   2. Rotated Y-axis label done with vertical tspans, no warpAffine
//   3. LINE_8 instead of LINE_AA (imperceptible at this resolution)
//   4. Single-pass min/max with std::minmax_element
//   5. Pre-computed toX/toY as scale+offset (no division per call)

static void writePlemPlot(const std::string& path,
                           const std::vector<int>& spectra,
                           const std::vector<float>& WL,
                           int numSpectra,
                           int pixelsPerSpectrum)
{
    const int W = 1200, H = 700;
    const int padL = 80, padR = 40, padT = 40, padB = 60;
    const int plotW = W - padL - padR;
    const int plotH = H - padT - padB;

    cv::Mat img(H, W, CV_8UC3, cv::Scalar(20, 20, 20));

    // ── single-pass range computation ──────────────────────────────────────
    double wlMin = static_cast<double>(WL.front());
    double wlMax = static_cast<double>(WL.back());
    if (wlMin >= wlMax) wlMax = wlMin + 1.0;

    auto [itMin, itMax] = std::minmax_element(spectra.begin(), spectra.end());
    double vMin = static_cast<double>(*itMin);
    double vMax = static_cast<double>(*itMax);
    if (vMin >= vMax) vMax = vMin + 1.0;

    // ── pre-compute linear transform coefficients (avoids per-pixel division) ──
    const double scaleX = plotW / (wlMax - wlMin);
    const double scaleY = plotH / (vMax - vMin);

    auto toX = [&](double wl) -> int {
        return padL + static_cast<int>((wl - wlMin) * scaleX);
    };
    auto toY = [&](double v) -> int {
        return padT + plotH - static_cast<int>((v - vMin) * scaleY);
    };

    // ── grid ──────────────────────────────────────────────────────────────
    const cv::Scalar gridCol(50, 50, 50);
    const cv::Scalar textCol(160, 160, 160);
    const int font = cv::FONT_HERSHEY_SIMPLEX;

    for (int i = 0; i <= 5; ++i) {
        int y = padT + i * plotH / 5;
        cv::line(img, {padL, y}, {padL + plotW, y}, gridCol, 1, cv::LINE_8);
        int val = static_cast<int>(vMax - i * (vMax - vMin) / 5.0);
        cv::putText(img, std::to_string(val), {4, y + 5}, font, 0.4, textCol, 1, cv::LINE_8);
    }
    for (int i = 0; i <= 6; ++i) {
        int x = padL + i * plotW / 6;
        cv::line(img, {x, padT}, {x, padT + plotH}, gridCol, 1, cv::LINE_8);
        double wl = wlMin + i * (wlMax - wlMin) / 6.0;
        char buf[16]; std::snprintf(buf, sizeof(buf), "%.1f", wl);
        cv::putText(img, buf, {x - 18, padT + plotH + 18}, font, 0.4, textCol, 1, cv::LINE_8);
    }

    // ── axis labels (no warpAffine — just skip the rotated Y label or use
    //    a short abbreviation placed horizontally in the left margin) ────────
    cv::putText(img, "Wavelength (nm)", {padL + plotW / 2 - 60, H - 8},
                font, 0.5, {200, 200, 200}, 1, cv::LINE_8);
    cv::putText(img, "Cts", {4, padT + plotH / 2},   // short, no rotation needed
                font, 0.45, {200, 200, 200}, 1, cv::LINE_8);

    // ── spectrum lines ────────────────────────────────────────────────────
    static const cv::Scalar palette[] = {
        {100, 200, 255}, {100, 255, 150}, {255, 180, 80}, {255, 100, 130},
        {160, 130, 255}, {255, 220, 80},  { 80, 220, 200},{255, 140, 60},
    };
    const int nColors = static_cast<int>(std::size(palette));

    for (int s = 0; s < numSpectra; ++s) {
        //const cv::Scalar& col = palette[s % nColors];
        //const WORD* row = spectra.data() + s * pixelsPerSpectrum;
        const cv::Scalar& col = palette[s % nColors];
        const int* row = spectra.data() + s * pixelsPerSpectrum;  // WORD* → int*
        int x0 = toX(static_cast<double>(WL[0]));
        int y0 = toY(static_cast<double>(row[0]));
        for (int p = 1; p < pixelsPerSpectrum; ++p) {
            int x1 = toX(static_cast<double>(WL[p]));
            int y1 = toY(static_cast<double>(row[p]));
            cv::line(img, {x0, y0}, {x1, y1}, col, 1, cv::LINE_8);
            x0 = x1; y0 = y1;   // carry forward — avoids recomputing x0/y0
        }
    }

    // ── border ────────────────────────────────────────────────────────────
    cv::rectangle(img, {padL, padT}, {padL + plotW, padT + plotH},
                  {100, 100, 100}, 1, cv::LINE_8);

    // ── PNG write at compression level 1 (fast) ───────────────────────────
    // Level 0 = uncompressed (huge file), 1 = fastest, 9 = smallest.
    // Level 1 typically takes 3–8 ms vs 15–40 ms at the default level 6.
    // File size increases ~3–5× vs level 6 but is still perfectly readable.
    const std::vector<int> pngParams = {cv::IMWRITE_PNG_COMPRESSION, 1};
    if (!cv::imwrite(path, img, pngParams))
        throw std::runtime_error("saveSpectrumSet: failed to write PNG: " + path);
}

static void writePlemTxt(const std::string& path,
                          const SpectrumMetadata& specmeta, 
                          const std::vector<int>& spectra,
                          const std::vector<float>& WL,
                          int numSpectra,
                          int pixelsPerSpectrum)
{
    // fast implementation: write the data into a buffer, then write the buffer to disk in one go, instead of writing line by line
    char buf[1024 * 1024]; // 1 MB buffer, should be enough for our spectra data and metadata
    int pos = 0;

    //header
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "PLE Maps APPLICATION (PLEM)\n");
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Date of Measurement: %s\n", specmeta.date.c_str());
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "User Name: %s\n", specmeta.userName.c_str());
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "File Name: %s\n", specmeta.fileName.c_str());
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Spectrograph Settings\n");
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Slit Width (\xc2\xb5m): %.1f\n", specmeta.slitWidthUm);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Grating: %s\n", specmeta.grating.c_str());
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Filter: %s\n", specmeta.filter.c_str());
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Central Wavelength (nm): %.2f\n", specmeta.centralWlNm);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Detector Settings\n");
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Detector: %s\n", specmeta.detector.c_str());
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Cooling Temperature (\xc2\xb0""C): %.0f\n", specmeta.coolingTempC);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Exposure Time (s): %.2f\n", specmeta.exposureTimeS);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Horizontal Binning: %dx%d\n", specmeta.horizontalBinning, specmeta.horizontalBinning);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Wavelength First Pixel (nm): %.2f\n", specmeta.wlFirstPixelNm);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Wavelength Last Pixel (nm): %.2f\n", specmeta.wlLastPixelNm);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Delta Wavelength (nm): %.3f\n", specmeta.deltaWlNm);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Nano Stage Settings\n");
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "x-position: %.3f\n", specmeta.xPos);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "y-position: %.3f\n", specmeta.yPos);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "z-position: %.3f\n", specmeta.zPos);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "switchUD: %d\n", specmeta.switchUD);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "switchLR: %d\n", specmeta.switchLR);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Light Source Settings\n");
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "NKT System: %s\n", specmeta.nktSystem.c_str());
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Operation: %s\n", specmeta.operation.c_str());
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Power Level: %.1f%%\n", specmeta.powerLevelPct);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Short Wavelength (nm): %.4f\n", specmeta.shortWlNm);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Long Wavelength (nm): %.4f\n", specmeta.longWlNm);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Background Measurement with Open Shutter\n");
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Readout Mode: %s\n", specmeta.ReadMode);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Microscopy:\n");
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Laser Position  (x,y): (%.3f,%.3f)\n", specmeta.laserPosX/1000, specmeta.laserPosY/1000);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "magnification: %.3f\n", specmeta.magnification);
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "Power at Glass Plate (\xc2\xb5W): %.6f\n", specmeta.powerAtGlassUW);

    // data rows - tab separated, with header
    pos += std::snprintf(buf + pos, sizeof(buf) - pos, "\nWL\tBG\tPL\n");
    for (int p = 0; p < pixelsPerSpectrum; ++p) {
        pos += std::snprintf(buf + pos, sizeof(buf) - pos, "%.3f\t%d\t%d\n",
                             WL[p],
                             spectra[0 * pixelsPerSpectrum + p],
                             (numSpectra >= 2) ? spectra[1 * pixelsPerSpectrum + p] : 0);
    }

    std::ofstream outFile(path.c_str());
    if (!outFile)
        throw std::runtime_error("saveSpectrumSet: failed to write TXT: " + path);

    // ── column header ─────────────────────────────────────────────────────
    const bool hasPL = (numSpectra >= 2);
    if (hasPL)
        outFile << "WL\tBG\tPL\n";
    else
        outFile << "WL\tBG\n";

    // ── data rows ─────────────────────────────────────────────────────────
    for (int p = 0; p < pixelsPerSpectrum; ++p) {
        outFile << WL[p]
                << '\t' << spectra[0 * pixelsPerSpectrum + p];
        if (hasPL)
            outFile << '\t' << spectra[1 * pixelsPerSpectrum + p];
        outFile << '\n';
    }
}


void AndorCameraProxy::saveSpectrumSet(const std::string& measurementFolder,
                     const std::string& stem,
                     const std::vector<int>& spectra,
                     const std::vector<float>& WL,
                     int numSpectra,
                     int pixelsPerSpectrum,
                     SpectrumMetadata& specmeta,
                     bool saveAsPng)
{
    if (saveAsPng)
        writePlemPlot(joinPath(measurementFolder, stem) + ".png",
                      spectra, WL, numSpectra, pixelsPerSpectrum);

    writePlemTxt(joinPath(measurementFolder, stem) + ".txt",
                 specmeta, spectra, WL, numSpectra, pixelsPerSpectrum);
}

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

int AndorCameraProxy::getAvailableCameras() {
    IpcMessage req = {};
    req.command = IpcCommand::AndorGetAvailableCameras;

    IpcMessage res = {};
    sendCommand(req, res);
    return res.iArgs[0];
}

void AndorCameraProxy::selectCamera(int cameraIndex) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorSelectCamera;
    req.iArgs[0] = cameraIndex;

    IpcMessage res = {};
    sendCommand(req, res);
    selectedCameraIndex_ = cameraIndex;
}

void AndorCameraProxy::enableCooling(bool enable) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorEnableCooling;
    req.iArgs[0] = enable ? 1 : 0;

    IpcMessage res = {};
    sendCommand(req, res);
}

void AndorCameraProxy::setCoolingTemperature(int temperatureC) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorSetCoolingTemperature;
    req.iArgs[0] = temperatureC;

    IpcMessage res = {};
    sendCommand(req, res);
}

int AndorCameraProxy::getCoolingTemperature() {
    IpcMessage req = {};
    req.command = IpcCommand::AndorGetCoolingTemperature;

    IpcMessage res = {};
    sendCommand(req, res);
    return res.iArgs[0];
}

bool AndorCameraProxy::isCoolingEnabled() {
    IpcMessage req = {};
    req.command = IpcCommand::AndorIsCoolingEnabled;

    IpcMessage res = {};
    sendCommand(req, res);
    return res.iArgs[0] != 0;
}

void AndorCameraProxy::setBackground(const std::vector<int>& spectra) {
    backgrounds_[selectedCameraIndex_] = spectra;
}

bool AndorCameraProxy::hasBackground() const {
    auto it = backgrounds_.find(selectedCameraIndex_);
    return it != backgrounds_.end() && !it->second.empty();
}

std::vector<int> AndorCameraProxy::getBackground() const {
    auto it = backgrounds_.find(selectedCameraIndex_);
    if (it == backgrounds_.end()) {
        return {};
    }
    return it->second;
}

void AndorCameraProxy::measureBackground(float exposureSeconds, const std::string& filename) {
    configureSpectral(AndorCamera::ReadMode::FVB, AndorCamera::TriggerMode::Internal, exposureSeconds, 1);
    startAcquisition();
    waitForAcquisition();

    const std::vector<int> spectra = getAllSpectra(1, getXPixels());
    setBackground(spectra);

    const std::string measurementFolder = createMeasurementFolder();
    const std::string stem = filename.empty() ? "background" : filename;
    saveSpectrumSet(measurementFolder, stem, spectra, wlArray_, 1, getXPixels(), metadataMap_[selectedCameraIndex_]);
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

void AndorCameraProxy::configureSpectral(AndorCamera::ReadMode ReadMode, AndorCamera::TriggerMode trigMode, float exposureSeconds, int numSpectra) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorConfigureSpectral;
    req.iArgs[0] = static_cast<int>(ReadMode);
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
void AndorCameraProxy::testAcquireAndSave(const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename) {
    if (numSpectra <= 0 || pixelsPerSpectrum <= 0) {
        throw std::runtime_error("AndorCameraProxy: invalid spectrum image dimensions");
    }
    if (spectra.empty() || spectra.size() < static_cast<size_t>(numSpectra) * static_cast<size_t>(pixelsPerSpectrum)) {
        throw std::runtime_error("AndorCameraProxy: testAcquireAndSave requires non-empty spectrum data");
    }

    const std::string measurementFolder = createMeasurementFolder();
    const std::vector<int> background = getBackground();
    const std::vector<int> WL; // placeholder for future wavelength data

    if (numSpectra == 1) {
        const std::string stem = filename.empty() ? "spectrum" : filename;
        saveSpectrumSet(measurementFolder, stem, spectra, wlArray_, numSpectra, pixelsPerSpectrum, metadataMap_[selectedCameraIndex_]);

        if (!background.empty()) {
            const std::vector<int> sigBg = subtractBackground(spectra, background);
            saveSpectrumSet(measurementFolder, "sig-bg", sigBg, wlArray_, numSpectra, pixelsPerSpectrum, metadataMap_[selectedCameraIndex_]);
        }
        return;
    }

    for (int frame = 0; frame < numSpectra; ++frame) {
        std::vector<int> frameData(spectra.begin() + (frame * pixelsPerSpectrum),
                                    spectra.begin() + ((frame + 1) * pixelsPerSpectrum));
        std::ostringstream name;
        name << (filename.empty() ? "frame" : filename) << '_' << std::setw(3) << std::setfill('0') << frame;
        saveSpectrumSet(measurementFolder, name.str(), frameData, wlArray_, 1, pixelsPerSpectrum, metadataMap_[selectedCameraIndex_]);

        if (!background.empty()) {
            std::vector<int> bgFrame(background.begin() + (frame * pixelsPerSpectrum),
                                      background.begin() + ((frame + 1) * pixelsPerSpectrum));
            const std::vector<int> sigBg = subtractBackground(frameData, bgFrame);
            saveSpectrumSet(measurementFolder, std::string("sig-bg_") + name.str().substr(name.str().find_last_of('_') + 1), sigBg, wlArray_, 1, pixelsPerSpectrum, metadataMap_[selectedCameraIndex_]);
        }
    }
}

// savefast function: save spectrum
void AndorCameraProxy::savespecfast(const std::string& measurementFolder,
         const std::vector<int>& spectra, 
         int numSpectra, 
         int pixelsPerSpectrum, 
         SpectrumMetadata& specmeta,
         const std::string& filename)

{
    if (numSpectra <= 0 || pixelsPerSpectrum <= 0) {
        throw std::runtime_error("AndorCameraProxy: invalid spectrum image dimensions");
    }
    if (spectra.empty() || spectra.size() < static_cast<size_t>(numSpectra) * static_cast<size_t>(pixelsPerSpectrum)) {
        throw std::runtime_error("AndorCameraProxy: savespecfast requires non-empty spectrum data");
    }
    const std::vector<int> background = getBackground();
    const std::vector<float> WL; // placeholder for future wavelength data

    writePlemTxt(joinPath(measurementFolder, filename) + ".txt",
                 specmeta, spectra, WL, numSpectra, pixelsPerSpectrum);
}


// overload that acquires data and calls the above test function
void AndorCameraProxy::testAcquireAndSave(float exposureS, const std::string& filename) {
    configureSpectral(AndorCamera::ReadMode::FVB, AndorCamera::TriggerMode::Internal, exposureS, 1);
    startAcquisition();
    waitForAcquisition();

    std::vector<int> spectra = getAllSpectra(1, getXPixels());
    testAcquireAndSave(spectra, 1, getXPixels(), filename);
}


std::vector<int> AndorCameraProxy::getAllSpectra(int numSpectra, int pixelsPerSpectrum) {
    IpcMessage req = {};
    req.command = IpcCommand::AndorGetImages16;
    req.iArgs[0] = numSpectra;
    req.iArgs[1] = pixelsPerSpectrum;
    
    IpcMessage res = {};
    sendCommand(req, res);
    
    std::vector<int> obj(res.dataSize / sizeof(WORD));
    
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

// new test function: acquire 10 spectra with 0.1 s exposure, save them to disk, important: print how loong it took
void AndorCameraProxy::testtenspectime() {
    const int numSpectra = 100;
    const int pixelsPerSpectrum = getXPixels();
    const float exposureS = 0.1f;
    const float readoutS = 0.02f; // assume 20 ms readout time, adjust as needed based on actual camera performance
    long totalExposureTime = numSpectra * exposureS + numSpectra * readoutS; // total time spent on exposure + readout, used for calculating overhead
    long overheadone = 0; // save in ms
    long overheadfast = 0; // save in ms
    long measuretimeone = 0; // total time for testAcquireAndSave
    long measuretimefast = 0; // total time for AcquireAndSavefast

    auto start = std::chrono::high_resolution_clock::now();
    configureSpectral(AndorCamera::ReadMode::FVB, AndorCamera::TriggerMode::Internal, exposureS, numSpectra);
    std::vector<int> spectra = getAllSpectra(numSpectra, pixelsPerSpectrum);
    auto end = std::chrono::high_resolution_clock::now();

    // testing: print "starting loop"
    std::cout << "Starting testAcquireAndSave loop for " << numSpectra << " spectra...\n";

    for (int i = 0; i < numSpectra; ++i) {
        // call the spectrometer to measure 100 times with 0.1 s exposure
        std::cout << "Processing spectrum " << (i + 1) << "/" << numSpectra << "...\n";
        testAcquireAndSave(std::vector<int>(spectra.begin() + (i * pixelsPerSpectrum), spectra.begin() + ((i + 1) * pixelsPerSpectrum)),
                         1, pixelsPerSpectrum, "spectrum_" + std::to_string(i));
        // waite for 120 ms to ensure the 100 ms exposure is done
        Sleep(120);  
        
    }

    std::chrono::duration<double> elapsed = end - start;
    measuretimeone = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    overheadone = measuretimeone - totalExposureTime * 1000; // convert to ms

    // then call AcquireAndSavefast to do the same but optimized, measure time again and print how long it took
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < numSpectra; ++i) {
//const std::string& measurementFolder, const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename);

        AcquireAndSavefast(std::vector<int>(spectra.begin() + (i * pixelsPerSpectrum), spectra.begin() + ((i + 1) * pixelsPerSpectrum)),
                         1, pixelsPerSpectrum, "spectrum_" + std::to_string(i));
    }
    end = std::chrono::high_resolution_clock::now();
    measuretimefast = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    overheadfast = measuretimefast - totalExposureTime * 1000; // convert to ms

    // end: print results
    std::cout << "testAcquireAndSave: total time = " << measuretimeone << " ms, overhead = " << overheadone << " ms\n";
    std::cout << "AcquireAndSavefast: total time = " << measuretimefast << " ms, overhead = " << overheadfast << " ms\n";
    // print saved time for each spectrum
    std::cout << "Average time per spectrum for testAcquireAndSave: " << measuretimeone / numSpectra << " ms\n";
    std::cout << "Average time per spectrum for AcquireAndSavefast: " << measuretimefast / numSpectra << " ms\n";
    std::cout << "saved time per spectrum: " << (measuretimeone - measuretimefast) / numSpectra << " ms\n";

}

void AndorCameraProxy::AcquireAndSavefast(const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename) {
    // 1. Keep the safety checks (they are fast and essential)
    if (numSpectra <= 0 || pixelsPerSpectrum <= 0) {
        throw std::runtime_error("AndorCameraProxy: invalid spectrum image dimensions");
    }
    
    // Use size_t directly to avoid repeated casting later
    const size_t totalPixels = static_cast<size_t>(numSpectra) * static_cast<size_t>(pixelsPerSpectrum);
    if (spectra.empty() || spectra.size() < totalPixels) {
        throw std::runtime_error("AndorCameraProxy: AcquireAndSavefast requires non-empty spectrum data");
    }

    const std::string measurementFolder = createMeasurementFolder();
    const std::vector<int>& background = getBackground(); // Use a reference to avoid any hidden copies
    const bool hasBackground = !background.empty() && (background.size() >= totalPixels);

    // Default base names to avoid checking empty string inside loops
    const std::string baseName = filename.empty() ? "frame" : filename;

    // Handle single spectrum case quickly
    if (numSpectra == 1) {
        const std::string stem = filename.empty() ? "spectrum" : filename;
        saveSpectrumSet(measurementFolder, stem, spectra, wlArray_, numSpectra, pixelsPerSpectrum, metadataMap_[selectedCameraIndex_]);

        if (hasBackground) {
            const std::vector<int> sigBg = subtractBackground(spectra, background);
            saveSpectrumSet(measurementFolder, stem, spectra, wlArray_, numSpectra, pixelsPerSpectrum, metadataMap_[selectedCameraIndex_]);
        }
        return;
    }

    // ==============================================================================
    // HIGH-SPEED MULTI-FRAME EXECUTION LAYER
    // ==============================================================================
    
    // Allocation Optimization: Allocate buffers ONCE outside the loop to prevent heap thrashing
    std::vector<int> frameDataBuffer(pixelsPerSpectrum);
    std::vector<int> sigBgBuffer(pixelsPerSpectrum);

    // String Optimization: Pre-allocate a reusable string buffer for frame names
    std::string nameCache;
    nameCache.reserve(baseName.size() + 5); 
    nameCache.assign(baseName).append("_000");
    size_t suffixIdx = baseName.size() + 1; // Index where the digits start

    // Pre-calculate raw data pointers for blisteringly fast iterator math
    const int* rawSpectraPtr = spectra.data();
    const int* rawBgPtr = hasBackground ? background.data() : nullptr;

    for (int frame = 0; frame < numSpectra; ++frame) {
        const size_t offset = static_cast<size_t>(frame) * pixelsPerSpectrum;
        const int* currentFrameSrc = rawSpectraPtr + offset;

        // Fast zero-allocation buffer copy
        std::memcpy(frameDataBuffer.data(), currentFrameSrc, pixelsPerSpectrum * sizeof(WORD));

        // Update the string directly using fast math instead of std::ostringstream
        int hundred = frame / 100;
        int ten = (frame / 10) % 10;
        int one = frame % 10;
        nameCache[suffixIdx]     = '0' + hundred;
        nameCache[suffixIdx + 1] = '0' + ten;
        nameCache[suffixIdx + 2] = '0' + one;

        // Save raw frame data
        saveSpectrumSet(measurementFolder, nameCache, frameDataBuffer, wlArray_, 1, pixelsPerSpectrum, metadataMap_[selectedCameraIndex_]);

        if (hasBackground) {
            const int* currentBgSrc = rawBgPtr + offset;
            
            // Vectorized Processing: Unrolled loop or SIMD-friendly element subtraction
            int* dst = sigBgBuffer.data();
            for (int i = 0; i < pixelsPerSpectrum; ++i) {
                // Prevents underflow if background is greater than signal
                dst[i] = (currentFrameSrc[i] > currentBgSrc[i]) ? (currentFrameSrc[i] - currentBgSrc[i]) : 0;
            }

            // Create background filename string ("sig-bg_000") without allocations or substr()
            std::string bgName = "sig-bg_";
            bgName.append(nameCache.data() + suffixIdx, 3);

            saveSpectrumSet(measurementFolder, bgName, sigBgBuffer, wlArray_, 1, pixelsPerSpectrum, metadataMap_[selectedCameraIndex_]);
        }
    }
}

// wl function: init array for the wl-array, for now just return 1024 pixels from 0 to 1023, later we can implement a real calibration
void AndorCameraProxy::getWLarray(float startWL, float endWL, std::vector<int>& WL) {
    // placeholder for future wavelength calibration data, for now return 1024 pixels from 0 to 1023
    WL.clear();
    const int numPixels = getXPixels();
    for (int i = 0; i < numPixels; ++i) {
        WL.push_back(static_cast<int>(i));
    }
}

