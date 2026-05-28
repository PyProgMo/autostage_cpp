// SpectrometerServer.cpp
#include <Windows.h>
#include <iostream>
#include <vector>
#include "AndorCamera.h"
#include "IpcStructs.h"
#include "Logger.h"

#include <sstream>

void ProcessClient(HANDLE hPipe) {
    AndorCamera cam;
    bool running = true;

    AppLogger::instance().info("SpectrometerServer: client handler started");

    while (running) {
        IpcMessage req = {};
        DWORD bytesRead = 0;
        BOOL result = ReadFile(hPipe, &req, sizeof(req), &bytesRead, NULL);

        if (!result || bytesRead == 0) {
            DWORD gle = GetLastError();
            if (gle == ERROR_BROKEN_PIPE) {
                AppLogger::instance().info("SpectrometerServer: client disconnected");
            } else {
                AppLogger::instance().error(std::string("SpectrometerServer: read failed, GLE=") + std::to_string(gle));
            }
            break;
        }

        IpcMessage res = {};
        res.command = req.command;
        res.status = 0;

        AppLogger::instance().info(std::string("SpectrometerServer: processing command ") + ipcCommandName(req.command));

        try {
            switch (req.command) {
            case IpcCommand::AndorLoadDLL:
                AppLogger::instance().info(std::string("SpectrometerServer: LoadDLL path='") + req.strArg + "'");
                cam.loadDLL(req.strArg);
                break;
            case IpcCommand::AndorInitialize:
                AppLogger::instance().info(std::string("SpectrometerServer: Initialize dir='") + req.strArg + "'");
                cam.initialize(req.strArg);
                res.iArgs[0] = cam.getXPixels();
                res.iArgs[1] = cam.getYPixels();
                break;
            case IpcCommand::AndorGetAvailableCameras:
                AppLogger::instance().info("SpectrometerServer: getAvailableCameras");
                res.iArgs[0] = cam.getAvailableCameras();
                break;
            case IpcCommand::AndorSelectCamera:
                AppLogger::instance().info(std::string("SpectrometerServer: selectCamera index=") + std::to_string(req.iArgs[0]));
                cam.selectCamera(req.iArgs[0]);
                break;
            case IpcCommand::AndorEnableCooling:
                AppLogger::instance().info(std::string("SpectrometerServer: enableCooling enable=") + (req.iArgs[0] != 0 ? "true" : "false"));
                cam.enableCooling(req.iArgs[0] != 0);
                break;
            case IpcCommand::AndorSetCoolingTemperature:
                AppLogger::instance().info(std::string("SpectrometerServer: setCoolingTemperature tempC=") + std::to_string(req.iArgs[0]));
                cam.setCoolingTemperature(req.iArgs[0]);
                break;
            case IpcCommand::AndorGetCoolingTemperature:
                AppLogger::instance().info("SpectrometerServer: getCoolingTemperature");
                res.iArgs[0] = cam.getCoolingTemperature();
                break;
            case IpcCommand::AndorIsCoolingEnabled:
                AppLogger::instance().info("SpectrometerServer: isCoolingEnabled");
                res.iArgs[0] = cam.isCoolingEnabled() ? 1 : 0;
                break;
            case IpcCommand::AndorShutDown:
                cam.shutdown();
                AppLogger::instance().info("SpectrometerServer: shutdown");
                break;
            case IpcCommand::AndorSetReadMode:
                AppLogger::instance().info("SpectrometerServer: setReadMode");
                cam.setReadMode(req.iArgs[0]);
                break;
            case IpcCommand::AndorSetAcquisitionMode:
                AppLogger::instance().info("SpectrometerServer: setAcquisitionMode");
                cam.setAcquisitionMode(req.iArgs[0]);
                break;
            case IpcCommand::AndorSetExposureTime:
                AppLogger::instance().info("SpectrometerServer: setExposureTime");
                cam.setExposureTime(static_cast<float>(req.dArgs[0]));
                break;
            case IpcCommand::AndorSetTriggerMode:
                AppLogger::instance().info("SpectrometerServer: setTriggerMode");
                cam.setTriggerMode(req.iArgs[0]);
                break;
            case IpcCommand::AndorSetImage:
                AppLogger::instance().info("SpectrometerServer: setImage");
                cam.setImage(req.iArgs[0], req.iArgs[1], req.iArgs[2], req.iArgs[3], req.iArgs[4], req.iArgs[5]);
                break;
            case IpcCommand::AndorGetStatus: {
                AppLogger::instance().info("SpectrometerServer: getStatus");
                int status = cam.getStatus();
                res.iArgs[0] = status;
                break;
            }
            case IpcCommand::AndorSetKineticCycleTime:
                AppLogger::instance().info("SpectrometerServer: setKineticCycleTime");
                cam.setKineticCycleTime(static_cast<float>(req.dArgs[0]));
                break;
            case IpcCommand::AndorSetNumberKinetics:
                AppLogger::instance().info("SpectrometerServer: setNumberKinetics");
                cam.setNumberKinetics(req.iArgs[0]);
                break;
            case IpcCommand::AndorConfigureSpectral:
                AppLogger::instance().info("SpectrometerServer: configureSpectral");
                cam.configureSpectral(static_cast<AndorCamera::ReadMode>(req.iArgs[0]),
                                      static_cast<AndorCamera::TriggerMode>(req.iArgs[1]),
                                      static_cast<float>(req.dArgs[0]),
                                      req.iArgs[2]);
                break;
            case IpcCommand::AndorConfigureFVBKinetic:
                AppLogger::instance().info("SpectrometerServer: configureFVBKinetic");
                cam.configureFVBKinetic(static_cast<float>(req.dArgs[0]), req.iArgs[0]);
                break;
            case IpcCommand::AndorStartAcquisition:
                AppLogger::instance().info("SpectrometerServer: StartAcquisition");
                cam.startAcquisition();
                break;
            case IpcCommand::AndorAbortAcquisition:
                AppLogger::instance().info("SpectrometerServer: AbortAcquisition");
                cam.abortAcquisition();
                break;
            case IpcCommand::AndorWaitForAcquisition:
                AppLogger::instance().info("SpectrometerServer: WaitForAcquisition");
                cam.waitForAcquisition();
                break;
            case IpcCommand::AndorGetImages16: {
                int numSpectra = req.iArgs[0];
                int pixelsPerSpectrum = req.iArgs[1];
                AppLogger::instance().info("SpectrometerServer: getAllSpectra");
                auto data = cam.getAllSpectra(numSpectra, pixelsPerSpectrum);
                res.dataSize = (int32_t)(data.size() * sizeof(WORD));
                
                DWORD bytesWritten = 0;
                if (!WriteFile(hPipe, &res, sizeof(res), &bytesWritten, NULL)) {
                    std::cerr << "Write response header failed\n";
                    break;
                }

                if (res.dataSize > 0) {
                    if (!WriteFile(hPipe, data.data(), res.dataSize, &bytesWritten, NULL)) {
                        std::cerr << "Write response payload failed\n";
                        break;
                    }
                }
                continue; 
            }
            case IpcCommand::ExitServer:
                AppLogger::instance().info("SpectrometerServer: ExitServer received");
                running = false;
                break;
            default:
                AppLogger::instance().error(std::string("SpectrometerServer: unknown command ID"));
                res.status = -1;
                break;
            }
        } catch (const std::exception& e) {
            AppLogger::instance().error(std::string("SpectrometerServer: exception: ") + e.what());
            res.status = -1;
            strncpy(res.strArg, e.what(), sizeof(res.strArg) - 1);
        }

        if (req.command != IpcCommand::AndorGetImages16) {
            DWORD bytesWritten = 0;
            if (!WriteFile(hPipe, &res, sizeof(res), &bytesWritten, NULL)) {
                AppLogger::instance().error("SpectrometerServer: write response failed");
                break;
            }
            AppLogger::instance().info("SpectrometerServer: response sent");
        }
    }
}

int main() {
    AppLogger::instance().setPaths("scan_log_64.txt", "error_log_64.txt");
    AppLogger::instance().info("Starting SpectrometerServer (64-bit)");

    while (true) {
        HANDLE hPipe = CreateNamedPipeA(
            ANDOR_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            sizeof(IpcMessage) * 4,
            sizeof(IpcMessage) * 4,
            0,
            NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            AppLogger::instance().error("SpectrometerServer: CreateNamedPipe failed");
            return 1;
        }

        AppLogger::instance().info(std::string("SpectrometerServer: listening on pipe ") + ANDOR_PIPE_NAME + "...");
        bool connected = ConnectNamedPipe(hPipe, NULL) ? true : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected) {
            AppLogger::instance().info("SpectrometerServer: client connected");
            ProcessClient(hPipe);
        } else {
            CloseHandle(hPipe);
        }

        AppLogger::instance().info("SpectrometerServer: client disconnected, restarting loop...");
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    return 0;
}