// AndorCamera.cpp
#include "AndorCamera.h"
#include <opencv2/opencv.hpp>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <chrono>
#include <thread>
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

} // namespace

const char* AndorCamera::readModeToString(Andor::ReadMode mode) {
    switch (mode) {
        case Andor::ReadMode::FVB: return "FVB";
        case Andor::ReadMode::MultiTrack: return "MultiTrack";
        case Andor::ReadMode::RandomTrack: return "RandomTrack";
        case Andor::ReadMode::SingleTrack: return "SingleTrack";
        case Andor::ReadMode::FullImage: return "FullImage";
        default: return "Unknown";
    }
}

const char* AndorCamera::triggerModeToString(Andor::TriggerMode mode) {
    switch (mode) {
        case Andor::TriggerMode::Internal: return "Internal";
        case Andor::TriggerMode::External: return "External";
        case Andor::TriggerMode::ExternalStart: return "ExternalStart";
        case Andor::TriggerMode::FastExternal: return "ExternalExposure";
        case Andor::TriggerMode::Software: return "Software";
        default: return "Unknown";
    }
}

const char* AndorCamera::CameraNtoName(int cameraN) {
    switch (cameraN) {
        case 0: return "Newton";
        case 1: return "Clara";
        case 2: return "Idus";
        default: return "Unknown Camera";
    }
}

