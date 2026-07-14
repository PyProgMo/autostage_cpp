# pytest_andor

Small Windows console harness for the Andor SDK DLL.

It loads `atmcd64d.dll`, probes camera count and handles, applies a basic spectroscopy configuration, triggers one acquisition, and saves the returned spectrum to a text file when a camera is available.

## Run

```bash
python andor_smoke_test.py --dll ..\dlls\atmcd64d.dll --output-dir captures --camera-index 0 --exposure 0.1
```

## What it exercises

- `Initialize`
- `GetAvailableCameras`
- `GetCameraHandle`
- `SetCurrentCamera`
- `GetDetector`
- `SetReadMode`
- `SetAcquisitionMode`
- `SetExposureTime`
- `SetTriggerMode`
- `CoolerON` / `CoolerOFF`
- `SetTemperature`
- `GetTemperature`
- `IsCoolerOn`
- `StartAcquisition`
- `WaitForAcquisition`
- `GetStatus`
- `GetTotalNumberImagesAcquired`
- `GetImages16`
- `ShutDown`

## Notes

- The script is dependency-free and uses only the Python standard library.
- If no camera is connected, it still prints the SDK return codes and exits cleanly.
- The saved spectrum format is a simple CSV-style text file that can be reused later when the C++ side is implemented.