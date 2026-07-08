// AndorCamera.h
#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <map>
#include <sstream>

// Andor SDK2 error codes
#define DRV_SUCCESS          20002
#define DRV_ACQUIRING        20072
#define DRV_IDLE             20073

// ── SDK2 function pointer typedefs ────────────────────────────────────────
typedef unsigned int (__stdcall *FP_Initialize)         (char* dir);
typedef unsigned int (__stdcall *FP_GetAvailableCameras)(long* totalCameras);
typedef unsigned int (__stdcall *FP_GetCameraHandle)    (long cameraIndex, long* cameraHandle);
typedef unsigned int (__stdcall *FP_SetCurrentCamera)   (long cameraHandle);
typedef unsigned int (__stdcall *FP_GetDetector)        (int* xpix, int* ypix);
typedef unsigned int (__stdcall *FP_SetReadMode)        (int mode);
typedef unsigned int (__stdcall *FP_SetAcquisitionMode) (int mode);
typedef unsigned int (__stdcall *FP_SetExposureTime)    (float seconds);
typedef unsigned int (__stdcall *FP_SetTriggerMode)     (int mode);
typedef unsigned int (__stdcall *FP_CoolerON)           ();
typedef unsigned int (__stdcall *FP_CoolerOFF)          ();
typedef unsigned int (__stdcall *FP_SetTemperature)     (int temperature);
typedef unsigned int (__stdcall *FP_GetTemperature)     (int* temperature);
typedef unsigned int (__stdcall *FP_IsCoolerOn)         (int* coolerOn);
typedef unsigned int (__stdcall *FP_SetAccumulationCycleTime)(float seconds);
typedef unsigned int (__stdcall *FP_SetImage)           (int hbin, int vbin,
                                                          int hstart, int hend,
                                                          int vstart, int vend);
typedef unsigned int (__stdcall *FP_StartAcquisition)   ();
typedef unsigned int (__stdcall *FP_AbortAcquisition)   ();
typedef unsigned int (__stdcall *FP_WaitForAcquisition) ();
typedef unsigned int (__stdcall *FP_GetAcquiredData)    (long* arr, unsigned long size);
typedef unsigned int (__stdcall *FP_GetAcquiredData16)  (unsigned short* arr, unsigned long size);
typedef unsigned int (__stdcall *FP_GetStatus)          (int* status);
typedef unsigned int (__stdcall *FP_ShutDown)           ();
typedef unsigned int (__stdcall *FP_SetSpool)           (int active, int method,
                                                          char* path, int framebufsize);
typedef unsigned int (__stdcall *FP_SetKineticCycleTime)(float seconds);
typedef unsigned int (__stdcall *FP_SetNumberKinetics)  (int numKin);
typedef unsigned int (__stdcall *FP_GetNumberNewImages) (long* first, long* last);
typedef unsigned int (__stdcall *FP_GetImages16)        (long first, long last,
                                                          unsigned short* arr, unsigned long size,
                                                          long* validfirst, long* validlast);
typedef unsigned int (__stdcall *FP_GetTotalNumberImagesAcquired)(long* total);                                                          

namespace Andor {
    enum class TriggerMode {
        Internal      = 0,
        External      = 1,    // edge trigger — one frame per pulse
        ExternalStart = 6,    // first pulse starts a kinetic series
        FastExternal  = 7,    // fast external — best for raster scanning
        Software      = 10
    };

    enum class ReadMode {
        FVB         = 0,     // Full Vertical Binning — fastest, 1D spectrum
        MultiTrack  = 1,     // Multiple Track — several binned tracks on sensor
        RandomTrack = 2,     // Random Track — user-defined rows to bin
        SingleTrack = 3,     // Single Track — like Multi but only one track (for spectroscopy)
        FullImage   = 4,     // Full Image — no binning, 2D image readout
    };

    enum class Camera {
        Newton = 0,
        Clara  = 1,
        Idus   = 2,
    };