std::string AndorCamera::TranslateCameraErrorToString(int status) {
    switch (static_cast<CameraErrorToString>(status)) {
        case Andor::CameraErrorToString::Success:                    return "DRV_SUCCESS";
        case Andor::CameraErrorToString::VxdNotInstalled:            return "DRV_VXDNOTINSTALLED";
        case Andor::CameraErrorToString::ErrorScan:                  return "DRV_ERROR_SCAN";
        case Andor::CameraErrorToString::ErrorCheckSum:              return "DRV_ERROR_CHECK_SUM";
        case Andor::CameraErrorToString::ErrorFileload:              return "DRV_ERROR_FILELOAD";
        case Andor::CameraErrorToString::UnknownFunction:            return "DRV_UNKNOWN_FUNCTION";
        case Andor::CameraErrorToString::ErrorVxdInit:               return "DRV_ERROR_VXD_INIT";
        case Andor::CameraErrorToString::ErrorAddress:               return "DRV_ERROR_ADDRESS";
        case Andor::CameraErrorToString::ErrorPagelock:              return "DRV_ERROR_PAGELOCK";
        case Andor::CameraErrorToString::ErrorPageUnlock:            return "DRV_ERROR_PAGE_UNLOCK";
        case Andor::CameraErrorToString::ErrorBoardtest:             return "DRV_ERROR_BOARDTEST";
        case Andor::CameraErrorToString::ErrorAck:                   return "DRV_ERROR_ACK";
        case Andor::CameraErrorToString::ErrorUpFifo:                return "DRV_ERROR_UP_FIFO";
        case Andor::CameraErrorToString::ErrorPattern:               return "DRV_ERROR_PATTERN";
        case Andor::CameraErrorToString::AcquisitionErrors:          return "DRV_ACQUISITION_ERRORS";
        case Andor::CameraErrorToString::AcqBuffer:                  return "DRV_ACQ_BUFFER";
        case Andor::CameraErrorToString::AcqDownfifoFull:            return "DRV_ACQ_DOWNFIFO_FULL";
        case Andor::CameraErrorToString::ProcUnknownInstruction:     return "DRV_PROC_UNKNOWN_INSTRUCTION";
        case Andor::CameraErrorToString::IllegalOpCode:              return "DRV_ILLEGAL_OP_CODE";
        case Andor::CameraErrorToString::KineticTimeNotMet:          return "DRV_KINETIC_TIME_NOT_MET";
        case Andor::CameraErrorToString::AccumTimeNotMet:            return "DRV_ACCUM_TIME_NOT_MET";
        case Andor::CameraErrorToString::NoNewData:                  return "DRV_NO_NEW_DATA";
        case Andor::CameraErrorToString::PciDmaFail:                 return "PCI_DMA_FAIL";
        case Andor::CameraErrorToString::SpoolError:                 return "DRV_SPOOLERROR";
        case Andor::CameraErrorToString::SpoolSetupError:            return "DRV_SPOOLSETUPERROR";
        case Andor::CameraErrorToString::Saturated:                  return "SATURATED";
        case Andor::CameraErrorToString::TemperatureOff:             return "DRV_TEMPERATURE_OFF";
        case Andor::CameraErrorToString::TempNotStabilized:          return "DRV_TEMP_NOT_STABILIZED";
        case Andor::CameraErrorToString::TemperatureStabilized:      return "DRV_TEMPERATURE_STABILIZED";
        case Andor::CameraErrorToString::TemperatureNotReached:      return "DRV_TEMPERATURE_NOT_REACHED";
        case Andor::CameraErrorToString::TemperatureOutRange:        return "DRV_TEMPERATURE_OUT_RANGE";
        case Andor::CameraErrorToString::TemperatureNotSupported:    return "DRV_TEMPERATURE_NOT_SUPPORTED";
        case Andor::CameraErrorToString::TemperatureDrift:           return "DRV_TEMPERATURE_DRIFT";
        case Andor::CameraErrorToString::InvalidAux:                 return "DRV_INVALID_AUX";
        case Andor::CameraErrorToString::CofNotLoaded:               return "DRV_COF_NOTLOADED";
        case Andor::CameraErrorToString::FpgaProg:                   return "DRV_FPGAPROG";
        case Andor::CameraErrorToString::FlexError:                  return "DRV_FLEXERROR";
        case Andor::CameraErrorToString::GpibError:                  return "DRV_GPIBERROR";
        case Andor::CameraErrorToString::ErrorDmaUpload:             return "ERROR_DMA_UPLOAD";
        case Andor::CameraErrorToString::Datatype:                   return "DRV_DATATYPE";
        case Andor::CameraErrorToString::P1Invalid:                  return "DRV_P1INVALID";
        case Andor::CameraErrorToString::P2Invalid:                  return "DRV_P2INVALID";
        case Andor::CameraErrorToString::P3Invalid:                  return "DRV_P3INVALID";
        case Andor::CameraErrorToString::P4Invalid:                  return "DRV_P4INVALID";
        case Andor::CameraErrorToString::IniError:                   return "DRV_INIERROR";
        case Andor::CameraErrorToString::CofError:                   return "DRV_COFERROR";
        case Andor::CameraErrorToString::Acquiring:                  return "DRV_ACQUIRING";
        case Andor::CameraErrorToString::Idle:                       return "DRV_IDLE";
        case Andor::CameraErrorToString::TempCycle:                  return "DRV_TEMPCYCLE";
        case Andor::CameraErrorToString::NotInitialized:             return "DRV_NOT_INITIALIZED";
        case Andor::CameraErrorToString::P5Invalid:                  return "DRV_P5INVALID";
        case Andor::CameraErrorToString::P6Invalid:                  return "DRV_P6INVALID";
        case Andor::CameraErrorToString::InvalidMode:                return "DRV_INVALID_MODE";
        case Andor::CameraErrorToString::InvalidFilter:              return "DRV_INVALID_FILTER";
        case Andor::CameraErrorToString::I2cErrors:                  return "DRV_I2CERRORS";
        case Andor::CameraErrorToString::I2cDevNotFound:             return "DRV_DRV_I2CDEVNOTFOUND";
        case Andor::CameraErrorToString::I2cTimeout:                 return "DRV_I2CTIMEOUT";
        case Andor::CameraErrorToString::P7Invalid:                  return "DRV_P7INVALID";
        case Andor::CameraErrorToString::UsbError:                   return "DRV_USBERROR";
        case Andor::CameraErrorToString::IocError:                   return "DRV_IOCERROR";
        case Andor::CameraErrorToString::VrmVersionError:            return "DRV_VRMVERSIONERROR";
        case Andor::CameraErrorToString::UsbInterruptEndpointError:  return "DRV_USB_INTERRUPT_ENDPOINT_ERROR";
        case Andor::CameraErrorToString::RandomTrackError:           return "DRV_RANDOM_TRACK_ERROR";
        case Andor::CameraErrorToString::InvalidTriggerMode:         return "DRV_INVALID_TRIGGER_MODE";
        case Andor::CameraErrorToString::LoadFirmwareError:          return "DRV_LOAD_FIRMWARE_ERROR";
        case Andor::CameraErrorToString::DivideByZeroError:          return "DRV_DIVIDE_BY_ZERO_ERROR";
        case Andor::CameraErrorToString::InvalidRingExposures:       return "DRV_INVALID_RINGEXPOSURES";
        case Andor::CameraErrorToString::BinningError:               return "DRV_BINNING_ERROR";
        case Andor::CameraErrorToString::InvalidAmplifier:           return "DRV_INVALID_AMPLIFIER";
        case Andor::CameraErrorToString::InvalidCountconvertMode:    return "DRV_INVALID_COUNTCONVERT_MODE";
        case Andor::CameraErrorToString::ErrorMap:                   return "DRV_ERROR_MAP";
        case Andor::CameraErrorToString::ErrorUnmap:                 return "DRV_ERROR_UNMAP";
        case Andor::CameraErrorToString::ErrorMdl:                   return "DRV_ERROR_MDL";
        case Andor::CameraErrorToString::ErrorUnmdl:                 return "DRV_ERROR_UNMDL";
        case Andor::CameraErrorToString::ErrorBuffsize:              return "DRV_ERROR_BUFFSIZE";
        case Andor::CameraErrorToString::ErrorNoHandle:              return "DRV_ERROR_NOHANDLE";
        case Andor::CameraErrorToString::GatingNotAvailable:         return "DRV_GATING_NOT_AVAILABLE";
        case Andor::CameraErrorToString::FpgaVoltageError:           return "DRV_FPGA_VOLTAGE_ERROR";
        case Andor::CameraErrorToString::ErrorNoCamera:              return "DRV_ERROR_NOCAMERA";
        case Andor::CameraErrorToString::NotSupported:               return "DRV_NOT_SUPPORTED";
        case Andor::CameraErrorToString::NotAvailable:               return "DRV_NOT_AVAILABLE";
        default:                                      return "Unknown";
    }
}

