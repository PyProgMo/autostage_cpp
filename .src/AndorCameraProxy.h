// AndorCameraProxy.h
#pragma once
#include "AndorCamera.h"
#include <Windows.h>
#include <string>
#include <vector>
#include <map>

struct IpcMessage;

class AndorCameraProxy {
public:
    AndorCameraProxy();
    ~AndorCameraProxy();

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
    void measureBackground(const std::string& filename = "background");

    void setReadMode(int mode);
    void setAcquisitionMode(int mode);
    void setExposureTime(float time);
    void setTriggerMode(int mode);
    void setImage(int hbin, int vbin, int hstart, int hend, int vstart, int vend);
    int getStatus();
    void setKineticCycleTime(float time);
    void setNumberKinetics(int number);

    void configureSpectral(AndorCamera::ReadMode ReadMode,
                           AndorCamera::TriggerMode trigMode,
                           float exposureSeconds,
                           int numSpectra = 1);

    void configureFVBKinetic(float exposureSeconds, int numLines);

    void startAcquisition();
    void abortAcquisition();
    void waitForAcquisition();
    // testAcquireAndSave functions
    void testAcquireAndSave(float exposureSeconds, const std::string& filename);
    void testAcquireAndSave(const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename);
    std::vector<int> getAllSpectra(int numSpectra, int pixelsPerSpectrum);


    
    // AcquireAndSavefast function
    void AcquireAndSavefast(const std::vector<int>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename);
    void saveSpectrumSet(const std::string& measurementFolder,
                      const std::string& stem,
                      const std::vector<int>& spectra,
                      const std::vector<float>& WL,
                      int numSpectra,
                      int pixelsPerSpectrum,
                      SpectrumMetadata& meta,
                      bool saveAsPng = false);
    void savespecfast(const std::string& measurementFolder,
         const std::vector<int>& spectra, 
         int numSpectra, 
         int pixelsPerSpectrum, 
         SpectrumMetadata& meta,
         const std::string& filename);

    int getXPixels() const { return xpix_; }
    int getYPixels() const { return ypix_; }

    // wl array function: init array for the wl-array
    void getWLarray(float startWL, float endWL, std::vector<int>& WL);
    // void that returns a reference to the medadata map of the proxy, so that it can be updated from ConsoleApp when metadata is entered by the user, and then saved with each spectrum without having to pass it back and forth with every save command
    SpectrumMetadata specmeta_;
    std::map<int, SpectrumMetadata>& getMetadataMap() { return metadataMap_; }
    // functions to request metadata over the pipe
    SpectrumMetadata getMetadata();
    void setMetadata(const SpectrumMetadata& metadata);

    // add test function for sanity checks
    // acquire 10 spectra with 0.1 s exposure and print the first 10 pixels of each to console, also save them to disk, important: print how loong it took
    void testtenspectime();

private:
    HANDLE hPipe_ = INVALID_HANDLE_VALUE;
    int xpix_ = 0, ypix_ = 0;
    int selectedCameraIndex_ = 0;
    std::map<int, std::vector<int>> backgrounds_;

    void sendCommand(const IpcMessage& msg, IpcMessage& response, unsigned int timeout_ms = 1000, bool throwOnError = true);

    // hold a copy of the wl array in the proxy for saving metadata, to avoid having to pass it back and forth with every save command
    std::vector<float> wlArray_;
    // hold a pointer to the Metadata of AndorCamera
    std::map<int, SpectrumMetadata> metadataMap_;
};
