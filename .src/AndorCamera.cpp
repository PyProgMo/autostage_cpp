// AndorCamera.cpp
#include "AndorCamera.h"
#include <stdexcept>
#include <iostream>

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