void AndorCamera::saveSpectrumSet(const std::string& measurementFolder,
                     const std::string& stem,
                     const std::vector<int>& spectra,
                     const std::vector<float>& WL,
                     int numSpectra,
                     int pixelsPerSpectrum, 
                     const SpectrumMetadata& specmeta,
                     bool saveAsPng) {
    int maxVal = 0;
    cv::Mat img = buildSpectrumImage(spectra, numSpectra, pixelsPerSpectrum, maxVal);
    if (saveAsPng) {
        if (!cv::imwrite(joinPath(measurementFolder, stem) + ".png", img)) {
            throw std::runtime_error(std::string("Andor: failed to write PNG: ") + stem);
        }
    }
    writeSpectrumTxt(joinPath(measurementFolder, stem) + ".txt", spectra, numSpectra, pixelsPerSpectrum);
}

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
    pGetTotalNumberImagesAcquired = loadProc<FP_GetTotalNumberImagesAcquired>("GetTotalNumberImagesAcquired");
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

    // Keep per-camera metadata available even before any UI updates.
    if (metadataMap_.find(selectedCameraIndex_) == metadataMap_.end()) {
        metadataMap_[selectedCameraIndex_] = SpectrumMetadata{};
    }
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
    currentMetadata().exposureTimeS = static_cast<double>(time);
}