        // Andor error codes
    enum class CameraErrorToString : int {
        ErrorCodes              = 20001,
        Success                 = 20002,
        VxdNotInstalled         = 20003,
        ErrorScan               = 20004,
        ErrorCheckSum           = 20005,
        ErrorFileload           = 20006,
        UnknownFunction         = 20007,
        ErrorVxdInit            = 20008,
        ErrorAddress            = 20009,
        ErrorPagelock           = 20010,
        ErrorPageUnlock         = 20011,
        ErrorBoardtest          = 20012,
        ErrorAck                = 20013,
        ErrorUpFifo             = 20014,
        ErrorPattern            = 20015,
        AcquisitionErrors       = 20017,
        AcqBuffer               = 20018,
        AcqDownfifoFull         = 20019,
        ProcUnknownInstruction  = 20020,
        IllegalOpCode           = 20021,
        KineticTimeNotMet       = 20022,
        AccumTimeNotMet         = 20023,
        NoNewData               = 20024,
        PciDmaFail              = 20025,
        SpoolError              = 20026,
        SpoolSetupError         = 20027,
        Saturated               = 20029,
        TemperatureCodes        = 20033,
        TemperatureOff          = 20034,
        TempNotStabilized       = 20035,
        TemperatureStabilized   = 20036,
        TemperatureNotReached   = 20037,
        TemperatureOutRange     = 20038,
        TemperatureNotSupported = 20039,
        TemperatureDrift        = 20040,
        GeneralErrors           = 20049,
        InvalidAux              = 20050,
        CofNotLoaded            = 20051,
        FpgaProg                = 20052,
        FlexError               = 20053,
        GpibError               = 20054,
        ErrorDmaUpload          = 20055,
        Datatype                = 20064,
        DriverErrors            = 20065,
        P1Invalid               = 20066,
        P2Invalid               = 20067,
        P3Invalid               = 20068,
        P4Invalid               = 20069,
        IniError                = 20070,
        CofError                = 20071,
        Acquiring               = 20072,
        Idle                    = 20073,
        TempCycle               = 20074,
        NotInitialized          = 20075,
        P5Invalid               = 20076,
        P6Invalid               = 20077,
        InvalidMode             = 20078,
        InvalidFilter           = 20079,
        I2cErrors               = 20080,
        I2cDevNotFound          = 20081,
        I2cTimeout              = 20082,
        P7Invalid               = 20083,
        UsbError                = 20089,
        IocError                = 20090,
        VrmVersionError         = 20091,
        UsbInterruptEndpointError = 20093,
        RandomTrackError        = 20094,
        InvalidTriggerMode      = 20095,
        LoadFirmwareError       = 20096,
        DivideByZeroError       = 20097,
        InvalidRingExposures    = 20098,
        BinningError            = 20099,
        InvalidAmplifier        = 20100,
        InvalidCountconvertMode = 20101,
        ErrorMap                = 20115,
        ErrorUnmap              = 20116,
        ErrorMdl                = 20117,
        ErrorUnmdl              = 20118,
        ErrorBuffsize           = 20119,
        ErrorNoHandle           = 20121,
        GatingNotAvailable      = 20130,
        FpgaVoltageError        = 20131,
        ErrorNoCamera           = 20990,
        NotSupported            = 20991,
        NotAvailable            = 20992,
    };
}


                                                          
// spectrum metadata struct for saving metadata along with spectra, can be extended in the future as needed
struct SpectrumMetadata {

        // ── Identity ──────────────────────────────────────────────────────────
        std::string date;           // "09.06.2026/10:46"
        std::string userName;       // "the master of microscopy"
        std::string fileName;       // "pl1"

        // ── Spectrograph ──────────────────────────────────────────────────────
        double      slitWidthUm;    // 250.0
        std::string grating;        // "500 blz 300l/mm"
        std::string filter;         // "Empty"
        double      centralWlNm;    // 599.98

        // ── Detector ──────────────────────────────────────────────────────────
        std::string                  detector;          // "CCD"
        double                       coolingTempC;      // -70
        double                       exposureTimeS;     // 1.00
        int                          horizontalBinning; // 1
        double                       wlFirstPixelNm;   // 458.55
        double                       wlLastPixelNm;    // 1024.00
        double                       deltaWlNm;        // 0.275
        Andor::ReadMode              ReadMode;          // FVB
        Andor::TriggerMode           triggerMode;       // External

