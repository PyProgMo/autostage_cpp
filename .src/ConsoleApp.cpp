// ConsoleApp.cpp
#include "PIStageProxy.h"
#include "AndorCameraProxy.h"
#include "Logger.h"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <memory>

void startProcess(const std::string& cmdLine) {
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    std::string cmd = cmdLine;
    if (CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::cout << "Started " << cmdLine << "\n";
    } else {
        std::cerr << "Failed to start " << cmdLine << ". Error: " << GetLastError() << "\n";
    }
}

int main() {
    std::cout << "--- Master Console ---\n";
    std::cout << "Starting 32-bit StageServer...\n";
    startProcess("build\\StageServer.exe");
    
    std::cout << "Starting 64-bit SpectrometerServer...\n";
    startProcess("build\\SpectrometerServer.exe");

    Sleep(1000); // Give servers time to bind to pipes

    std::unique_ptr<PIStageProxy> stage;
    std::unique_ptr<AndorCameraProxy> cam;

    try {
        stage = std::make_unique<PIStageProxy>();
        cam = std::make_unique<AndorCameraProxy>();
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect to IPC pipes: " << e.what() << "\n";
        return 1;
    }

    std::string cmd;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, cmd)) break;
        if (cmd == "exit" || cmd == "quit") break;
        if (cmd.empty()) continue;
        // if cmd == "help" print user guidance
        if (cmd == "help") {
            std::cout << "Commands:\n";
            std::cout << "  stage connect\n";
            std::cout << "  stage disconnect\n";
            std::cout << "  stage get_pos [axis] \n";
            std::cout << "  stage move_abs [axis] [pos]\n";
            std::cout << "  stage wait [axis]\n";
            std::cout << "  andor connect [cameraIndex]\n";
            std::cout << "  andor cameras\n";
            std::cout << "  andor selectCamera [cameraIndex]\n";
            std::cout << "  andor cooling on|off\n";
            std::cout << "  andor setTemp [celsius]\n";
            std::cout << "  andor getTemp\n";
            std::cout << "  andor measurebg\n";
            std::cout << "  andor measure\n";
            std::cout << "  andor setTint [milliseconds]\n";
            std::cout << "  andor setReadMode [mode] (FVB, MultiTrack, RandomTrack, SingleTrack, FullImage)\n";
            std::cout << "  andor setAcquisitionMode [mode] (Single, Continuous, Kinetic)\n";
            std::cout << "  andor setExposureTime [seconds]\n";
            std::cout << "  andor setTriggerMode [mode] (Internal, External, ExternalStart, FastExternal, Software)\n";
            std::cout << "  andor setImage [h] [v] [hs] [he] [vs] [ve]\n";
            std::cout << "  andor getStatus\n";
            std::cout << "  andor setKineticCycleTime [s]\n";
            std::cout << "  andor setNumberKinetics [num]\n";
            std::cout << "  andor test\n";
            std::cout << "  andor disconnect\n";
            std::cout << "  scan\n";
            continue;
        }

        std::istringstream iss(cmd);
        std::string target, action;
        iss >> target >> action;

        try {
            if (target == "stage") {
                if (action == "connect") {
                    stage->loadDLL("E7XX_GCS2_DLL.dll");
                    stage->connect("109021162");
                    std::cout << "Stage connected.\n";
                } else if (action == "disconnect") {
                    stage->disconnect();
                    std::cout << "Stage disconnected.\n";
                } else if (action == "get_pos") {
                    std::string axis;
                    iss >> axis;
                    if (axis.empty()) axis = "X";
                    double pos = stage->getPos(axis.c_str());
                    std::cout << axis << " Position: " << pos << "\n";
                } else if (action == "move_abs") {
                    std::string axis;
                    double pos;
                    if (iss >> axis >> pos) {
                        stage->moveAbs(axis.c_str(), pos);
                        std::cout << "Moving " << axis << " to " << pos << "\n";
                    } else {
                        std::cout << "Usage: stage move_abs [axis] [pos]\n";
                    }
                } else if (action == "wait") {
                    std::string axis;
                    if (iss >> axis) {
                        stage->waitOnTarget(axis.c_str(), 10000);
                        std::cout << "Wait complete.\n";
                    }
                } else {
                    std::cout << "Unknown stage action: " << action << "\n";
                }
            } else if (target == "andor") {
                if (action == "connect") {
                    int cameraIndex = 0;
                    if (!(iss >> cameraIndex)) {
                        cameraIndex = 0;
                    }
                    cam->loadDLL("atmcd64d.dll");
                    cam->selectCamera(cameraIndex);
                    cam->initialize("");
                    std::cout << "Andor initialized using camera " << cameraIndex << ". X=" << cam->getXPixels() << ", Y=" << cam->getYPixels() << "\n";
                } else if (action == "cameras") {
                    int count = cam->getAvailableCameras();
                    std::cout << "Andor cameras available: " << count << "\n";
                } else if (action == "selectCamera") {
                    int cameraIndex;
                    if (iss >> cameraIndex) {
                        cam->selectCamera(cameraIndex);
                        cam->initialize("");
                        std::cout << "Selected Andor camera " << cameraIndex << ". X=" << cam->getXPixels() << ", Y=" << cam->getYPixels() << "\n";
                    } else {
                        std::cout << "Usage: andor selectCamera [cameraIndex]\n";
                    }
                } else if (action == "cooling") {
                    std::string mode;
                    if (iss >> mode) {
                        if (mode == "on") {
                            cam->enableCooling(true);
                            std::cout << "Andor cooling enabled.\n";
                        } else if (mode == "off") {
                            cam->enableCooling(false);
                            std::cout << "Andor cooling disabled.\n";
                        } else {
                            std::cout << "Usage: andor cooling on|off\n";
                        }
                    } else {
                        std::cout << "Usage: andor cooling on|off\n";
                    }
                } else if (action == "setTemp") {
                    int tempC;
                    if (iss >> tempC) {
                        cam->setCoolingTemperature(tempC);
                        std::cout << "Andor cooling target set to " << tempC << " C.\n";
                    } else {
                        std::cout << "Usage: andor setTemp [celsius]\n";
                    }
                } else if (action == "getTemp") {
                    std::cout << "Andor cooling temperature: " << cam->getCoolingTemperature() << " C\n";
                } else if (action == "measurebg") {
                    cam->measureBackground(0.1f);
                    std::cout << "Background captured for the current camera.\n";
                } else if (action == "disconnect") {
                    cam->shutdown();
                    std::cout << "Andor shutdown.\n";
                } else if (action == "abort") {
                    cam->abortAcquisition();
                    std::cout << "Acquisition aborted.\n";
                }
                else if (action == "measure") {
                    cam->configureSpectral(AndorCamera::ReadMode::FVB,
                                           AndorCamera::TriggerMode::Internal, 0.1f);
                    cam->startAcquisition();
                    cam->waitForAcquisition();
                    auto data = cam->getAllSpectra(1, cam->getXPixels());
                    std::cout << "Measured spectrum: ";
                    for (int i = 0; i < std::min(10, cam->getXPixels()); i++) {
                        std::cout << data[i] << " ";
                    }
                    std::cout << "...\n";
                    cam->testAcquireAndSave(data, 1, cam->getXPixels(), "measured_spectrum");
                } else if (action == "test") {
                    cam->testAcquireAndSave(0.1f, "test_spectrum");
                    std::cout << "Measured spectrum and sig-bg saved under the timestamped measurements folder when a background is available.\n";
                } else if (action == "setTint") {
                    float tint;
                    if (iss >> tint) {
                        // check if tint is between 1 ms and 10 s
                        if (tint < 1.0f) tint = 1.0f;
                        if (tint > 10000.0f) tint = 10000.0f;
                        else if (tint < 10.0f) tint = 10.0f; // Andor SDK may have issues with very short exposures
                        // convert tint from ms to seconds for the SDK
                        tint /= 1000.0f;
                        cam->configureSpectral(AndorCamera::ReadMode::FVB,
                                               AndorCamera::TriggerMode::External, tint);
                        std::cout << "Exposure time set to " << tint << " seconds.\n";
                    } else {
                        std::cout << "Usage: andor setTint [milliseconds]\n";
                    }
                } else if (action == "setReadMode") {
                    int mode;
                    if (iss >> mode) {
                        cam->setReadMode(mode);
                        std::cout << "Read Mode set to " << mode << ".\n";
                    } else {
                        std::cout << "Usage: andor setReadMode [mode]\n";
                    }
                } else if (action == "setAcquisitionMode") {
                    int mode;
                    if (iss >> mode) {
                        cam->setAcquisitionMode(mode);
                        std::cout << "Acquisition Mode set to " << mode << ".\n";
                    } else {
                        std::cout << "Usage: andor setAcquisitionMode [mode]\n";
                    }
                } else if (action == "setExposureTime") {
                    float seconds;
                    if (iss >> seconds) {
                        cam->setExposureTime(seconds);
                        std::cout << "Exposure Time set to " << seconds << " seconds.\n";
                    } else {
                        std::cout << "Usage: andor setExposureTime [seconds]\n";
                    }
                } else if (action == "setTriggerMode") {
                    int mode;
                    if (iss >> mode) {
                        cam->setTriggerMode(mode);
                        std::cout << "Trigger Mode set to " << mode << ".\n";
                    } else {
                        std::cout << "Usage: andor setTriggerMode [mode]\n";
                    }
                } else if (action == "setImage") {
                    int hbin, vbin, hstart, hend, vstart, vend;
                    if (iss >> hbin >> vbin >> hstart >> hend >> vstart >> vend) {
                        cam->setImage(hbin, vbin, hstart, hend, vstart, vend);
                        std::cout << "Image Region set: Binning(" << hbin << "x" << vbin 
                                  << "), H(" << hstart << "-" << hend 
                                  << "), V(" << vstart << "-" << vend << ").\n";
                    } else {
                        std::cout << "Usage: andor setImage [h] [v] [hs] [he] [vs] [ve]\n";
                    }
                } else if (action == "getStatus") {
                    int status = cam->getStatus();
                    std::cout << "Andor Status: " << status << "\n";
                } else if (action == "setKineticCycleTime") {
                    float seconds;
                    if (iss >> seconds) {
                        cam->setKineticCycleTime(seconds);
                        std::cout << "Kinetic Cycle Time set to " << seconds << " seconds.\n";
                    } else {
                        std::cout << "Usage: andor setKineticCycleTime [s]\n";
                    }
                } else if (action == "setNumberKinetics") {
                    int num;
                    if (iss >> num) {
                        cam->setNumberKinetics(num);
                        std::cout << "Number of Kinetics set to " << num << ".\n";
                    } else {
                        std::cout << "Usage: andor setNumberKinetics [num]\n";
                    }
                }
                else {
                    std::cout << "Unknown andor action: " << action << "\n";
                }
            } else if (target == "scan") {
                // Here we replicate runRasterScan using proxies!
                double xStart = 0.0, xStop = 2.0, xStep = 0.002;
                double yStart = 0.0, yStop = 2.0, yStep = 0.002;
                float exposureS = 0.002f;
                int trigCh = 1, pulseUs = 50;

                int nX = (int)std::round((xStop - xStart) / xStep) + 1;
                int nY = (int)std::round((yStop - yStart) / yStep) + 1;
                int nPix = cam->getXPixels();

                if (nPix == 0) {
                    std::cout << "Run 'andor connect' first.\n";
                    continue;
                }

                std::cout << "Scan: " << nX << " x " << nY << " points, " << nPix << " spectral px\n";

                stage->configureTriggerOutput(trigCh, "X", xStart, xStep, xStop, pulseUs);
                stage->enableTriggerOutput(trigCh, true);
                cam->configureFVBKinetic(exposureS, nX);

                stage->setupDataRecorder(1, "X", 2);
                stage->setRecordRate(4);
                stage->setRecordTrigger(3);

                stage->moveAbs("X", xStart);
                stage->moveAbs("Y", yStart);
                stage->waitOnTarget("X");
                stage->waitOnTarget("Y");

                std::vector<std::vector<std::vector<WORD>>> cube(
                    nY, std::vector<std::vector<WORD>>(nX, std::vector<WORD>(nPix, 0)));

                for (int iy = 0; iy < nY; iy++) {
                    double yPos = yStart + iy * yStep;
                    stage->moveAbs("Y", yPos);
                    stage->moveAbs("X", xStart);
                    stage->waitOnTarget("Y");
                    stage->waitOnTarget("X");

                    cam->startAcquisition();
                    stage->setWaitOnGo("X", 0x1);
                    stage->moveAbs("X", xStop);

                    cam->waitForAcquisition();
                    std::vector<WORD> lineData = cam->getAllSpectra(nX, nPix);
                    for (int ix = 0; ix < nX; ix++) {
                        cube[iy][ix].assign(lineData.begin() + ix * nPix,
                                            lineData.begin() + (ix + 1) * nPix);
                    }
                    stage->waitOnTarget("X");
                    std::cout << "Line " << iy + 1 << "/" << nY << " done\n";
                }

                stage->enableTriggerOutput(trigCh, false);
                std::cout << "Scan finished.\n";
            } else {
                std::cout << "Unknown target: " << target << "\n";
            }
        } 
       catch (const std::exception& e) {
            std::cout << "Error executing command: " << e.what() << "\n";
        }
    }

    std::cout << "Exiting...\n";
    // Optional: send ExitServer to pipes so they close cleanly
    return 0;
}