void AndorCamera::setTriggerMode(int mode) {
    ensureLoaded();
    check(pSetTriggerMode(mode), "SetTriggerMode");
    currentMetadata().triggerMode = static_cast<TriggerMode>(mode);
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

int AndorCamera::getTotalNumberImagesAcquired() {
    ensureLoaded();
    long total = 0;
    check(pGetTotalNumberImagesAcquired(&total), "GetTotalNumberImagesAcquired");
    return static_cast<int>(total);
}

void AndorCamera::setKineticCycleTime(float time) {
    ensureLoaded();
    check(pSetKineticCycleTime(time), "SetKineticCycleTime");
}

void AndorCamera::setNumberKinetics(int number) {
    ensureLoaded();
    check(pSetNumberKinetics(number), "SetNumberKinetics");
}

void AndorCamera::configureSpectral(ReadMode ReadMode, TriggerMode trigMode, float exposureSeconds, int numSpectra) {
    ensureLoaded();
    check(pSetReadMode(static_cast<int>(ReadMode)), "SetReadMode");
    check(pSetAcquisitionMode(numSpectra > 1 ? 3 : 1), "SetAcquisitionMode"); // 3 = kinetic, 1 = single scan
    check(pSetExposureTime(exposureSeconds), "SetExposureTime");
    
    if (numSpectra > 1) {
        check(pSetNumberKinetics(numSpectra), "SetNumberKinetics");
        check(pSetKineticCycleTime(0.000f), "SetKineticCycleTime");
    }
    
    check(pSetTriggerMode(static_cast<int>(trigMode)), "SetTriggerMode");

    SpectrumMetadata& meta = currentMetadata();
    meta.ReadMode = ReadMode;
    meta.triggerMode = trigMode;
    meta.exposureTimeS = static_cast<double>(exposureSeconds);
}

void AndorCamera::configureFVBKinetic(float exposureSeconds, int numLines) {
    ensureLoaded();
    // Full Vertical Binning: collapses all rows → 1D spectrum per exposure
    // Kinetic mode: numLines exposures, each triggered by one TTL pulse
    check(pSetReadMode(0),              "SetReadMode FVB");
    check(pSetAcquisitionMode(3),       "SetAcquisitionMode Kinetic");
    check(pSetExposureTime(exposureSeconds), "SetExposureTime");
    check(pSetKineticCycleTime(0.00000f),   "SetKineticCycleTime 0=min");
    check(pSetNumberKinetics(numLines), "SetNumberKinetics");
    // Fast External: one TTL rising edge = one acquisition
    check(pSetTriggerMode(7),           "SetTriggerMode FastExternal");

    SpectrumMetadata& meta = currentMetadata();
    meta.ReadMode = ReadMode::FVB;
    meta.triggerMode = TriggerMode::FastExternal;
    meta.exposureTimeS = static_cast<double>(exposureSeconds);
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

/* old version of getAllSpectra 
std::vector<int> AndorCamera::getAllSpectra(int numSpectra, int pixelsPerSpectrum) {
    ensureLoaded();
    std::vector<int> buf(numSpectra * pixelsPerSpectrum);
    long vf, vl;
    check(pGetImages16(1, numSpectra, buf.data(),
                       (unsigned long)buf.size(),
                        &vf, &vl),
          "GetImages16");
    return buf;
}*/
std::vector<int> AndorCamera::getAllSpectra(int numSpectra, int pixelsPerSpectrum) {
    ensureLoaded();

    // Guard: confirm all frames actually arrived
    long acquired = 0;
    pGetTotalNumberImagesAcquired(&acquired);
    if (acquired < numSpectra) {
        throw std::runtime_error(
            "GetImages16: only " + std::to_string(acquired) +
            " of " + std::to_string(numSpectra) + " frames acquired");
    }

    const size_t totalPixels = static_cast<size_t>(numSpectra) * pixelsPerSpectrum;
    std::vector<unsigned short> buf(totalPixels);
    long vf = 0, vl = 0;
    check(pGetImages16(1, numSpectra, buf.data(),
                       static_cast<unsigned long>(totalPixels),
                       &vf, &vl),
          "GetImages16");

    // Warn if SDK returned fewer valid frames than requested
    if (vf != 1 || vl != numSpectra) {
        throw std::runtime_error(
            "GetImages16: requested frames 1-" + std::to_string(numSpectra) +
            " but got valid range " + std::to_string(vf) + "-" + std::to_string(vl));
    }

    return std::vector<int>(buf.begin(), buf.end());
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
    SpectrumMetadata& meta = currentMetadata();

    if (numSpectra == 1) {
        const std::string stem = filename.empty() ? "spectrum" : filename;
        saveSpectrumSet(measurementFolder, stem, spectra, wlArray_, numSpectra, pixelsPerSpectrum, meta);

        if (!background.empty()) {
            const std::vector<int> sigBg = subtractBackground(spectra, background);
            saveSpectrumSet(measurementFolder, "sig-bg", sigBg, wlArray_, numSpectra, pixelsPerSpectrum, meta);
        }

        std::cout << "Saved spectrum to " << measurementFolder << "\n";
        return;
    }

    for (int frame = 0; frame < numSpectra; ++frame) {
        std::vector<int> frameData(spectra.begin() + (frame * pixelsPerSpectrum),
                                    spectra.begin() + ((frame + 1) * pixelsPerSpectrum));

        std::ostringstream name;
        name << (filename.empty() ? "frame" : filename) << '_' << std::setw(3) << std::setfill('0') << frame;
        saveSpectrumSet(measurementFolder, name.str(), frameData, wlArray_, 1, pixelsPerSpectrum, meta);

        if (!background.empty()) {
            std::vector<int> bgFrame(background.begin() + (frame * pixelsPerSpectrum),
                                      background.begin() + ((frame + 1) * pixelsPerSpectrum));
            const std::vector<int> sigBg = subtractBackground(frameData, bgFrame);
            saveSpectrumSet(measurementFolder, std::string("sig-bg_") + name.str().substr(name.str().find_last_of('_') + 1), sigBg, wlArray_, 1, pixelsPerSpectrum, meta);
        }
    }

    std::cout << "Saved kinetic spectra to " << measurementFolder << "\n";

}

void AndorCamera::Savefast(const std::string& foldername, const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename) {
    if (numSpectra <= 0 || pixelsPerSpectrum <= 0) {
        throw std::runtime_error("Andor: invalid spectrum image dimensions");
    }
    if (spectra.empty() || spectra.size() < static_cast<size_t>(numSpectra) * static_cast<size_t>(pixelsPerSpectrum)) {
        throw std::runtime_error("Andor: AcquireAndSavefast requires non-empty spectrum data");
    }

    const std::string measurementFolder = joinPath(executableDirectory(), foldername);
    ensureDirectoryExists(measurementFolder);
    const std::vector<int> background = getBackground();
    // write spectrum to .txt
    const std::string txtFilePath = joinPath(measurementFolder, filename + ".txt");
    writeSpectrumTxt(txtFilePath, spectra, numSpectra, pixelsPerSpectrum);
}    

void AndorCamera::setupfastAcquisition(float exposureSeconds, int numSpectra) {
    configureSpectral(ReadMode::FVB, TriggerMode::FastExternal, exposureSeconds, numSpectra);
}

void AndorCamera::runfastAcquistiontriggered(float exposureSeconds, int numSpectra, std::string filename, std::string foldername) {
    for (int i = 0; i < numSpectra; ++i) {
        int timewaited = 0;
        int finished = 0;
        int statusMsg = 0;
        startAcquisition();
        // waite for integration time + 20 ms to ensure acquisition is complete before next trigger
        std::chrono::milliseconds waitTime(static_cast<int>(exposureSeconds * 1000) + 20);
        std::this_thread::sleep_for(waitTime);
        statusMsg = getStatus();
        while (statusMsg == 20002 and timewaited > exposureSeconds) { // DRV_ACQUIRING
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            timewaited += 20;
            if (timewaited > exposureSeconds){
                finished = 2;
            }
            statusMsg = getStatus();
        }
        if (finished == 1){
            // save spectrum
            const std::vector<int> spectra = getAllSpectra(1, getXPixels());
            //(const std::string& foldername, const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename)
            Savefast(foldername, spectra, 1, getXPixels(), filename + "_" + std::to_string(i));

        } else if (finished == 2) {
            std::cerr << "Andor: warning - acquisition did not finish within expected time after trigger\n";
        } else if (statusMsg != 20073) { // DRV_IDLE
            std::cerr << "Andor: warning - acquisition finished with unexpected status: " << statusMsg << "\n";
        } // here add more cases if needed to handle other status codes



        
    }
    
}


void AndorCamera::measureBackground(float exposureSeconds, const std::string& filename) {
    configureSpectral(ReadMode::FVB, TriggerMode::Internal, exposureSeconds);
    startAcquisition();
    waitForAcquisition();

    const std::vector<int> spectra = getAllSpectra(1, getXPixels());
    setBackground(spectra);

    const std::string measurementFolder = createMeasurementFolder();
    const std::string stem = filename.empty() ? "background" : filename;
    saveSpectrumSet(measurementFolder, stem, spectra, wlArray_, 1, getXPixels(), currentMetadata());
    std::cout << "Saved background to " << measurementFolder << "\n";
}

SpectrumMetadata& AndorCamera::currentMetadata() {
    auto it = metadataMap_.find(selectedCameraIndex_);
    if (it == metadataMap_.end()) {
        it = metadataMap_.emplace(selectedCameraIndex_, SpectrumMetadata{}).first;
    }
    return it->second;
}

const SpectrumMetadata& AndorCamera::currentMetadata() const {
    auto it = metadataMap_.find(selectedCameraIndex_);
    if (it == metadataMap_.end()) {
        static const SpectrumMetadata empty{};
        return empty;
    }
    return it->second;
}

SpectrumMetadata AndorCamera::getMetadata() const {
    return currentMetadata();
}

void AndorCamera::setMetadata(const SpectrumMetadata& metadata) {
    metadataMap_[selectedCameraIndex_] = metadata;
}

void AndorCamera::getWLarray(float startWL, float endWL, std::vector<float>& WL) {
    // placeholder for future wavelength calibration data, for now return 1024 pixels from 0 to 1023
    WL.clear();
    const int numPixels = getXPixels();
    for (int i = 0; i < numPixels; ++i) {
        WL.push_back(static_cast<int>(i));
    }
}

void AndorCamera::AcquireAndFetchSingle(int numPixels, std::vector<int>& spectrum, SpectrumMetadata& metadata) {
    startAcquisition();
    waitForAcquisition();
    spectrum = getAllSpectra(1, numPixels);
    metadata = currentMetadata();
}

void AndorCamera::setWLarray(std::vector<float>& WL) {
    wlStart_ = WL.empty() ? 0 : WL.front();
    wlEnd_ = WL.empty() ? 0 : WL.back();
    wlNumPoints_ = static_cast<float>(WL.size());
    wlArray_.assign(WL.begin(), WL.end());
}

void AndorCamera::measureandsaveNspecs(const std::string& foldername, int nspecs) {    
    const std::string measurementFolder = joinPath(executableDirectory(), foldername);
    ensureDirectoryExists(measurementFolder);

    // save startingtime as tstart
    const auto tstart = std::chrono::steady_clock::now();   
    std::cout << "Starting acquisition of " << nspecs << " spectra...\n";     

    /* we can do this faster 93 ms is too slow
    // this test: measure and save in a for loop
    for (int i = 0; i < nspecs; ++i) {
        startAcquisition();
        waitForAcquisition();

        // save spectrum in seperate thread to avoid blocking acquisition of next spectrum
        // pass mearuement folder, filename, and spectrum data to thread, so each spec is saved in the same folder
        std::thread saveThread([this, measurementFolder, i]()        {
            const std::vector<int> spectra = getAllSpectra(1, getXPixels());
            const std::string stem = "HSI_" + std::to_string(i);
            saveSpectrumSet(measurementFolder, stem, spectra, wlArray_, 1, getXPixels(), currentMetadata());
            
        });
        saveThread.join();
    }*/
   /*still too slow, this takes 93 ms overhead. lets to this: set the trigger to internal and the mode do kinetic, then we can trigger the acquisition and save the data in a separate thread. This way we can start the next acquisition while the previous one is being saved.
    
    std::thread prevSaveThread;  // declare before the loop

    for (int i = 0; i < nspecs; ++i) {
        // wait for previous save to finish before overwriting the buffer
        if (prevSaveThread.joinable())
            prevSaveThread.join();

        startAcquisition();
        waitForAcquisition();

        // capture data immediately into a local copy
        std::vector<int> spectra = getAllSpectra(1, getXPixels());
        std::string stem = "HSI_" + std::to_string(i);
        std::string folder = measurementFolder;
        std::vector<float> wl = wlArray_;
        SpectrumMetadata meta = currentMetadata();

        // launch save in background — next acquisition starts immediately
        prevSaveThread = std::thread([this, folder, stem, spectra, wl, meta]() {
            saveSpectrumSet(folder, stem, spectra, wl, 1, getXPixels(), meta);
        });

        // next loop iteration: startAcquisition() runs while prevSaveThread is saving
    }

    // join the last save after the loop
    if (prevSaveThread.joinable())
        prevSaveThread.join();
    */

    // set the trigger to internal and the mode to kinetic, then we can trigger the acquisition and save the data in a separate thread. This way we can start the next acquisition while the previous one is being saved.

    configureSpectral(ReadMode::FVB, TriggerMode::Internal, 0.1f, nspecs);
    // set integration time to 100 ms
    setExposureTime(0.1f);

    // waite until the acquisition is complete before saving the data
    startAcquisition();
    waitForAcquisition();

        // read the entire kinetics buffer in one call
    std::vector<int> spectra = getAllSpectra(nspecs, getXPixels());

    // save all spectra
    for (int i = 0; i < nspecs; ++i) {
        std::vector<int> single(spectra.begin() + i * getXPixels(),
                                spectra.begin() + (i + 1) * getXPixels());
        const std::string stem = "HSI_" + std::to_string(i);
        saveSpectrumSet(foldername, stem, single, wlArray_, 1, getXPixels(), currentMetadata());
    }

    std::chrono::steady_clock::time_point tend = std::chrono::steady_clock::now();
    auto acquisitionTime = std::chrono::duration_cast<std::chrono::milliseconds>(tend - tstart);

    std::cout << "Saved " << nspecs << " spectra to " << measurementFolder << "\n";
    std::cout << "Total acquisition time: " << acquisitionTime.count() << " ms\n";
    std::cout << "Average acquisition time per spectrum: " << acquisitionTime.count() / static_cast<float>(nspecs) << " ms\n";
}