        // ── Nano Stage ────────────────────────────────────────────────────────
        double xPos, yPos, zPos;    // 150.000, 150.000, 263.000
        double xPosNm, yPosNm, zPosNm; // 150000, 150000, 263000
        int    switchUD;            // 1
        int    switchLR;            // 1
        double xstartNm, xendNm, ystartNm, yendNm, zstartNm, zendNm; // if measuring while moving

        // ── Light Source ──────────────────────────────────────────────────────
        std::string nktSystem;      // "SuperK Varia (VIS)"
        std::string operation;      // ""
        double      powerLevelPct;  // 0.0
        float         shortWlNm;      // 0
        float         longWlNm;       // 0

        // ── Microscopy ────────────────────────────────────────────────────────
        double    laserPosX;           // 520
        double    laserPosY;           // 696
        double magnification;       // 83.333
        double powerAtGlassUW;      // -0.008998
    };

inline std::string serializeSpectrumMetadata(const SpectrumMetadata& metadata) {
    std::ostringstream oss;

    // Keep format line-oriented and append-only for easy forward compatibility.
    oss << "date=" << metadata.date << '\n';
    oss << "userName=" << metadata.userName << '\n';
    oss << "fileName=" << metadata.fileName << '\n';
    oss << "slitWidthUm=" << metadata.slitWidthUm << '\n';
    oss << "grating=" << metadata.grating << '\n';
    oss << "filter=" << metadata.filter << '\n';
    oss << "centralWlNm=" << metadata.centralWlNm << '\n';
    oss << "detector=" << metadata.detector << '\n';
    oss << "coolingTempC=" << metadata.coolingTempC << '\n';
    oss << "exposureTimeS=" << metadata.exposureTimeS << '\n';
    oss << "horizontalBinning=" << metadata.horizontalBinning << '\n';
    oss << "wlFirstPixelNm=" << metadata.wlFirstPixelNm << '\n';
    oss << "wlLastPixelNm=" << metadata.wlLastPixelNm << '\n';
    oss << "deltaWlNm=" << metadata.deltaWlNm << '\n';
    oss << "ReadMode=" << static_cast<int>(metadata.ReadMode) << '\n';
    oss << "triggerMode=" << static_cast<int>(metadata.triggerMode) << '\n';
    oss << "xPos=" << metadata.xPos << '\n';
    oss << "yPos=" << metadata.yPos << '\n';
    oss << "zPos=" << metadata.zPos << '\n';
    oss << "switchUD=" << metadata.switchUD << '\n';
    oss << "switchLR=" << metadata.switchLR << '\n';
    oss << "nktSystem=" << metadata.nktSystem << '\n';
    oss << "operation=" << metadata.operation << '\n';
    oss << "powerLevelPct=" << metadata.powerLevelPct << '\n';
    oss << "shortWlNm=" << metadata.shortWlNm << '\n';
    oss << "longWlNm=" << metadata.longWlNm << '\n';
    oss << "laserPosX=" << metadata.laserPosX << '\n';
    oss << "laserPosY=" << metadata.laserPosY << '\n';
    oss << "magnification=" << metadata.magnification << '\n';
    oss << "powerAtGlassUW=" << metadata.powerAtGlassUW << '\n';

    return oss.str();
}

