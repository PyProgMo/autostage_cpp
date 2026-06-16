// ConsoleApp.cpp
#include "PIStageProxy.h"
#include "AndorCameraProxy.h"
#include "RasterScan.h"
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
#include <thread>

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

    // init empty metadata in the camera proxy, so that it can be updated from the console app when the user enters metadata, and then saved with each spectrum without having to pass it back and forth with every save command
    cam->specmeta_ = SpectrumMetadata();

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
            std::cout << "  stage pos [axis] \n";
            std::cout << "  stage qpos\n";
            std::cout << "  stage moveto [x] [y] [z] (in nm)\n";
            std::cout << "  stage adda [vx] [vy] [vz] (in nm/s)\n";
            std::cout << "  stage halt\n";
            std::cout << "  stage velocitytest [velocity_nm_s] [x_distance_nm]\n";
            std::cout << "  stage rowcorrected [duration_s] [x_distance_nm] [log 0|1]\n";
            std::cout << "  stage m [axis] [pos] (in nm)\n";
            std::cout << "  stage wait [axis]\n";
            std::cout << "  andor connect [cameraIndex]\n";
            std::cout << "  andor cameras\n";
            std::cout << "  andor selectCamera [cameraIndex]\n";
            std::cout << "  andor cooling on|off\n";
            std::cout << "  andor setTemp [celsius]\n";
            std::cout << "  andor getTemp\n";
            std::cout << "  andor initspec\n"; 
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
            std::cout << "  andor testfunctions\n";
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
                } else if (action == "pos") {
                    std::string axis;
                    iss >> axis;
                    if (axis.empty()) axis = "1";
                    double pos = stage->getPos(axis.c_str())*1e3; // convert µm to nm for user
                    std::cout << axis << " Position: " << pos << "\n";
                } else if (action == "qpos") { // print x y z positions
                    auto pos = stage->qpos();
                    std::cout << "X: " << pos[0] << " Y: " << pos[1] << " Z: " << pos[2] << "\n";
                } else if (action == "halt") {
                    stage->halt();
                    std::cout << "Stage halt requested.\n";
                } else if (action == "velocitytest") {
                    double velocityNmPerS;
                    double xDistanceNm;
                    if (iss >> velocityNmPerS >> xDistanceNm) {
                        RasterScan::runOneRowTest(*stage, *cam, velocityNmPerS, xDistanceNm);
                        std::cout << "Velocity test completed.\n";
                    } else {
                        std::cout << "Usage: stage velocitytest [velocity_nm_s] [x_distance_nm]\n";
                    }
                } else if (action == "rowcorrected") {
                    double durationS;
                    double xDistanceNm;
                    int logFlag = 0;
                    if (iss >> durationS >> xDistanceNm) {
                        if (!(iss >> logFlag)) {
                            logFlag = 0;
                        }
                        RasterScan::runRowCorrected(*stage, 
                                                     *cam, 
                                                     durationS,
                                                     xDistanceNm,
                                                     logFlag != 0);
                        std::cout << "Row corrected test completed.\n";
                    } else {
                        std::cout << "Usage: stage rowcorrected [duration_s] [x_distance_nm] [log 0|1]\n";
                    }
                } else if (action == "moveto") {
                    double x, y, z;
                    if (iss >> x >> y >> z) {
                        stage->moveto(x, y, z); // conversion is handled inside the proxy
                        std::cout << "Moving X/Y/Z to " << x << " " << y << " " << z << " nm\n";
                    } else {
                        std::cout << "Usage: stage moveto [x] [y] [z] (in nm)\n";
                    }
                } else if (action == "adda") {
                    double vx, vy, vz;
                    if (iss >> vx >> vy >> vz) {
                        stage->adda(vx, vy, vz);
                        std::cout << "Setting X/Y/Z velocities to " << vx << " " << vy << " " << vz << " nm/s\n";
                    } else {
                        std::cout << "Usage: stage adda [vx] [vy] [vz] (in nm/s)\n";
                    }
                } else if (action == "m") {
                    std::string axis;
                    double pos;
                    if (iss >> axis >> pos) {
                        stage->moveAbs(axis.c_str(), pos);
                        std::cout << "Moving " << axis << " to " << pos << "\n";
                    } else {
                        std::cout << "Usage: stage m [axis] [pos] (in nm)\n";
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
                    cam->measureBackground();
                    std::cout << "Background captured for the current camera.\n";
                } else if (action == "disconnect") {
                    cam->shutdown();
                    std::cout << "Andor shutdown.\n";
                } else if (action == "abort") {
                    cam->abortAcquisition();
                    std::cout << "Acquisition aborted.\n";
                } else if (action == "measure") {
                    cam->startAcquisition();
                    cam->waitForAcquisition();
                    auto data = cam->getAllSpectra(1, cam->getXPixels());
                    std::cout << "Measured spectrum: \n";
                    //cam->testAcquireAndSave(data, 1, cam->getXPixels(), "measured_spectrum");
                    cam->getMetadata(cam->specmeta_);
                    cam->savespecfast("measurements", data, 1, cam->getXPixels(), cam->specmeta_, "measured_spectrum");
                    std::cout << "saved spectrum";
                } else if (action == "initspec") {
                    // set integration time to 100 ms, set read mode to FVB, set trigger mode to external, and start acquisition, but do not wait for it to finish, so that the camera is ready and waiting for the trigger when the user is ready to measure
                    cam->configureSpectral(AndorCamera::ReadMode::FVB,
                                           AndorCamera::TriggerMode::Internal, 0.1f, 1);
                }
                else if (action == "setTint") {
                    float tint;
                    if (iss >> tint) {
                        // check if tint is between 1 ms and 10 s
                        if (tint < 1.0f) tint = 1.0f;
                        if (tint > 10000.0f) tint = 10000.0f;
                        else if (tint < 10.0f) tint = 10.0f; // Andor SDK may have issues with very short exposures
                        // convert tint from ms to seconds for the SDK
                        tint /= 1000.0f;
                        cam->configureSpectral(AndorCamera::ReadMode::FVB,
                                               AndorCamera::TriggerMode::External, tint, 1);
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
                } else if (action == "testfunctions") {
                    std::cout << "andor testfunctions:\n";
                    std::cout << "  andor test -> testmeasurement\n";
                    std::cout << "  andor testtiming -> measure 100 spectra with 0.1 s exposure, also save them to disk, important: print how loong it took\n (important: timing uses windows chorono)\n"; 
                    std::cout << " andor printmeta -> print the current metadata stored in the proxy\n";
                } else if (action == "printmeta") {
                    cam->getMetadata(cam->specmeta_);
                    // deserialize the metadata
                    
                    std::cout << "Current metadata in proxy:\n";
                    std::cout << "  ExposureTime: " << cam->specmeta_.date << " s\n";
                    std::cout << "  Temperature: " << cam->specmeta_.userName << " C\n";
                } else if (action == "test") {
                    cam->testAcquireAndSave(0.1f, "test_spectrum");
                    std::cout << "Measured spectrum and sig-bg saved under the timestamped measurements folder when a background is available.\n";
                } else if (action == "testtiming") {
                    cam->testtenspectime();
                }
                else {
                    std::cout << "Unknown andor action: " << action << "\n";
                }
            } else if (target == "scan") {
                // Here we replicate runRasterScan using proxies!
                // defaults are in nanometres (nm)
                double xStart = 0.0, xStop = 2000000.0, xStep = 2000.0; // 2.0 mm -> 2000000 nm, 0.002 mm -> 2000 nm
                double yStart = 0.0, yStop = 2000000.0, yStep = 2000.0;
                float exposureS = 0.1f;
                int trigCh = 1, pulseUs = 50;

                int nX = (int)std::round((xStop - xStart) / xStep) + 1;
                int nY = (int)std::round((yStop - yStart) / yStep) + 1;
                int nPix = cam->getXPixels();

                if (nPix == 0) {
                    std::cout << "Run 'andor connect' first.\n";
                    continue;
                }

                std::cout << "Scan: " << nX << " x " << nY << " points, " << nPix << " spectral px\n";

                stage->configureTriggerOutput(trigCh, "1", xStart, xStep, xStop, pulseUs);
                stage->enableTriggerOutput(trigCh, true);
                cam->configureFVBKinetic(exposureS, nX);

                stage->setupDataRecorder(1, "1", 2);
                stage->setRecordRate(4);
                stage->setRecordTrigger(3);

                stage->moveAbs("1", xStart);
                stage->moveAbs("2", yStart);
                stage->waitOnTarget("1");
                stage->waitOnTarget("2");

                std::vector<std::vector<std::vector<int>>> cube(
                    nY, std::vector<std::vector<int>>(nX, std::vector<int>(nPix, 0)));

                for (int iy = 0; iy < nY; iy++) {
                    double yPos = yStart + iy * yStep;
                    stage->moveAbs("2", yPos);
                    stage->moveAbs("1", xStart);
                    stage->waitOnTarget("2");
                    stage->waitOnTarget("1");

                    cam->startAcquisition();
                    stage->setWaitOnGo("1", 0x1);
                    stage->moveAbs("1", xStop);

                    cam->waitForAcquisition();
                    std::vector<int> lineData = cam->getAllSpectra(nX, nPix);
                    for (int ix = 0; ix < nX; ix++) {
                        cube[iy][ix].assign(lineData.begin() + ix * nPix,
                                            lineData.begin() + (ix + 1) * nPix);
                    }
                    stage->waitOnTarget("1");
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