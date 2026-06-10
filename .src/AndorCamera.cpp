// AndorCamera.cpp
#include "AndorCamera.h"
#include <opencv2/opencv.hpp>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <cstdint>
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
        throw std::runtime_error("Andor: failed to resolve executable directory");
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

cv::Mat buildSpectrumImage(const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum, int& maxVal) {
    maxVal = 0;
    for (size_t i = 0; i < spectra.size(); ++i) {
        if (spectra[i] > maxVal) {
            maxVal = spectra[i];
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
        throw std::runtime_error(std::string("Andor: failed to write TXT: ") + filePath);
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

void saveSpectrumSet(const std::string& measurementFolder,
                     const std::string& stem,
                     const std::vector<int>& spectra,
                     const std::vector<float>& WL,
                     int numSpectra,
                     int pixelsPerSpectrum) {
    int maxVal = 0;
    cv::Mat img = buildSpectrumImage(spectra, numSpectra, pixelsPerSpectrum, maxVal);
    if (!cv::imwrite(joinPath(measurementFolder, stem) + ".png", img)) {
        throw std::runtime_error(std::string("Andor: failed to write PNG: ") + stem);
    }
    writeSpectrumTxt(joinPath(measurementFolder, stem) + ".txt", spectra, numSpectra, pixelsPerSpectrum);
}

} // namespace

AndorCamera::AndorCamera() {}

AndorCamera::~AndorCamera() {
    shutdown();
    if (hDll_) {
        FreeLibrary(hDll_);
    }
}

template<typename T>
T AndorCamera::loadProc(const char* name) {
    T fp = reinterpret_cast<T>(GetProcAddress(hDll_, name));
    if (!fp) throw std::runtime_error(std::string("Andor: cannot find ") + name);
    return fp;
}

void AndorCamera::check(unsigned int ret, const char* ctx) {
    if (ret != DRV_SUCCESS)
        throw std::runtime_error(std::string("Andor error in ") + ctx +
                                 ": code " + std::to_string(ret));
}

void AndorCamera::loadDLL(const std::string& dllPath) {
    hDll_ = LoadLibraryA(dllPath.c_str());
    if (!hDll_) throw std::runtime_error("Cannot load Andor DLL");

    pInitialize          = loadProc<FP_Initialize>         ("Initialize");
    pGetAvailableCameras = loadProc<FP_GetAvailableCameras>("GetAvailableCameras");
    pGetCameraHandle     = loadProc<FP_GetCameraHandle>    ("GetCameraHandle");
    pSetCurrentCamera    = loadProc<FP_SetCurrentCamera>   ("SetCurrentCamera");
    pGetDetector         = loadProc<FP_GetDetector>        ("GetDetector");
    pSetReadMode         = loadProc<FP_SetReadMode>        ("SetReadMode");
    pSetAcquisitionMode  = loadProc<FP_SetAcquisitionMode> ("SetAcquisitionMode");
    pSetExposureTime     = loadProc<FP_SetExposureTime>    ("SetExposureTime");
    pSetTriggerMode      = loadProc<FP_SetTriggerMode>     ("SetTriggerMode");
    pCoolerON            = loadProc<FP_CoolerON>           ("CoolerON");
    pCoolerOFF           = loadProc<FP_CoolerOFF>          ("CoolerOFF");
    pSetTemperature      = loadProc<FP_SetTemperature>     ("SetTemperature");
    pGetTemperature      = loadProc<FP_GetTemperature>     ("GetTemperature");
    pIsCoolerOn          = loadProc<FP_IsCoolerOn>         ("IsCoolerOn");
    pSetImage            = loadProc<FP_SetImage>           ("SetImage");
    pStartAcquisition    = loadProc<FP_StartAcquisition>   ("StartAcquisition");
    pAbortAcquisition    = loadProc<FP_AbortAcquisition>   ("AbortAcquisition");
    pWaitForAcquisition  = loadProc<FP_WaitForAcquisition> ("WaitForAcquisition");
    pGetAcquiredData16   = loadProc<FP_GetAcquiredData16>  ("GetAcquiredData16");
    pGetStatus           = loadProc<FP_GetStatus>          ("GetStatus");
    pShutDown            = loadProc<FP_ShutDown>           ("ShutDown");
    pSetKineticCycleTime = loadProc<FP_SetKineticCycleTime>("SetKineticCycleTime");
    pSetNumberKinetics   = loadProc<FP_SetNumberKinetics>  ("SetNumberKinetics");
    pGetImages16         = loadProc<FP_GetImages16>        ("GetImages16");
}

void AndorCamera::initialize(const std::string& iniDir) {
    ensureLoaded();
    char dir[512] = {};
    strncpy_s(dir, iniDir.empty() ? "." : iniDir.c_str(), 511);
    check(pInitialize(dir), "Initialize");
    check(pGetDetector(&xpix_, &ypix_), "GetDetector");
    std::cout << "Andor: " << xpix_ << " x " << ypix_ << " pixels\n";
}

int AndorCamera::getAvailableCameras() {
    ensureLoaded();
    long total = 0;
    check(pGetAvailableCameras(&total), "GetAvailableCameras");
    availableCameras_ = total;
    return static_cast<int>(total);
}

void AndorCamera::selectCamera(int cameraIndex) {
    ensureLoaded();
    if (cameraIndex < 0) {
        throw std::runtime_error("Andor: camera index must be non-negative");
    }

    long total = availableCameras_;
    if (total <= 0) {
        total = getAvailableCameras();
    }
    if (cameraIndex >= total) {
        throw std::runtime_error(std::string("Andor: camera index out of range: ") + std::to_string(cameraIndex));
    }

    long handle = 0;
    check(pGetCameraHandle(cameraIndex, &handle), "GetCameraHandle");
    check(pSetCurrentCamera(handle), "SetCurrentCamera");

    selectedCameraIndex_ = cameraIndex;
    selectedCameraHandle_ = handle;
}

void AndorCamera::enableCooling(bool enable) {
    ensureLoaded();
    if (enable) {
        check(pCoolerON(), "CoolerON");
    } else {
        check(pCoolerOFF(), "CoolerOFF");
    }
}

void AndorCamera::setBackground(const std::vector<int>& spectra) {
    ensureLoaded();
    backgrounds_[selectedCameraIndex_] = spectra;
}

bool AndorCamera::hasBackground() const {
    auto it = backgrounds_.find(selectedCameraIndex_);
    return it != backgrounds_.end() && !it->second.empty();
}

std::vector<int> AndorCamera::getBackground() const {
    auto it = backgrounds_.find(selectedCameraIndex_);
    if (it == backgrounds_.end()) {
        return {};
    }
    return it->second;
}

void AndorCamera::setCoolingTemperature(int temperatureC) {
    ensureLoaded();
    check(pSetTemperature(temperatureC), "SetTemperature");
}

int AndorCamera::getCoolingTemperature() {
    ensureLoaded();
    int temperature = 0;
    check(pGetTemperature(&temperature), "GetTemperature");
    return temperature;
}

bool AndorCamera::isCoolingEnabled() {
    ensureLoaded();
    int coolerOn = 0;
    check(pIsCoolerOn(&coolerOn), "IsCoolerOn");
    return coolerOn != 0;
}

void AndorCamera::setReadMode(int mode) {
    ensureLoaded();
    check(pSetReadMode(mode), "SetReadMode");
}

void AndorCamera::setAcquisitionMode(int mode) {
    ensureLoaded();
    check(pSetAcquisitionMode(mode), "SetAcquisitionMode");
}

void AndorCamera::setExposureTime(float time) {
    ensureLoaded();
    check(pSetExposureTime(time), "SetExposureTime");
}

void AndorCamera::setTriggerMode(int mode) {
    ensureLoaded();
    check(pSetTriggerMode(mode), "SetTriggerMode");
}

void AndorCamera::setImage(int hbin, int vbin, int hstart, int hend, int vstart, int vend) {
    ensureLoaded();
    check(pSetImage(hbin, vbin, hstart, hend, vstart, vend), "SetImage");
}

int AndorCamera::getStatus() {
    ensureLoaded();
    int status = 0;
    check(pGetStatus(&status), "GetStatus");
    return status;
}

void AndorCamera::setKineticCycleTime(float time) {
    ensureLoaded();
    check(pSetKineticCycleTime(time), "SetKineticCycleTime");
}

void AndorCamera::setNumberKinetics(int number) {
    ensureLoaded();
    check(pSetNumberKinetics(number), "SetNumberKinetics");
}

void AndorCamera::configureSpectral(ReadMode readMode, TriggerMode trigMode, float exposureSeconds, int numSpectra) {
    ensureLoaded();
    check(pSetReadMode(static_cast<int>(readMode)), "SetReadMode");
    check(pSetAcquisitionMode(numSpectra > 1 ? 3 : 1), "SetAcquisitionMode"); // 3 = kinetic, 1 = single scan
    check(pSetExposureTime(exposureSeconds), "SetExposureTime");
    
    if (numSpectra > 1) {
        check(pSetNumberKinetics(numSpectra), "SetNumberKinetics");
        check(pSetKineticCycleTime(0.0f), "SetKineticCycleTime");
    }
    
    check(pSetTriggerMode(static_cast<int>(trigMode)), "SetTriggerMode");
}

void AndorCamera::configureFVBKinetic(float exposureSeconds, int numLines) {
    ensureLoaded();
    // Full Vertical Binning: collapses all rows → 1D spectrum per exposure
    // Kinetic mode: numLines exposures, each triggered by one TTL pulse
    check(pSetReadMode(0),              "SetReadMode FVB");
    check(pSetAcquisitionMode(3),       "SetAcquisitionMode Kinetic");
    check(pSetExposureTime(exposureSeconds), "SetExposureTime");
    check(pSetKineticCycleTime(0.0f),   "SetKineticCycleTime 0=min");
    check(pSetNumberKinetics(numLines), "SetNumberKinetics");
    // Fast External: one TTL rising edge = one acquisition
    check(pSetTriggerMode(7),           "SetTriggerMode FastExternal");
}

void AndorCamera::startAcquisition() {
    ensureLoaded();
    check(pStartAcquisition(), "StartAcquisition");
}

void AndorCamera::abortAcquisition() {
    ensureLoaded();
    pAbortAcquisition();    // ignore error on abort
}

void AndorCamera::waitForAcquisition() {
    ensureLoaded();
    check(pWaitForAcquisition(), "WaitForAcquisition");
}

std::vector<int> AndorCamera::getAllSpectra(int numSpectra, int pixelsPerSpectrum) {
    ensureLoaded();
    std::vector<int> buf(numSpectra * pixelsPerSpectrum);
    long vf, vl;
    check(pGetImages16(1, numSpectra, buf.data(),
                       (unsigned long)buf.size(), &vf, &vl),
          "GetImages16");
    return buf;
}

void AndorCamera::shutdown() {
    if (hDll_) { pShutDown(); }
}

void AndorCamera::ensureLoaded() {
    if (hDll_) {
        return;
    }

    loadDLL("atmcd64d.dll");
}

void AndorCamera::testAcquireAndSave(const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename) {
    if (numSpectra <= 0 || pixelsPerSpectrum <= 0) {
        throw std::runtime_error("Andor: invalid spectrum image dimensions");
    }
    if (spectra.empty() || spectra.size() < static_cast<size_t>(numSpectra) * static_cast<size_t>(pixelsPerSpectrum)) {
        throw std::runtime_error("Andor: testAcquireAndSave requires non-empty spectrum data");
    }

    const std::string measurementFolder = createMeasurementFolder();

    std::cout << "OpenCV Build Information:\n" << cv::getBuildInformation() << "\n";

    const std::vector<int> background = getBackground();

    if (numSpectra == 1) {
        const std::string stem = filename.empty() ? "spectrum" : filename;
        saveSpectrumSet(measurementFolder, stem, spectra, wlArray_, numSpectra, pixelsPerSpectrum, specmeta[selectedCameraIndex_]);

        if (!background.empty()) {
            const std::vector<int> sigBg = subtractBackground(spectra, background);
            saveSpectrumSet(measurementFolder, "sig-bg", sigBg, wlArray_, numSpectra, pixelsPerSpectrum, specmeta[selectedCameraIndex_]);
        }

        std::cout << "Saved spectrum to " << measurementFolder << "\n";
        return;
    }

    for (int frame = 0; frame < numSpectra; ++frame) {
        std::vector<int> frameData(spectra.begin() + (frame * pixelsPerSpectrum),
                                    spectra.begin() + ((frame + 1) * pixelsPerSpectrum));

        std::ostringstream name;
        name << (filename.empty() ? "frame" : filename) << '_' << std::setw(3) << std::setfill('0') << frame;
        saveSpectrumSet(measurementFolder, name.str(), frameData, wlArray_, 1, pixelsPerSpectrum, specmeta[selectedCameraIndex_]);

        if (!background.empty()) {
            std::vector<int> bgFrame(background.begin() + (frame * pixelsPerSpectrum),
                                      background.begin() + ((frame + 1) * pixelsPerSpectrum));
            const std::vector<int> sigBg = subtractBackground(frameData, bgFrame);
            saveSpectrumSet(measurementFolder, std::string("sig-bg_") + name.str().substr(name.str().find_last_of('_') + 1), sigBg, wlArray_, 1, pixelsPerSpectrum, specmeta[selectedCameraIndex_]);
        }
    }

    std::cout << "Saved kinetic spectra to " << measurementFolder << "\n";

}

void AndorCamera::measureBackground(float exposureSeconds, const std::string& filename) {
    configureSpectral(ReadMode::FVB, TriggerMode::Internal, exposureSeconds);
    startAcquisition();
    waitForAcquisition();

    const std::vector<int> spectra = getAllSpectra(1, getXPixels());
    setBackground(spectra);

    const std::string measurementFolder = createMeasurementFolder();
    const std::string stem = filename.empty() ? "background" : filename;
    saveSpectrumSet(measurementFolder, stem, spectra, wlArray_, 1, getXPixels(), specmeta[selectedCameraIndex_]);
    std::cout << "Saved background to " << measurementFolder << "\n";
}

void AndorCamera::getWLarray(float startWL, float endWL, std::vector<float>& WL) {
    // placeholder for future wavelength calibration data, for now return 1024 pixels from 0 to 1023
    WL.clear();
    const int numPixels = getXPixels();
    for (int i = 0; i < numPixels; ++i) {
        WL.push_back(static_cast<int>(i));
    }

}

void AndorCamera::setWLarray(std::vector<float>& WL) {
    wlStart_ = WL.empty() ? 0 : WL.front();
    wlEnd_ = WL.empty() ? 0 : WL.back();
    wlNumPoints_ = static_cast<float>(WL.size());
    
}
    
    
