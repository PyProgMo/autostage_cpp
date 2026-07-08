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

    try {
        stage = std::make_unique<PIStageProxy>();
        cam = std::make_unique<AndorCameraProxy>();
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect to IPC pipes: " << e.what() << "\n";
        return 1;
    }

    // init empty metadata in the camera proxy, so that it can be updated from the console app when the user enters metadata, and then saved with each spectrum without having to pass it back and forth with every save command
    try{
        cam->specmeta_ = cam->getMetadata();
    } catch (const std::exception& e) {
        std::cerr << "Failed to get initial metadata from camera proxy: " << e.what() << "\n";
        // initialize with empty metadata if we can't get it from the proxy
        cam->specmeta_ = SpectrumMetadata();
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
            std::cout << "  init\n";
            std::cout << "  stage connect\n";
            std::cout << "  stage disconnect\n";
            std::cout << "  stage pos [axis] \n";
            std::cout << "  stage qpos\n";
            std::cout << "  stage moveto [x] [y] [z] (in nm)\n";
            std::cout << "  stage adda [vx] [vy] [vz] (in nm/s)\n";
            std::cout << "  stage halt\n";
            std::cout << "  stage velocitytest [spec_int_ms] [x_distance_nm] [stepsize_nm] [log 0|1] [tdead_ms]\n";
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
            std::cout << "  andor measureandsave [foldername] [nspecs]\n";
            std::cout << "  andor measurekinetic [nspecs]\n";
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
            std::cout << "  andor 1 (measure on specified camera with current settings)\n";
            std::cout << "  scan1\n";
            std::cout << "  scan2\n";
            continue;
        }

        std::istringstream iss(cmd);
        std::string target, action;
        iss >> target >> action;

        try {
            if (target == "init") {
                // Initialize both stage and camera
                // 1. connect to stage
                stage->loadDLL("E7XX_GCS2_DLL.dll");
                stage->connect("109021162");
                std::cout << "Stage connected.\n";
                // 2. connect to camera
                int cameraIndex = 0;
                cam->loadDLL("atmcd64d.dll");
                cam->selectCamera(cameraIndex);
                cam->initialize("");
                std::cout << "Andor initialized using camera " << cameraIndex << ". X=" << cam->getXPixels() << ", Y=" << cam->getYPixels() << "\n";
                // 3. initialize empty metadata in the camera proxy and run andor initspec
                cam->specmeta_ = cam->getMetadata(); 
                cam->configureSpectral(AndorCamera::ReadMode::FVB,
                        AndorCamera::TriggerMode::Internal, 0.1f, 1);
                // 4. measure background
                std::string bgFilename = "background.txt";
                cam->measureBackground(bgFilename);

            }
            else if (target == "stage") {
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
                    double pos = stage->getPos(axis.c_str())*1e3; // convert µm to nm for user, x=1, y=2, z=3
                    std::cout << axis << " Position: " << pos << "\n";
                } else if (action == "qpos") { // print x y z positions
                    auto pos = stage->qpos();
                    std::cout << "X: " << pos[0] << " Y: " << pos[1] << " Z: " << pos[2] << "\n";
                } else if (action == "halt") {
                    stage->halt();
                    std::cout << "Stage halt requested.\n";
                } else if (action == "velocitytest") {
                    double t_measure;
                    double tdead=60; // default dead time in ms
                    double xDistanceNm;
                    double stepsize_nm;
                    bool logFlag = false;
                    if (iss >> t_measure >> xDistanceNm >> stepsize_nm >> logFlag >> tdead) {
                        RasterScan::runOneRowTest(*stage, *cam, t_measure, xDistanceNm, stepsize_nm, logFlag, tdead);
                        std::cout << "Velocity test completed.\n";
                    } else {
                        std::cout << "Usage: stage velocitytest [spec_int_ms] [x_distance_nm] [stepsize_nm] [log 0|1] [tdead_ms]\n";
                    }
                } else if (action == "rowcorrected") {
                    double durationS;
                    double xDistanceNm;
                    double stepsize_nm;
                    bool logFlag = false;
                    if (iss >> durationS >> xDistanceNm >> stepsize_nm >> logFlag) {
                        RasterScan::runRowCorrected(*stage, 
                                                     *cam, 
                                                     durationS,
                                                     xDistanceNm,
                                                     stepsize_nm,
                                                     logFlag);
                        std::cout << "Row corrected test completed.\n";
                    } else {
                        std::cout << "Usage: stage rowcorrected [duration_s] [x_distance_nm] [stepsize_nm] [log 0|1]\n";
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
                } else if (action == "1") {
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
                    try {
                        SpectrumMetadata meta = cam->specmeta_; // get current metadata from the proxy
                        std::cout << "Metadata - Date: " << meta.date << ", User: " << meta.userName << ", File: " << meta.fileName << "\n";
                    }
                        catch (const std::exception& e) {
                            std::cerr << "Failed to get metadata from proxy: " << e.what() << "\n";
                            SpectrumMetadata meta; // use empty metadata if we can't get it
                        }
                    try {
                        std::cout << "Saving spectrum...\n";
                        cam-> savespecfast("measurements", data, 1, cam->getXPixels(), cam->specmeta_, "measured_spectrum");
                    } catch (const std::exception& e) {
                        std::cerr << "Failed to save spectrum: " << e.what() << "\n";
                    }
                    std::cout << "saved spectrum";

                } else if (action == "measureandsave") {
                    std::string foldername;
                    int nspecs;
                    if (iss >> foldername >> nspecs) {
                        cam->MeasureAndSaveNSpecs(foldername, nspecs);
                        std::cout << "Measured and saved " << nspecs << " spectra to folder: " << foldername << "\n";
                    } else {
                        std::cout << "Usage: andor measureandsave [foldername] [nspecs]\n";
                    }
                } else if (action == "measurekinetic") {
                    int nspecs = 2; // default to 2 spectra if not specified
                    if (iss >> nspecs) {
                        // set mode to kinetic, set trigger to internal, and acquire nspecs spectra
                        /* old order:
                        cam->setReadMode(0); // 0 = FVB
                        cam->setAcquisitionMode(3); // 3 = kinetic
                        cam->setTriggerMode(0); // 0 = internal trigger
                        cam->setKineticCycleTime(0.000f); // minimum cycle time
                        cam->setNumberKinetics(nspecs);
                        cam->setExposureTime(0.1f); // 100 ms exposure time
                        cam->startAcquisition();
                        cam->waitForAcquisition();
                        new order: */
                        cam->setAcquisitionMode(3);       // FIRST - must be before everything
                        cam->setReadMode(0);              // FVB
                        cam->setNumberKinetics(nspecs);   // before cycle time
                        cam->setKineticCycleTime(0.0f);   // after number kinetics
                        cam->setExposureTime(0.1f);
                        cam->setTriggerMode(0);           // last
                        cam->startAcquisition();
                        cam->waitForAcquisition();

                        int acquired = 0;
                        cam->getTotalNumberImagesAcquired(acquired); // sanity checkd
                        std::cout << "Frames acquired: " << acquired << " (expected " << nspecs << ")\n";

                        auto data = cam->getAllSpectra(nspecs, cam->getXPixels());
                        SpectrumMetadata meta = cam->specmeta_; // get current metadata from the proxy
                        cam-> savespecfast("measurements", data, nspecs, cam->getXPixels(), cam->specmeta_, "measured_spectrum");
                        std::cout << "Measured " << nspecs << " spectra in kinetic mode.\n";
                    } else {
                        std::cout << "Usage: andor measurekinetic [nspecs]\n";
                    }
                }
                else if (action == "1") {
                    cam->startAcquisition();
                    cam->waitForAcquisition();
                    auto data = cam->getAllSpectra(1, cam->getXPixels());
                    SpectrumMetadata meta = cam->specmeta_; // get current metadata from the proxy
                    cam-> savespecfast("measurements", data, 1, cam->getXPixels(), cam->specmeta_, "measured_spectrum");
                }
                else if (action == "initspec") {
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
                    std::cout << "  andor test [bool_save] [num_spectra] -> testmeasurement\n";
                    std::cout << "  andor testtiming -> measure 100 spectra with 0.1 s exposure, also save them to disk, important: print how loong it took\n (important: timing uses windows chorono)\n"; 
                    std::cout << " andor setuptest [tint_ms] -> measure num_spectra spectra and save if bool_save is true\n";
                    std::cout << " andor printmeta -> print the current metadata stored in the proxy\n";
                } else if (action == "printmeta") {
                    try {
                        SpectrumMetadata meta = cam->getMetadata();
                        // deserialize the metadata
                        std::cout << "Current metadata in proxy:\n";
                        std::cout << "  ExposureTime: " << meta.date << " s\n";
                        std::cout << "  Temperature: " << meta.userName << " C\n";
                        std::cout << "  SlitWidth: " << meta.slitWidthUm << " um\n";
                        std::cout << "  Grating: " << meta.grating << "\n";}
                    catch (const std::exception& e) {
                        std::cout << "Error retrieving metadata: " << e.what() << "\n";
                    }
                } else if (action == "setuptest"){
                    float tint;
                    if (iss >> tint) {
                        cam->configureSpectral(AndorCamera::ReadMode::FVB,
                                               AndorCamera::TriggerMode::Internal, tint/1000.0f, 1);
                        std::cout << "Test setup: Tint=" << tint << " ms" << "\n";
                    } else {
                        std::cout << "Usage: andor setuptest [tint_ms]\n";
                    }
                } else if (action == "test") {
                    bool boolSave = false; // set to false to disable saving and just test acquisition speed without disk writing influence
                    int num_spectra = 10; // default to 10 spectra if not specified
                    if (iss >> boolSave >> num_spectra) {
                        std::cout << "Test: boolSave=" << boolSave << ", num_spectra=" << num_spectra << "\n";
                    } else {
                        std::cout << "Usage: andor test [bool_save] [num_spectra], default is false\n";
                        boolSave = false;
                        num_spectra = 10;
                    }

                    // print if spectrum saving is enabled or not, and where the spectra will be saved if it is enabled
                    std::cout << "Spectrum saving is " << (boolSave ? "enabled" : "disabled") << ".\n";
                    // run test
                    // foldername: start DD.MM.YYYY_HH-MM-SS, end with a backslash, and save all spectra from this test in that folder, so we can easily compare the results of different tests by looking at the folders
                    std::string foldername = "measurements/test_measurement_" + std::to_string(std::time(nullptr)) + "\\";
                    // create the folder if it doesn't exist
                    CreateDirectoryA(foldername.c_str(), NULL);
                    std::cout << "Spectra from this test will be saved in folder: " << foldername << "\n";
                    // measure time to acruire and save 100 spectra with 0.1 s exposure, using the testAcquireAndSave function, and print how long it took
                    auto start = std::chrono::high_resolution_clock::now();
                    std::string filename;
                    std::vector<int> data;
                    SpectrumMetadata meta;
                    // set tint to 10 ms for faster testing
                    cam->configureSpectral(AndorCamera::ReadMode::FVB,
                                           AndorCamera::TriggerMode::Internal, 0.01f, 1);
                    for (int i = 0; i < num_spectra; i++) {

                        /* we put this 2 calls into one single one
                        cam->startAcquisition();
                        cam->waitForAcquisition();
                        auto data = cam->getAllSpectra(1, cam->getXPixels());
                        SpectrumMetadata meta = cam->specmeta_; // get current metadata from the proxy
                        */
                        // The function will modify 'data' and 'meta' directly in place!
                        //std::cout << "Acquiring spectrum " << (i+1) << "/" << num_spectra << "...\n";
                        cam->AcquireAndFetchSingle(cam->getXPixels(), data, meta);
                        //std::cout << "Acquired spectrum " << (i+1) << "/" << num_spectra << ".\n";

                        if (boolSave) {
                            filename = "spectrum_" + std::to_string(i) + ".txt";
                            //cam-> savespecfast(foldername, data, 1, cam->getXPixels(), cam->specmeta_, filename); <- run this one assync
                            std::thread saveThread([cam=cam.get(), data, foldername, meta, filename]() {
                                try {
                                    cam->savespecfast(foldername, data, 1, cam->getXPixels(), meta, filename);
                                } catch (const std::exception& e) {
                                    std::cerr << "Failed to save " << filename << ": " << e.what() << "\n";
                                }
                            });
                            saveThread.join(); // Wait for the save operation to complete
                        }
                        // disable saving to test the acquisition speed without the influence of disk writing, and just save the last spectrum after the loop to verify that saving still works

                        /* this one is really trash
                        // 1. Tell the server/hardware to physically start a new exposure
                        cam->startAcquisition(); 
                        
                        // 2. CRITICAL: This blocks the loop for exactly 100ms while the camera integrates light!
                        cam->waitForAcquisition(); 
                        
                        // 3. Now that the exposure is physically done, fetch the valid frame data
                        std::vector<int> currentFrame = cam->getAllSpectra(1, cam->getXPixels());
                        
                        // 4. Hand it off to the lightning-fast asynchronous saver we built
                        cam->savefast1(currentFrame, 1, cam->getXPixels(), foldername + "measurement_folder");*/
                        }
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double> elapsed = end - start;
                    std::cout << "Time to acquire and save " << num_spectra << " spectra: " << elapsed.count() << " seconds\n";
                    // calculate the average time per spectrum
                    std::cout << "Average time per spectrum: " << (elapsed.count() / static_cast<double>(num_spectra)) << " seconds\n";

                } else if (action == "testtiming") {
                    cam->testtenspectime(); // this crashes the program
                }
                else {
                    std::cout << "Unknown andor action: " << action << "\n";
                }
            } else if (target == "scan1") {
                // Here we replicate runRasterScan using proxies!
                // defaults are in nanometres (nm)
                double xStart = 0.0, xStop = 200000.0, xStep = 2000.0; // 2.0 mm -> 2000000 nm, 0.002 mm -> 2000 nm
                double yStart = 0.0, yStop = 200000.0, yStep = 2000.0;
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
                std::cout << "Scan finished.Save data...\n";
                // save each spectrum as a separate file in a folder named "scan_data" with filenames "spectrum_N.txt" write position and time into the metadata for each spectrum, where N is a running number starting from 0
                std::string foldername = "scan_data/scan_" + std::to_string(std::time(nullptr)) + "\\";
                CreateDirectoryA(foldername.c_str(), NULL);
                SpectrumMetadata meta; // create a single metadata object that we will update with the correct position and time for each spectrum, and then pass it to the save function, so we don't have to create a new metadata object for each spectrum
                for (int iy = 0; iy < nY; iy++) {
                    for (int ix = 0; ix < nX; ix++) {
                        std::string filename = foldername + "spectrum_" + std::to_string(iy * nX + ix) + ".txt";
                        // update metadata with position and time
                        SpectrumMetadata meta;
                        meta.xPosNm = xStart + ix * xStep;
                        meta.yPosNm = yStart + iy * yStep;
                        meta.date = exposureS; // just to have some value in there, since we don't have a real timestamp for each spectrum in this scan
                        cam->savespecfast(foldername, cube[iy][ix], 1, nPix, meta, "spectrum_" + std::to_string(iy * nX + ix) + ".txt");
                    }
                }

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