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

    Sleep(2000); // Give servers time to bind to pipes

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
            std::cout << "  stage get_pos [axis]\n";
            std::cout << "  stage move_abs [axis] [pos]\n";
            std::cout << "  stage wait [axis]\n";
            std::cout << "  andor connect\n";
            std::cout << "  andor measure\n";
            std::cout << "  andor setTint [milliseconds]\n";
            std::cout << "  andor save_test_spectrum\n";
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
                    cam->loadDLL("atmcd64d.dll");
                    cam->initialize("");
                    std::cout << "Andor initialized. X=" << cam->getXPixels() << ", Y=" << cam->getYPixels() << "\n";
                } else if (action == "disconnect") {
                    cam->shutdown();
                    std::cout << "Andor shutdown.\n";
                } else if (action == "abort") {
                    cam->abortAcquisition();
                    std::cout << "Acquisition aborted.\n";
                }
                else if (action == "measure") {
                    cam->configureSpectral(AndorCamera::ReadMode::FVB,
                                           AndorCamera::TriggerMode::External, 0.1f);
                    cam->startAcquisition();
                    cam->waitForAcquisition();
                    auto data = cam->getAllSpectra(1, cam->getXPixels());
                    std::cout << "Measured spectrum: ";
                    for (int i = 0; i < std::min(10, cam->getXPixels()); i++) {
                        std::cout << data[i] << " ";
                    }
                    std::cout << "...\n";
                } else if (action == "save_test_spectrum") {
                    cam->testAcquireAndSave(0.1f, "test_spectrum.png");
                    std::cout << "Measured spectrum saved to test_spectrum.png\n";
                } else if (action == "setTint") {
                    float tint;
                    if (iss >> tint) {
                        // check if tint is between 1 ms and 10 s
                        if (tint < 1.0f) tint = 1.0f;
                        if (tint > 10000.0f) tint = 10000.0f;
                        else if (tint < 10.0f) tint = 10.0f; // Andor SDK may have issues with very short exposures
                        cam->configureSpectral(AndorCamera::ReadMode::FVB,
                                               AndorCamera::TriggerMode::External, tint);
                        std::cout << "Exposure time set to " << tint/1000.0f << " seconds.\n";
                    } else {
                        std::cout << "Usage: andor setTint [milliseconds]\n";
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