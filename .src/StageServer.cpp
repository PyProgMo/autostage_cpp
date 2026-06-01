// StageServer.cpp
#include <Windows.h>
#include <iostream>
#include <vector>
#include "PIStage.h"
#include "IpcStructs.h"
#include "Logger.h"

#include <sstream>

namespace {
std::string requestSummary(const IpcMessage& req) {
    std::ostringstream oss;
    oss << ipcCommandName(req.command)
        << " str='" << req.strArg << "'"
        << " i0=" << req.iArgs[0]
        << " i1=" << req.iArgs[1]
        << " d0=" << req.dArgs[0]
        << " d1=" << req.dArgs[1]
        << " dataSize=" << req.dataSize;
    return oss.str();
}
}

void ProcessClient(HANDLE hPipe) {
    PIStage stage;
    bool running = true;
    
    std::vector<double> zProfileBuffer;

    AppLogger::instance().info("StageServer: client handler started");

    while (running) {
        IpcMessage req = {};
        DWORD bytesRead = 0;
        BOOL result = ReadFile(hPipe, &req, sizeof(req), &bytesRead, NULL);

        if (!result || bytesRead == 0) {
            DWORD gle = GetLastError();
            if (gle == ERROR_BROKEN_PIPE) {
                AppLogger::instance().info("StageServer: client disconnected");
            } else {
                AppLogger::instance().error(std::string("StageServer: read failed, GLE=") + std::to_string(gle));
            }
            break;
        }

        IpcMessage res = {};
        res.command = req.command;
        res.status = 0;

        AppLogger::instance().info(std::string("StageServer: processing command ") + requestSummary(req));

        try {
            switch (req.command) {
            case IpcCommand::LoadDLL:
                AppLogger::instance().info(std::string("StageServer: LoadDLL path='") + req.strArg + "'");
                stage.loadDLL(req.strArg);
                break;
            case IpcCommand::Connect:
                AppLogger::instance().info(std::string("StageServer: Connect serial='") + req.strArg + "'");
                stage.connect(req.strArg);
                try {
                    stage.enableServo("1", true);
                    stage.enableServo("2", true);
                    stage.enableServo("3", true);
                } catch (const std::exception& e) {
                    AppLogger::instance().error(std::string("StageServer: servo enable failed: ") + e.what());
                }
                AppLogger::instance().info("StageServer: connected to stage");
                break;
            case IpcCommand::Disconnect:
                stage.disconnect();
                AppLogger::instance().info("StageServer: disconnected from stage");
                break;
            case IpcCommand::MoveAbs:
                AppLogger::instance().info(std::string("StageServer: MoveAbs axis=") + req.strArg + " target=" + std::to_string(req.dArgs[0]));
                stage.moveAbs(req.strArg, req.dArgs[0]);
                break;
            case IpcCommand::GetPos:
                res.dArgs[0] = stage.getPos(req.strArg);
                AppLogger::instance().info(std::string("StageServer: GetPos axis=") + req.strArg + " value=" + std::to_string(res.dArgs[0]));
                break;
            case IpcCommand::QueryPosTuple: {
                // Wrap the qpos call with a one-time retry in case the controller
                // is not fully ready immediately after Connect (fixes PI error=26)
                std::array<double,3> positions = {0.0,0.0,0.0};
                bool ok = false;
                for (int attempt = 1; attempt <= 2 && !ok; ++attempt) {
                    try {
                        AppLogger::instance().info(std::string("StageServer: QueryPosTuple attempt ") + std::to_string(attempt));
                        positions = stage.qpos();
                        ok = true;
                    } catch (const std::exception& e) {
                        AppLogger::instance().error(std::string("StageServer: QueryPosTuple attempt ") + std::to_string(attempt) + " failed: " + e.what());
                        if (attempt == 1) {
                            Sleep(50); // give controller a brief moment to settle
                        } else {
                            throw; // let outer handler convert to response error
                        }
                    }
                }

                if (ok) {
                    res.dArgs[0] = positions[0];
                    res.dArgs[1] = positions[1];
                    res.dArgs[2] = positions[2];
                    AppLogger::instance().info("StageServer: QueryPosTuple completed");
                }
                break;
            }
            case IpcCommand::MoveTuple:
                AppLogger::instance().info(std::string("StageServer: MoveTuple x=") + std::to_string(req.dArgs[0]) +
                                           " y=" + std::to_string(req.dArgs[1]) +
                                           " z=" + std::to_string(req.dArgs[2]));
                stage.moveto(req.dArgs[0], req.dArgs[1], req.dArgs[2]);
                break;
            case IpcCommand::Halt:
                AppLogger::instance().info("StageServer: Halt requested");
                stage.halt();
                AppLogger::instance().info("StageServer: Halt executed");
                break;
            case IpcCommand::SetVelocityTuple:
                AppLogger::instance().info(std::string("StageServer: SetVelocityTuple vx=") + std::to_string(req.dArgs[0]) +
                                           " vy=" + std::to_string(req.dArgs[1]) +
                                           " vz=" + std::to_string(req.dArgs[2]));
                stage.adda(req.dArgs[0], req.dArgs[1], req.dArgs[2]);
                break;
            case IpcCommand::WaitOnTarget:
                AppLogger::instance().info(std::string("StageServer: WaitOnTarget axis=") + req.strArg + " timeoutMs=" + std::to_string(req.iArgs[0]));
                stage.waitOnTarget(req.strArg, req.iArgs[0]);
                break;
            case IpcCommand::ConfigTriggerOut:
                AppLogger::instance().info(std::string("StageServer: ConfigTriggerOut channel=") + std::to_string(req.iArgs[0]) +
                                           " axis=" + req.strArg +
                                           " start_um=" + std::to_string(req.dArgs[0]) +
                                           " step_um=" + std::to_string(req.dArgs[1]) +
                                           " stop_um=" + std::to_string(req.dArgs[2]) +
                                           " pulseWidthUs=" + std::to_string(req.iArgs[1]));
                stage.configureTriggerOutput(req.iArgs[0], req.strArg,
                                        req.dArgs[0], req.dArgs[1],
                                        req.dArgs[2], req.iArgs[1]);
                AppLogger::instance().info("StageServer: returned from configureTriggerOutput");
                break;
            case IpcCommand::EnableTriggerOut:
                AppLogger::instance().info(std::string("StageServer: EnableTriggerOut channel=") + std::to_string(req.iArgs[0]) +
                                           " enable=" + (req.iArgs[1] != 0 ? "true" : "false"));
                stage.enableTriggerOutput(req.iArgs[0], req.iArgs[1] != 0);
                break;
            case IpcCommand::UploadZProfile:
                AppLogger::instance().info("StageServer: UploadZProfile");
                if (req.dataSize > 0) {
                    zProfileBuffer.resize(req.dataSize / sizeof(double));
                    DWORD extraRead = 0;
                    DWORD expectedBytes = static_cast<DWORD>(req.dataSize);
                    if (!ReadFile(hPipe, zProfileBuffer.data(), expectedBytes, &extraRead, NULL) || extraRead != expectedBytes) {
                        throw std::runtime_error("Failed to read ZProfile payload");
                    }
                } else {
                    zProfileBuffer.clear();
                }
                break;
            case IpcCommand::RunVelocitySweep:
                AppLogger::instance().info("StageServer: RunVelocitySweep vNominal=" + std::to_string(req.dArgs[0]) + " xStop=" + std::to_string(req.dArgs[1]));
                // dArgs[0]=vNominal, dArgs[1]=xStop, dArgs[2]=yHold, dArgs[3]=xStart, strArg contains xStep as well maybe? No, let's pack xStep into strArg or something.
                // We'll use iArgs[0] as pointer cast, wait, we can just pass xStep via IpcMessage 
                // Let's assume parameters are packed properly. For RasterScan, we need vNominal, xStop, yHold, xStart, xStep
                // We can use iArgs, but we have 4 dArgs. We need 5. So we can put xStep into an array?
                // Or dArgs[3] = xStart, and req.strArg can carry std::to_string(xStep), it's a string anyway.
                {
                    double xStep = std::stod(req.strArg);
                    stage.runVelocitySweep(req.dArgs[0], req.dArgs[1], req.dArgs[2], zProfileBuffer, req.dArgs[3], xStep);
                }
                break;
            case IpcCommand::WaitTriggerIn:
                AppLogger::instance().info(std::string("StageServer: WaitTriggerIn channel=") + std::to_string(req.iArgs[0]) +
                                           " timeoutMs=" + std::to_string(req.iArgs[1]));
                stage.waitForTriggerInput(req.iArgs[0], req.iArgs[1]);
                break;
            case IpcCommand::SetWaitOnGo:
                AppLogger::instance().info(std::string("StageServer: SetWaitOnGo axis=") + req.strArg + " mask=" + std::to_string(req.iArgs[0]));
                stage.setWaitOnGo(req.strArg, req.iArgs[0]);
                break;
            case IpcCommand::SetupDataRecorder:
                AppLogger::instance().info(std::string("StageServer: SetupDataRecorder table=") + std::to_string(req.iArgs[0]) +
                                           " source=" + req.strArg +
                                           " option=" + std::to_string(req.iArgs[1]));
                stage.setupDataRecorder(req.iArgs[0], req.strArg, req.iArgs[1]);
                break;
            case IpcCommand::SetRecordTrigger:
                AppLogger::instance().info(std::string("StageServer: SetRecordTrigger triggerSource=") + std::to_string(req.iArgs[0]) +
                                           " axis=" + std::to_string(req.iArgs[1]) +
                                           " threshold_um=" + std::to_string(req.dArgs[0]));
                stage.setRecordTrigger(req.iArgs[0], req.iArgs[1], req.dArgs[0]);
                break;
            case IpcCommand::SetRecordRate:
                AppLogger::instance().info(std::string("StageServer: SetRecordRate cycleDiv=") + std::to_string(req.iArgs[0]));
                stage.setRecordRate(req.iArgs[0]);
                break;
            case IpcCommand::ReadRecorder: {
                int startOffset = req.iArgs[0];
                int numValues = req.iArgs[1];
                int nTables = req.iArgs[2];
                int tables[4];
                for (int i = 0; i < nTables && i < 4; ++i) {
                    tables[i] = req.iArgs[3 + i];
                }
                AppLogger::instance().info(std::string("StageServer: ReadRecorder startOffset=") + std::to_string(startOffset) +
                                           " numValues=" + std::to_string(numValues) +
                                           " nTables=" + std::to_string(nTables));
                auto data = stage.readRecorder(startOffset, numValues, tables, nTables);
                res.dataSize = (int32_t)(data.size() * sizeof(double));
                
                DWORD bytesWritten = 0;
                if (!WriteFile(hPipe, &res, sizeof(res), &bytesWritten, NULL)) {
                    std::cerr << "Write response header failed, GLE=" << GetLastError() << "\n";
                    break;
                }

                if (res.dataSize > 0) {
                    if (!WriteFile(hPipe, data.data(), res.dataSize, &bytesWritten, NULL)) {
                        std::cerr << "Write response payload failed, GLE=" << GetLastError() << "\n";
                        break;
                    }
                }
                continue; // we already wrote the response header
            }
            case IpcCommand::ExitServer:
                AppLogger::instance().info("StageServer: ExitServer received");
                running = false;
                break;
            default:
                AppLogger::instance().error(std::string("StageServer: unknown command ID: ") + std::to_string(static_cast<int>(req.command)));
                res.status = -1;
                break;
            }
        } catch (const std::exception& e) {
            AppLogger::instance().error(std::string("StageServer: exception executing command ") + ipcCommandName(req.command) + ": " + e.what());
            // For ConfigTriggerOut, treat parameter syntax errors as non-fatal
            if (req.command == IpcCommand::ConfigTriggerOut ||
                req.command == IpcCommand::EnableTriggerOut) {
                AppLogger::instance().error("StageServer: ConfigTriggerOut/EnableTriggerOut failed; continuing without hardware CTO");
                res.status = -1; // let the client fall back to software timing
                strncpy(res.strArg, e.what(), sizeof(res.strArg) - 1);
            } else {
                res.status = -1;
                strncpy(res.strArg, e.what(), sizeof(res.strArg) - 1);
            }
        }

        // Send normal response
        if (req.command != IpcCommand::ReadRecorder) {
            DWORD bytesWritten = 0;
            if (!WriteFile(hPipe, &res, sizeof(res), &bytesWritten, NULL)) {
                AppLogger::instance().error(std::string("StageServer: write response failed, GLE=") + std::to_string(GetLastError()));
                break;
            }
            AppLogger::instance().info(std::string("StageServer: response sent for ") + ipcCommandName(req.command) +
                                       " status=" + std::to_string(res.status));
        }
    }
}

int main() {
    AppLogger::instance().setPaths("scan_log.txt", "error_log.txt");
    AppLogger::instance().info("Starting StageServer (32-bit)");

    while (true) {
        HANDLE hPipe = CreateNamedPipeA(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            sizeof(IpcMessage) * 4,
            sizeof(IpcMessage) * 4,
            0,
            NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            AppLogger::instance().error(std::string("StageServer: CreateNamedPipe failed, GLE=") + std::to_string(GetLastError()));
            return 1;
        }

        AppLogger::instance().info(std::string("StageServer: listening on pipe ") + PIPE_NAME + "...");
        bool connected = ConnectNamedPipe(hPipe, NULL) ? true : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected) {
            AppLogger::instance().info("StageServer: client connected");
            ProcessClient(hPipe);
        } else {
            CloseHandle(hPipe);
        }

        AppLogger::instance().info("StageServer: client disconnected, restarting loop...");
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    return 0;
}