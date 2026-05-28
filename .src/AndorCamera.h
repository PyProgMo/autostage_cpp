// AndorCamera.h
#pragma once
#include <Windows.h>
#include <vector>
#include <string>

// Andor SDK2 error codes
#define DRV_SUCCESS          20002
#define DRV_ACQUIRING        20072
#define DRV_IDLE             20073

// ── SDK2 function pointer typedefs ────────────────────────────────────────
typedef unsigned int (__stdcall *FP_Initialize)         (char* dir);
typedef unsigned int (__stdcall *FP_GetDetector)        (int* xpix, int* ypix);
typedef unsigned int (__stdcall *FP_SetReadMode)        (int mode);
typedef unsigned int (__stdcall *FP_SetAcquisitionMode) (int mode);
typedef unsigned int (__stdcall *FP_SetExposureTime)    (float seconds);
typedef unsigned int (__stdcall *FP_SetTriggerMode)     (int mode);
typedef unsigned int (__stdcall *FP_SetAccumulationCycleTime)(float seconds);
typedef unsigned int (__stdcall *FP_SetImage)           (int hbin, int vbin,
                                                          int hstart, int hend,
                                                          int vstart, int vend);
typedef unsigned int (__stdcall *FP_StartAcquisition)   ();
typedef unsigned int (__stdcall *FP_AbortAcquisition)   ();
typedef unsigned int (__stdcall *FP_WaitForAcquisition) ();
typedef unsigned int (__stdcall *FP_GetAcquiredData)    (long* arr, unsigned long size);
typedef unsigned int (__stdcall *FP_GetAcquiredData16)  (WORD* arr, unsigned long size);
typedef unsigned int (__stdcall *FP_GetStatus)          (int* status);
typedef unsigned int (__stdcall *FP_ShutDown)           ();
typedef unsigned int (__stdcall *FP_SetSpool)           (int active, int method,
                                                          char* path, int framebufsize);
typedef unsigned int (__stdcall *FP_SetKineticCycleTime)(float seconds);
typedef unsigned int (__stdcall *FP_SetNumberKinetics)  (int numKin);
typedef unsigned int (__stdcall *FP_GetNumberNewImages) (long* first, long* last);
typedef unsigned int (__stdcall *FP_GetImages16)        (long first, long last,
                                                          WORD* arr, unsigned long size,
                                                          long* validfirst, long* validlast);

class AndorCamera {
public:
    enum class TriggerMode {
        Internal    = 0,
        External    = 1,    // edge trigger — one frame per pulse
        ExternalStart = 6,  // first pulse starts a kinetic series
        FastExternal = 7,   // fast external — best for raster scanning
        Software    = 10
    };

    enum class ReadMode {
        FVB         = 0,     // Full Vertical Binning — fastest, 1D spectrum
        MultiTrack  = 1,     // Multiple Track — several binned tracks on sensor
        RandomTrack  = 2,     // Random Track — user-defined rows to bin
        SingleTrack = 3,      // Single Track — like Multi but only one track (for spectroscopy)
        FullImage   = 4,       // Full Image — no binning, 2D image readout
    };

    AndorCamera();
    ~AndorCamera();

    void loadDLL(const std::string& dllPath);
    void initialize(const std::string& iniDir = "");
    void shutdown();

    void setReadMode(int mode);
    void setAcquisitionMode(int mode);
    void setExposureTime(float time);
    void setTriggerMode(int mode);
    void setImage(int hbin, int vbin, int hstart, int hend, int vstart, int vend);
    int getStatus();
    void setKineticCycleTime(float time);
    void setNumberKinetics(int number);

    // Configure for triggered spectral acquisition
    void configureSpectral(ReadMode   readMode,
                           TriggerMode trigMode,
                           float      exposureSeconds,
                           int        numSpectra = 1);

    // FVB: single spectrum per trigger pulse (fastest for raster)
    void configureFVBKinetic(float exposureSeconds, int numLines);

    void startAcquisition();
    void abortAcquisition();

    // Block until one frame arrives (for slow scan)
    void waitForAcquisition();

    // Read all kinetic data at end of scan
    std::vector<WORD> getAllSpectra(int numSpectra, int pixelsPerSpectrum);

    int getXPixels() const { return xpix_; }
    int getYPixels() const { return ypix_; }

    // declare the test functions here so they can be called from ConsoleApp without including private members
    void testAcquireAndSave(float exposureSeconds, const std::string& filename);
    void testAcquireAndSave(const std::vector<WORD>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename);

private:
    HMODULE hDll_ = nullptr;
    int xpix_ = 0, ypix_ = 0;

    FP_Initialize              pInitialize              = nullptr;
    FP_GetDetector             pGetDetector             = nullptr;
    FP_SetReadMode             pSetReadMode             = nullptr;
    FP_SetAcquisitionMode      pSetAcquisitionMode      = nullptr;
    FP_SetExposureTime         pSetExposureTime         = nullptr;
    FP_SetTriggerMode          pSetTriggerMode          = nullptr;
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

    void check(unsigned int ret, const char* context);

    template<typename T>
    T loadProc(const char* name);
};
