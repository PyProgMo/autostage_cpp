# pytest_andor

Small Windows console harness for the Andor SDK DLL.

It loads `atmcd64d.dll`, probes camera count and handles, applies a basic spectroscopy configuration, triggers one acquisition, and saves the returned spectrum to a text file when a camera is available.

If a Shamrock spectrograph DLL is available, the script can also set the center wavelength and save a wavelength-calibrated spectrum using `ShamrockGetCalibration`.

## Run

```bash
python andor_smoke_test.py --dll ..\dlls\atmcd64d.dll --output-dir captures --camera-index 0 --exposure 0.1
```

To enable wavelength calibration, add `--shamrock-dll <path>` and optionally `--central-wavelength-nm <value>`.

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

Optional Shamrock calls when the spectrograph DLL is present:

- `ShamrockInitialize`
- `ShamrockGetNumberDevices`
- `ShamrockGetSerialNumber`
- `ShamrockSetWavelength`
- `ShamrockGetWavelength`
- `ShamrockSetNumberPixels`
- `ShamrockSetPixelWidth`
- `ShamrockGetCalibration`

## Notes

- The script is dependency-free and uses only the Python standard library.
- If no camera is connected, it still prints the SDK return codes and exits cleanly.
- The saved spectrum format is a simple CSV-style text file. When Shamrock calibration is available, the first column becomes wavelength in nm instead of pixel index.