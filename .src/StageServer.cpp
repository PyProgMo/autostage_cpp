// StageServer.cpp
#include <Windows.h>
#include <iostream>
#include <vector>
#include "PIStage.h"
#include "IpcStructs.h"

void ProcessClient(HANDLE hPipe) {
    PIStage stage;
    bool running = true;

    while (running) {
        IpcMessage req = {};
        DWORD bytesRead = 0;
        BOOL result = ReadFile(hPipe, &req, sizeof(req), &bytesRead, NULL);

        if (!result || bytesRead == 0) {
            DWORD gle = GetLastError();
            if (gle == ERROR_BROKEN_PIPE) {
                std::cout << "Client disconnected.\n";
            } else {
                std::cerr << "Read failed, GLE=" << gle << "\n";
            }
            break;
        }

        IpcMessage res = {};
        res.command = req.command;
        res.status = 0;

        std::cout << "Processing command " << static_cast<int>(req.command) << "\n";

        try {
            switch (req.command) {
            case IpcCommand::LoadDLL:
                stage.loadDLL(req.strArg);
                break;
            case IpcCommand::Connect:
                stage.connect(req.strArg);
                try {
                    stage.enableServo("X", true);
                } catch (const std::exception& e) {
                    std::cerr << "Servo enable failed: " << e.what() << "\n";
                }
                std::cout << "Connected to stage.\n";
                break;
            case IpcCommand::Disconnect:
                stage.disconnect();
                std::cout << "Disconnected from stage.\n";
                break;
            case IpcCommand::MoveAbs:
                stage.moveAbs(req.strArg, req.dArgs[0]);
                break;
            case IpcCommand::GetPos:
                res.dArgs[0] = stage.getPos(req.strArg);
                break;
            case IpcCommand::WaitOnTarget:
                stage.waitOnTarget(req.strArg, req.iArgs[0]);
                break;
            case IpcCommand::ConfigTriggerOut:
                std::cout << "Calling configureTriggerOutput...\n";
                stage.configureTriggerOutput(req.iArgs[0], req.strArg,
                                        req.dArgs[0], req.dArgs[1],
                                        req.dArgs[2], req.iArgs[1]);
                std::cout << "Returned from configureTriggerOutput.\n";
                break;
            case IpcCommand::EnableTriggerOut:
                stage.enableTriggerOutput(req.iArgs[0], req.iArgs[1] != 0);
                break;
            case IpcCommand::WaitTriggerIn:
                stage.waitForTriggerInput(req.iArgs[0], req.iArgs[1]);
                break;
            case IpcCommand::SetWaitOnGo:
                stage.setWaitOnGo(req.strArg, req.iArgs[0]);
                break;
            case IpcCommand::SetupDataRecorder:
                stage.setupDataRecorder(req.iArgs[0], req.strArg, req.iArgs[1]);
                break;
            case IpcCommand::SetRecordTrigger:
                stage.setRecordTrigger(req.iArgs[0], req.iArgs[1], req.dArgs[0]);
                break;
            case IpcCommand::SetRecordRate:
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
                running = false;
                break;
            default:
                std::cerr << "Unknown command ID: " << static_cast<int>(req.command) << "\n";
                res.status = -1;
                break;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception executing command: " << e.what() << "\n";
            // For ConfigTriggerOut, treat parameter syntax errors as non-fatal
            if (req.command == IpcCommand::ConfigTriggerOut ||
                req.command == IpcCommand::EnableTriggerOut) {
                std::cerr << "ConfigTriggerOut failed; continuing without hardware CTO.\n";
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
                std::cerr << "Write response failed, GLE=" << GetLastError() << "\n";
                break;
            }
        }
    }
}

int main() {
    std::cout << "Starting StageServer (32-bit)\n";

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
            std::cerr << "CreateNamedPipe failed, GLE=" << GetLastError() << "\n";
            return 1;
        }

        std::cout << "Listening on pipe " << PIPE_NAME << "...\n";
        bool connected = ConnectNamedPipe(hPipe, NULL) ? true : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected) {
            std::cout << "Client connected.\n";
            ProcessClient(hPipe);
        } else {
            CloseHandle(hPipe);
        }

        std::cout << "Client disconnected, restarting loop...\n";
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    return 0;
}