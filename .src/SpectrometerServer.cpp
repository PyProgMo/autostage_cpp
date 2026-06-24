// SpectrometerServer.cpp
#include <Windows.h>
#include <iostream>
#include <vector>
#include "AndorCamera.h"
#include "IpcStructs.h"
#include "Logger.h"

#include <sstream>

namespace {
bool readExact(HANDLE pipe, void* buffer, size_t totalBytes) {
    BYTE* ptr = static_cast<BYTE*>(buffer);
    size_t remaining = totalBytes;
    DWORD bytesRead = 0;
    while (remaining > 0) {
        if (!ReadFile(pipe, ptr, static_cast<DWORD>(remaining), &bytesRead, NULL)) {
            const DWORD gle = GetLastError();
            if (gle != ERROR_MORE_DATA || bytesRead == 0) {
                return false;
            }
        }
        if (bytesRead == 0) {
            return false;
        }
        ptr += bytesRead;
        remaining -= bytesRead;
    }
    return true;
}

bool writeExact(HANDLE pipe, const void* buffer, size_t totalBytes) {
    const BYTE* ptr = static_cast<const BYTE*>(buffer);
    size_t remaining = totalBytes;
    DWORD bytesWritten = 0;
    while (remaining > 0) {
        if (!WriteFile(pipe, ptr, static_cast<DWORD>(remaining), &bytesWritten, NULL)) {
            return false;
        }
        if (bytesWritten == 0) {
            return false;
        }
        ptr += bytesWritten;
        remaining -= bytesWritten;
    }
    return true;
}
} // namespace

void ProcessClient(HANDLE hPipe) {
    AndorCamera cam;
    bool running = true;

    AppLogger::instance().info("SpectrometerServer: client handler started");

    while (running) {
        IpcMessage req = {};
        if (!readExact(hPipe, &req, sizeof(req))) {
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
        res.errorCode = 0;

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
            case IpcCommand::getTotalNumberImagesAcquired: {
                AppLogger::instance().info("SpectrometerServer: GetTotalNumberImagesAcquired");
                int totalImages = 0;
                cam.getTotalNumberImagesAcquired(totalImages);
                res.iArgs[0] = static_cast<int>(totalImages);
                break;
            }
            case IpcCommand::AndorWaitForAcquisition:
                AppLogger::instance().info("SpectrometerServer: WaitForAcquisition");
                cam.waitForAcquisition();
                break;
            case IpcCommand::AndorGetImages16: {
                int numSpectra = req.iArgs[0];
                int pixelsPerSpectrum = req.iArgs[1];
                AppLogger::instance().info("SpectrometerServer: getAllSpectra");
            try {
                auto data = cam.getAllSpectra(numSpectra, pixelsPerSpectrum);
                res.dataSize = (int32_t)(data.size() * sizeof(int));
                res.errorCode = 0;

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
            } catch (const std::exception& e) {
                AppLogger::instance().error(std::string("SpectrometerServer: exception: ") + e.what());
                res.dataSize = 0;
                res.errorCode = -1;  // or a specific error enum value
                DWORD bytesWritten = 0;
                WriteFile(hPipe, &res, sizeof(res), &bytesWritten, NULL);
            }
            continue;
            }
            case IpcCommand::AndorGetMetadata: {
                AppLogger::instance().info("SpectrometerServer: getMetadata");
                const SpectrumMetadata metadata = cam.getMetadata();
                const std::string payload = serializeSpectrumMetadata(metadata);
                res.dataSize = static_cast<int32_t>(payload.size());

                if (!writeExact(hPipe, &res, sizeof(res))) {
                    std::cerr << "Write metadata response header failed\n";
                    break;
                }

                if (res.dataSize > 0 && !writeExact(hPipe, payload.data(), static_cast<size_t>(res.dataSize))) {
                    std::cerr << "Write metadata response payload failed\n";
                    break;
                }
            continue;
            }
            case IpcCommand::AndorSetMetadata: {
                AppLogger::instance().info("SpectrometerServer: setMetadata");
                if (req.dataSize < 0) {
                    throw std::runtime_error("SpectrometerServer: invalid metadata payload size");
                }

                std::string payload;
                payload.resize(static_cast<size_t>(req.dataSize));
                if (!payload.empty() && !readExact(hPipe, &payload[0], payload.size())) {
                    throw std::runtime_error("SpectrometerServer: failed to read metadata payload");
                }

                SpectrumMetadata metadata = cam.getMetadata();
                deserializeSpectrumMetadata(payload, metadata);
                cam.setMetadata(metadata);
                
            continue;
            }
            case IpcCommand::AcquireAndFetchSingle: {
                AppLogger::instance().info("SpectrometerServer: AcquireAndFetchSingle");
                std::vector<int> data;
                SpectrumMetadata meta;
                cam.AcquireAndFetchSingle(req.iArgs[0], data, meta);

                // Send metadata first as a separate message
                const std::string metaPayload = serializeSpectrumMetadata(meta);
                IpcMessage metaRes = {};
                metaRes.command = IpcCommand::AndorGetMetadata; // reuse the same command for metadata response
                metaRes.dataSize = static_cast<int32_t>(metaPayload.size());
                if (!writeExact(hPipe, &metaRes, sizeof(metaRes))) {
                    std::cerr << "Write metadata response header failed\n";
                    break;
                }
                if (metaRes.dataSize > 0 && !writeExact(hPipe, metaPayload.data(), static_cast<size_t>(metaRes.dataSize))) {
                    std::cerr << "Write metadata response payload failed\n";
                    break;
                }

                // Then send the spectrum data
                IpcMessage dataRes = {};
                dataRes.command = req.command; // same command for spectrum data response
                dataRes.dataSize = static_cast<int32_t>(data.size() * sizeof(int));
                if (!writeExact(hPipe, &dataRes, sizeof(dataRes))) {
                    std::cerr << "Write spectrum response header failed\n";
                    break;
                }
                if (dataRes.dataSize > 0 && !writeExact(hPipe, data.data(), static_cast<size_t>(dataRes.dataSize))) {
                    std::cerr << "Write spectrum response payload failed\n";
                    break;
                }
            continue;
            }
            case IpcCommand::MeasureAndSaveNSpecs:
                AppLogger::instance().info("SpectrometerServer: MeasureAndSaveNSpecs");
                // For this command, we will read the payload containing the spectra and metadata
                if (req.dataSize < 0) {
                    throw std::runtime_error("SpectrometerServer: invalid spectra payload size");
                } else {
                    AppLogger::instance().info("SpectrometerServer: MeasureAndSaveNSpecs to measure and save " + std::to_string(req.iArgs[0]) + " spectra");
                    // AndorCamera::MeasureAndSaveNSpecs will handle reading the payload and saving the spectra.
                    // args: const std::string& foldername, const std::string& filename, int nspecs
                    std::string foldername(req.strArg);
                    int nspecs = req.iArgs[0];
                    cam.measureandsaveNspecs(foldername, nspecs);
                }
                
                
                break;
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

        // After the switch block:
        if (req.command != IpcCommand::AndorGetImages16 &&
            req.command != IpcCommand::AndorGetMetadata) {
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