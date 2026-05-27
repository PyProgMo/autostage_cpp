// AndorCamera.cpp
#include "AndorCamera.h"
#include <opencv2/opencv.hpp>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <cstdint>

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
    pGetDetector         = loadProc<FP_GetDetector>        ("GetDetector");
    pSetReadMode         = loadProc<FP_SetReadMode>        ("SetReadMode");
    pSetAcquisitionMode  = loadProc<FP_SetAcquisitionMode> ("SetAcquisitionMode");
    pSetExposureTime     = loadProc<FP_SetExposureTime>    ("SetExposureTime");
    pSetTriggerMode      = loadProc<FP_SetTriggerMode>     ("SetTriggerMode");
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
    char dir[512] = {};
    strncpy_s(dir, iniDir.empty() ? "." : iniDir.c_str(), 511);
    check(pInitialize(dir), "Initialize");
    check(pGetDetector(&xpix_, &ypix_), "GetDetector");
    std::cout << "Andor: " << xpix_ << " x " << ypix_ << " pixels\n";
}

void AndorCamera::configureSpectral(ReadMode readMode, TriggerMode trigMode, float exposureSeconds, int numSpectra) {
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
    check(pStartAcquisition(), "StartAcquisition");
}

void AndorCamera::abortAcquisition() {
    pAbortAcquisition();    // ignore error on abort
}

void AndorCamera::waitForAcquisition() {
    check(pWaitForAcquisition(), "WaitForAcquisition");
}

std::vector<WORD> AndorCamera::getAllSpectra(int numSpectra, int pixelsPerSpectrum) {
    std::vector<WORD> buf(numSpectra * pixelsPerSpectrum);
    long vf, vl;
    check(pGetImages16(1, numSpectra, buf.data(),
                       (unsigned long)buf.size(), &vf, &vl),
          "GetImages16");
    return buf;
}

void AndorCamera::shutdown() {
    if (hDll_) { pShutDown(); }
}

void AndorCamera::testAcquireAndSave(const std::vector<WORD>& spectra, int numSpectra, int pixelsPerSpectrum, const std::string& filename) {
    if (numSpectra <= 0 || pixelsPerSpectrum <= 0) {
        throw std::runtime_error("Andor: invalid spectrum image dimensions");
    }
    if (spectra.empty() || spectra.size() < static_cast<size_t>(numSpectra) * static_cast<size_t>(pixelsPerSpectrum)) {
        throw std::runtime_error("Andor: testAcquireAndSave requires non-empty spectrum data");
    }

    // debug 1: print cv::getBuildInformation()
    std::cout << "OpenCV Build Information:\n" << cv::getBuildInformation() << "\n";

    // Find max value for scaling
    WORD maxVal = 0;
    for (WORD val : spectra) {
        if (val > maxVal) maxVal = val;
    }
    if (maxVal == 0) maxVal = 1; // avoid division by zero

    // Create OpenCV Mat and scale to 8-bit
    cv::Mat img(numSpectra, pixelsPerSpectrum, CV_8UC1);
    for (int i = 0; i < numSpectra; i++) {
        for (int j = 0; j < pixelsPerSpectrum; j++) {
            WORD val = spectra[i * pixelsPerSpectrum + j];
            img.at<uchar>(i, j) = static_cast<uchar>((val * 255) / maxVal);
        }
    }
    // debug 1: print cv::getBuildInformation()
    std::cout << "OpenCV Build Information:\n" << cv::getBuildInformation() << "\n";
    // Save as PNG
    if (!cv::imwrite(filename + ".png", img)) {
        std::cerr << "Failed to save PNG: " << filename << "\n";
    } else {
        std::cout << "Saved spectra image to " << filename << "\n";
    }
    // Save to .txt file
    std::ofstream outFile(filename + ".txt");
    if (!outFile) {
        std::cerr << "Failed to save TXT: " << filename << "\n";
    } else {
        for (int i = 0; i < numSpectra; i++) {
            for (int j = 0; j < pixelsPerSpectrum; j++) {
                outFile << spectra[i * pixelsPerSpectrum + j] << " ";
            }
            outFile << "\n";
        }
        std::cout << "Saved spectra data to " << filename << ".txt\n";
    }

}

/* this founction exists twice in the script
void AndorCamera::testAcquireAndSave(float exposureSeconds, const std::string& filename) {
    configureSpectral(ReadMode::FVB, TriggerMode::Internal, exposureSeconds);
    startAcquisition();
    waitForAcquisition();

    std::vector<WORD> spectra = getAllSpectra(1, getXPixels());
    testAcquireAndSave(spectra, 1, getXPixels(), filename);
}*/