inline bool deserializeSpectrumMetadata(const std::string& payload, SpectrumMetadata& metadata) {
    std::istringstream iss(payload);
    std::string line;

    while (std::getline(iss, line)) {
        const size_t split = line.find('=');
        if (split == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, split);
        const std::string value = line.substr(split + 1);

        if (key == "date") metadata.date = value;
        else if (key == "userName") metadata.userName = value;
        else if (key == "fileName") metadata.fileName = value;
        else if (key == "slitWidthUm") metadata.slitWidthUm = std::stod(value);
        else if (key == "grating") metadata.grating = value;
        else if (key == "filter") metadata.filter = value;
        else if (key == "centralWlNm") metadata.centralWlNm = std::stod(value);
        else if (key == "detector") metadata.detector = value;
        else if (key == "coolingTempC") metadata.coolingTempC = std::stod(value);
        else if (key == "exposureTimeS") metadata.exposureTimeS = std::stod(value);
        else if (key == "horizontalBinning") metadata.horizontalBinning = std::stoi(value);
        else if (key == "wlFirstPixelNm") metadata.wlFirstPixelNm = std::stod(value);
        else if (key == "wlLastPixelNm") metadata.wlLastPixelNm = std::stod(value);
        else if (key == "deltaWlNm") metadata.deltaWlNm = std::stod(value);
        else if (key == "ReadMode") metadata.ReadMode = static_cast<Andor::ReadMode>(std::stoi(value));
        else if (key == "triggerMode") metadata.triggerMode = static_cast<Andor::TriggerMode>(std::stoi(value));
        else if (key == "xPos") metadata.xPos = std::stod(value);
        else if (key == "yPos") metadata.yPos = std::stod(value);
        else if (key == "zPos") metadata.zPos = std::stod(value);
        else if (key == "switchUD") metadata.switchUD = std::stoi(value);
        else if (key == "switchLR") metadata.switchLR = std::stoi(value);
        else if (key == "nktSystem") metadata.nktSystem = value;
        else if (key == "operation") metadata.operation = value;
        else if (key == "powerLevelPct") metadata.powerLevelPct = std::stod(value);
        else if (key == "shortWlNm") metadata.shortWlNm = std::stof(value);
        else if (key == "longWlNm") metadata.longWlNm = std::stof(value);
        else if (key == "laserPosX") metadata.laserPosX = std::stod(value);
        else if (key == "laserPosY") metadata.laserPosY = std::stod(value);
        else if (key == "magnification") metadata.magnification = std::stod(value);
        else if (key == "powerAtGlassUW") metadata.powerAtGlassUW = std::stod(value);
    }

    return true;
}

// Adapt this to your actual Andor::ReadMode enum values.
inline const char* readModeToString(Andor::ReadMode mode) {
    switch (mode) {
        case Andor::ReadMode::FVB:         return "Full Vertical Binning";
        case Andor::ReadMode::SingleTrack: return "Single Track";
        case Andor::ReadMode::MultiTrack:  return "Multi Track";
        case Andor::ReadMode::FullImage:       return "Full Image";
        default:                           return "Unknown";
    }
}


class AndorCamera {
public:
    using TriggerMode = Andor::TriggerMode;
    using ReadMode = Andor::ReadMode;
    using Camera = Andor::Camera;
    using CameraErrorToString = Andor::CameraErrorToString;

    AndorCamera();
    ~AndorCamera();

    void loadDLL(const std::string& dllPath);
    void initialize(const std::string& iniDir = "");
    void shutdown();
    int getAvailableCameras();
    void selectCamera(int cameraIndex);
    void enableCooling(bool enable);
    void setCoolingTemperature(int temperatureC);
    int getCoolingTemperature();
    bool isCoolingEnabled();
    void setBackground(const std::vector<int>& spectra);
    bool hasBackground() const;
    std::vector<int> getBackground() const;
    void measureBackground(float exposureSeconds, const std::string& filename = "background");

    void setReadMode(int mode);
    void setAcquisitionMode(int mode);
    void setExposureTime(float time);
    void setTriggerMode(int mode);
    void setImage(int hbin, int vbin, int hstart, int hend, int vstart, int vend);
    int getStatus();
    int getTotalNumberImagesAcquired(int& total);
    void setKineticCycleTime(float time);
    void setNumberKinetics(int number);

    // Configure for triggered spectral acquisition
    void configureSpectral(ReadMode   ReadMode,
                           TriggerMode trigMode,
                           float      exposureSeconds,
                           int        numSpectra = 1);
    
    void AcquireSpecandSave(const std::string& foldername, const std::string& filename);
    void AcquireSpecandSavefast(const std::string& foldername, double x, double y, double z, const std::string& filename);

    // FVB: single spectrum per trigger pulse (fastest for raster)
    void configureFVBKinetic(float exposureSeconds, int numLines);

    void startAcquisition();
    void abortAcquisition();

    // Block until one frame arrives (for slow scan)
    void waitForAcquisition();

    // Read all kinetic data at end of scan
    std::vector<int> getAllSpectra(int numSpectra, int pixelsPerSpectrum);

    int getXPixels() const { return xpix_; }
    int getYPixels() const { return ypix_; }

