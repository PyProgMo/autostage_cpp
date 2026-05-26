// AndorCameraProxy.h
#pragma once
#include "AndorCamera.h"
#include <Windows.h>
#include <string>
#include <vector>

struct IpcMessage;

class AndorCameraProxy {
public:
    AndorCameraProxy();
    ~AndorCameraProxy();

    void loadDLL(const std::string& dllPath);
    void initialize(const std::string& iniDir = "");
    void shutdown();

    void configureSpectral(AndorCamera::ReadMode readMode,
                           AndorCamera::TriggerMode trigMode,
                           float exposureSeconds,
                           int numSpectra = 1);

    void configureFVBKinetic(float exposureSeconds, int numLines);

    void startAcquisition();
    void abortAcquisition();
    void waitForAcquisition();
    void testAcquireAndSave(const std::vector<WORD>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename);
    std::vector<WORD> getAllSpectra(int numSpectra, int pixelsPerSpectrum);

    int getXPixels() const { return xpix_; }
    int getYPixels() const { return ypix_; }

private:
    HANDLE hPipe_ = INVALID_HANDLE_VALUE;
    int xpix_ = 0, ypix_ = 0;

    void sendCommand(const IpcMessage& msg, IpcMessage& response);
};