    // declare the test functions here so they can be called from ConsoleApp without including private members
    void testAcquireAndSave(float exposureSeconds, const std::string& filename);
    void testAcquireAndSave(const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename);
    void Savefast(const std::string& foldername, const std::vector<int>& spectra, const std::vector<int>& background, const std::vector<float>& wlArray, int numSpectra, int pixelsPerSpectrum, const std::string& filename, const SpectrumMetadata& metadata);
    void setupfastAcquisition(float exposureSeconds, int numSpectra);
    void runfastAcquistiontriggered(float exposureSeconds, int numSpectra, std::string filename, std::string foldername);

    // wl array function: init array for the wl-array
    void getWLarray(float startWL, float endWL, std::vector<float>& WL);
    void setWLarray(std::vector<float>& WL);
    SpectrumMetadata getMetadata() const;
    void setMetadata(const SpectrumMetadata& metadata);
    void saveSpectrumSet(const std::string& measurementFolder,
                      const std::string& stem,
                      const std::vector<int>& spectra,
                      const std::vector<float>& WL,
                      int numSpectra,
                      int pixelsPerSpectrum,
                      const SpectrumMetadata& specmeta,
                      bool saveAsPng = false);
    void AcquireAndFetchSingle(int pixelsPerSpectrum, std::vector<int>& data, SpectrumMetadata& meta);
    
    static const char* readModeToString(Andor::ReadMode mode);
    static const char* triggerModeToString(Andor::TriggerMode mode);
    static const char* CameraNtoName(int cameraN);
    std::string TranslateCameraErrorToString(int status);

    void measureandsaveNspecs(const std::string& foldername, int nspecs);
    std::map<int, SpectrumMetadata> metadataMap_;


private:
    HMODULE hDll_ = nullptr;
    int xpix_ = 0, ypix_ = 0;
    int selectedCameraIndex_ = 0;
    long selectedCameraHandle_ = 0;
    long availableCameras_ = 0;
    // wavelength array init.
    int wldummy = 1; // placeholder to check. start=1 (dummy), 0=init, 
    int wlStart_ = 0, wlEnd_ = 1023; // placeholder values for wavelength calibration range
    int wlNumPoints_ = 1024; // placeholder for number of points in wavelength calibration, typically matches pixel count
    std::vector<float> wlArray_; // placeholder for wavelength calibration data, on start initialize with pixel indices, later with real wavelength values

    FP_Initialize              pInitialize              = nullptr;
    FP_GetAvailableCameras      pGetAvailableCameras     = nullptr;
    FP_GetCameraHandle          pGetCameraHandle         = nullptr;
    FP_SetCurrentCamera         pSetCurrentCamera        = nullptr;
    FP_GetDetector             pGetDetector             = nullptr;
    FP_SetReadMode             pSetReadMode             = nullptr;
    FP_SetAcquisitionMode      pSetAcquisitionMode      = nullptr;
    FP_SetExposureTime         pSetExposureTime         = nullptr;
    FP_SetTriggerMode          pSetTriggerMode          = nullptr;
    FP_CoolerON                pCoolerON                = nullptr;
    FP_CoolerOFF               pCoolerOFF               = nullptr;
    FP_SetTemperature          pSetTemperature          = nullptr;
    FP_GetTemperature          pGetTemperature          = nullptr;
    FP_IsCoolerOn              pIsCoolerOn              = nullptr;
    FP_SetImage                pSetImage                = nullptr;
    FP_StartAcquisition        pStartAcquisition        = nullptr;
    FP_AbortAcquisition        pAbortAcquisition        = nullptr;
    FP_WaitForAcquisition      pWaitForAcquisition      = nullptr;
    FP_GetAcquiredData16       pGetAcquiredData16       = nullptr;
    FP_GetStatus               pGetStatus               = nullptr;
    FP_ShutDown                pShutDown                = nullptr;
    FP_SetKineticCycleTime     pSetKineticCycleTime     = nullptr;
    FP_SetNumberKinetics       pSetNumberKinetics       = nullptr;
    FP_GetImages16             pGetImages16             = nullptr;
    FP_GetTotalNumberImagesAcquired pGetTotalNumberImagesAcquired = nullptr;

    std::map<int, std::vector<int>> backgrounds_;

    SpectrumMetadata& currentMetadata();
    const SpectrumMetadata& currentMetadata() const;

    void ensureLoaded();
    void check(unsigned int ret, const char* context);

    template<typename T>
    T loadProc(const char* name);
};

