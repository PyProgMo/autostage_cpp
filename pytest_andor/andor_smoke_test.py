from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
import os
import sys


DRV_SUCCESS = 20002

READ_MODES = {
    "FVB": 0,
    "MultiTrack": 1,
    "RandomTrack": 2,
    "SingleTrack": 3,
    "FullImage": 4,
}

TRIGGER_MODES = {
    "Internal": 0,
    "External": 1,
    "ExternalStart": 6,
    "FastExternal": 7,
    "Software": 10,
}

ACQUISITION_MODES = {
    "Single": 1,
    "Continuous": 2,
    "Kinetic": 3,
}


def status_name(code: int) -> str:
    names = {
        20002: "DRV_SUCCESS",
        20024: "DRV_NO_NEW_DATA",
        20065: "DRV_DRIVER_ERRORS",
        20072: "DRV_ACQUIRING",
        20073: "DRV_IDLE",
        20075: "DRV_NOT_INITIALIZED",
        20089: "DRV_USBERROR",
        20090: "DRV_IOCERROR",
        20091: "DRV_VRMVERSIONERROR",
        20095: "DRV_INVALID_TRIGGER_MODE",
        20099: "DRV_BINNING_ERROR",
        20100: "DRV_INVALID_AMPLIFIER",
        20115: "DRV_ERROR_MAP",
        20121: "DRV_ERROR_NOHANDLE",
        20990: "DRV_ERROR_NOCAMERA",
        20991: "DRV_NOT_SUPPORTED",
        20992: "DRV_NOT_AVAILABLE",
    }
    return names.get(code, f"UNKNOWN_{code}")


def add_dll_directory_if_needed(dll_path: Path) -> None:
    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(dll_path.parent))


@dataclass
class CaptureConfig:
    dll_path: Path
    shamrock_dll_path: Path | None = None
    ini_dir: str = "."
    output_dir: Path = Path("captures")
    camera_index: int = 1
    spectrograph_index: int = 0
    read_mode: str = "FVB"
    trigger_mode: str = "Internal"
    acquisition_mode: str = "Single"
    exposure_s: float = 0.1
    central_wavelength_nm: float | None = None
    pixel_width_um: float = 13.5
    cooling: bool = False
    temperature_c: int | None = None
    continue_on_error: bool = False
    list_spectrographs: bool = False


class AndorSDK:
    def __init__(self, dll_path: Path) -> None:
        self.dll_path = dll_path
        self.lib = ctypes.WinDLL(str(dll_path), use_last_error=True)
        self._bind()

    def _bind(self) -> None:
        self.lib.Initialize.argtypes = [ctypes.c_char_p]
        self.lib.Initialize.restype = ctypes.c_uint

        self.lib.GetAvailableCameras.argtypes = [ctypes.POINTER(ctypes.c_long)]
        self.lib.GetAvailableCameras.restype = ctypes.c_uint

        self.lib.GetCameraHandle.argtypes = [ctypes.c_long, ctypes.POINTER(ctypes.c_long)]
        self.lib.GetCameraHandle.restype = ctypes.c_uint

        self.lib.SetCurrentCamera.argtypes = [ctypes.c_long]
        self.lib.SetCurrentCamera.restype = ctypes.c_uint

        self.lib.GetDetector.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        self.lib.GetDetector.restype = ctypes.c_uint

        self.lib.SetReadMode.argtypes = [ctypes.c_int]
        self.lib.SetReadMode.restype = ctypes.c_uint

        self.lib.SetAcquisitionMode.argtypes = [ctypes.c_int]
        self.lib.SetAcquisitionMode.restype = ctypes.c_uint

        self.lib.SetExposureTime.argtypes = [ctypes.c_float]
        self.lib.SetExposureTime.restype = ctypes.c_uint

        self.lib.SetTriggerMode.argtypes = [ctypes.c_int]
        self.lib.SetTriggerMode.restype = ctypes.c_uint

        self.lib.CoolerON.argtypes = []
        self.lib.CoolerON.restype = ctypes.c_uint
        self.lib.CoolerOFF.argtypes = []
        self.lib.CoolerOFF.restype = ctypes.c_uint

        self.lib.SetTemperature.argtypes = [ctypes.c_int]
        self.lib.SetTemperature.restype = ctypes.c_uint

        self.lib.GetTemperature.argtypes = [ctypes.POINTER(ctypes.c_int)]
        self.lib.GetTemperature.restype = ctypes.c_uint

        self.lib.IsCoolerOn.argtypes = [ctypes.POINTER(ctypes.c_int)]
        self.lib.IsCoolerOn.restype = ctypes.c_uint

        self.lib.SetImage.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.SetImage.restype = ctypes.c_uint

        self.lib.StartAcquisition.argtypes = []
        self.lib.StartAcquisition.restype = ctypes.c_uint

        self.lib.WaitForAcquisition.argtypes = []
        self.lib.WaitForAcquisition.restype = ctypes.c_uint

        self.lib.AbortAcquisition.argtypes = []
        self.lib.AbortAcquisition.restype = ctypes.c_uint

        self.lib.GetStatus.argtypes = [ctypes.POINTER(ctypes.c_int)]
        self.lib.GetStatus.restype = ctypes.c_uint

        self.lib.GetTotalNumberImagesAcquired.argtypes = [ctypes.POINTER(ctypes.c_long)]
        self.lib.GetTotalNumberImagesAcquired.restype = ctypes.c_uint

        self.lib.GetImages16.argtypes = [
            ctypes.c_long,
            ctypes.c_long,
            ctypes.POINTER(ctypes.c_ushort),
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_long),
            ctypes.POINTER(ctypes.c_long),
        ]
        self.lib.GetImages16.restype = ctypes.c_uint

        self.lib.ShutDown.argtypes = []
        self.lib.ShutDown.restype = ctypes.c_uint

    def _call(self, name: str, *args) -> int:
        fn = getattr(self.lib, name)
        result = int(fn(*args))
        print(f"{name}: {result} ({status_name(result)})")
        return result

    def initialize(self, ini_dir: str) -> int:
        return self._call("Initialize", ini_dir.encode("utf-8"))

    def get_available_cameras(self) -> int:
        total = ctypes.c_long(0)
        result = self._call("GetAvailableCameras", ctypes.byref(total))
        if result == DRV_SUCCESS:
            print(f"Available cameras: {total.value}")
        return total.value

    def select_camera(self, camera_index: int) -> None:
        handle = ctypes.c_long(0)
        result = self._call("GetCameraHandle", ctypes.c_long(camera_index), ctypes.byref(handle))
        if result != DRV_SUCCESS:
            raise RuntimeError(f"GetCameraHandle failed for camera {camera_index}: {status_name(result)}")
        result = self._call("SetCurrentCamera", handle)
        if result != DRV_SUCCESS:
            raise RuntimeError(f"SetCurrentCamera failed for camera {camera_index}: {status_name(result)}")
        print(f"Selected camera {camera_index} with handle {handle.value}")

    def get_detector(self) -> tuple[int, int]:
        xpix = ctypes.c_int(0)
        ypix = ctypes.c_int(0)
        result = self._call("GetDetector", ctypes.byref(xpix), ctypes.byref(ypix))
        if result != DRV_SUCCESS:
            raise RuntimeError(f"GetDetector failed: {status_name(result)}")
        return xpix.value, ypix.value

    def get_status(self) -> int:
        status = ctypes.c_int(0)
        result = self._call("GetStatus", ctypes.byref(status))
        if result != DRV_SUCCESS:
            raise RuntimeError(f"GetStatus failed: {status_name(result)}")
        print(f"Status value: {status.value} ({status_name(status.value)})")
        return status.value

    def configure(self, read_mode: str, acquisition_mode: str, exposure_s: float, trigger_mode: str, xpix: int | None = None) -> None:
        self._call("SetReadMode", ctypes.c_int(READ_MODES[read_mode]))
        self._call("SetAcquisitionMode", ctypes.c_int(ACQUISITION_MODES[acquisition_mode]))
        self._call("SetExposureTime", ctypes.c_float(exposure_s))
        self._call("SetTriggerMode", ctypes.c_int(TRIGGER_MODES[trigger_mode]))
        if xpix and xpix > 0:
            self._call("SetImage", ctypes.c_int(1), ctypes.c_int(1), ctypes.c_int(1), ctypes.c_int(xpix), ctypes.c_int(1), ctypes.c_int(1))

    def set_cooling(self, enabled: bool) -> None:
        self._call("CoolerON" if enabled else "CoolerOFF")

    def set_temperature(self, temperature_c: int) -> None:
        self._call("SetTemperature", ctypes.c_int(temperature_c))

    def get_temperature(self) -> int:
        temperature = ctypes.c_int(0)
        result = self._call("GetTemperature", ctypes.byref(temperature))
        if result != DRV_SUCCESS:
            raise RuntimeError(f"GetTemperature failed: {status_name(result)}")
        print(f"Temperature: {temperature.value} C")
        return temperature.value

    def is_cooler_on(self) -> bool:
        cooler_on = ctypes.c_int(0)
        result = self._call("IsCoolerOn", ctypes.byref(cooler_on))
        if result != DRV_SUCCESS:
            raise RuntimeError(f"IsCoolerOn failed: {status_name(result)}")
        print(f"Cooler on: {bool(cooler_on.value)}")
        return bool(cooler_on.value)

    def start_acquisition(self) -> None:
        result = self._call("StartAcquisition")
        if result != DRV_SUCCESS:
            raise RuntimeError(f"StartAcquisition failed: {status_name(result)}")

    def wait_for_acquisition(self) -> None:
        result = self._call("WaitForAcquisition")
        if result != DRV_SUCCESS:
            raise RuntimeError(f"WaitForAcquisition failed: {status_name(result)}")

    def get_total_images(self) -> int:
        total = ctypes.c_long(0)
        result = self._call("GetTotalNumberImagesAcquired", ctypes.byref(total))
        if result != DRV_SUCCESS:
            raise RuntimeError(f"GetTotalNumberImagesAcquired failed: {status_name(result)}")
        print(f"Total images acquired: {total.value}")
        return total.value

    def get_images16(self, first: int, last: int, total_pixels: int) -> list[int]:
        buffer = (ctypes.c_ushort * total_pixels)()
        valid_first = ctypes.c_long(0)
        valid_last = ctypes.c_long(0)
        result = self._call(
            "GetImages16",
            ctypes.c_long(first),
            ctypes.c_long(last),
            buffer,
            ctypes.c_ulong(total_pixels),
            ctypes.byref(valid_first),
            ctypes.byref(valid_last),
        )
        if result != DRV_SUCCESS:
            raise RuntimeError(f"GetImages16 failed: {status_name(result)}")
        print(f"Valid image range: {valid_first.value} - {valid_last.value}")
        return [int(value) for value in buffer]

    def abort_acquisition(self) -> None:
        self._call("AbortAcquisition")

    def shutdown(self) -> None:
        self._call("ShutDown")


class ShamrockSDK:
    def __init__(self, dll_path: Path) -> None:
        self.dll_path = dll_path
        self.lib = ctypes.WinDLL(str(dll_path), use_last_error=True)
        self._bind()

    def _bind(self) -> None:
        self.lib.ShamrockInitialize.argtypes = [ctypes.c_char_p]
        self.lib.ShamrockInitialize.restype = ctypes.c_uint

        self.lib.ShamrockClose.argtypes = []
        self.lib.ShamrockClose.restype = ctypes.c_uint

        self.lib.ShamrockGetNumberDevices.argtypes = [ctypes.POINTER(ctypes.c_int)]
        self.lib.ShamrockGetNumberDevices.restype = ctypes.c_uint

        self.lib.ShamrockGetSerialNumber.argtypes = [ctypes.c_int, ctypes.c_char_p]
        self.lib.ShamrockGetSerialNumber.restype = ctypes.c_uint

        self.lib.ShamrockSetWavelength.argtypes = [ctypes.c_int, ctypes.c_float]
        self.lib.ShamrockSetWavelength.restype = ctypes.c_uint

        self.lib.ShamrockGetWavelength.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_float)]
        self.lib.ShamrockGetWavelength.restype = ctypes.c_uint

        self.lib.ShamrockSetNumberPixels.argtypes = [ctypes.c_int, ctypes.c_int]
        self.lib.ShamrockSetNumberPixels.restype = ctypes.c_uint

        self.lib.ShamrockSetPixelWidth.argtypes = [ctypes.c_int, ctypes.c_float]
        self.lib.ShamrockSetPixelWidth.restype = ctypes.c_uint

        self.lib.ShamrockGetCalibration.argtypes = [
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_float),
            ctypes.c_int,
        ]
        self.lib.ShamrockGetCalibration.restype = ctypes.c_uint

    def _call(self, name: str, *args) -> int:
        fn = getattr(self.lib, name)
        result = int(fn(*args))
        print(f"{name}: {result} ({status_name(result)})")
        return result

    def initialize(self, ini_dir: str) -> int:
        return self._call("ShamrockInitialize", ini_dir.encode("utf-8"))

    def close(self) -> None:
        self._call("ShamrockClose")

    def get_number_devices(self) -> int:
        total = ctypes.c_int(0)
        result = self._call("ShamrockGetNumberDevices", ctypes.byref(total))
        if result == DRV_SUCCESS:
            print(f"Available spectrographs: {total.value}")
        return total.value

    def list_spectrographs(self) -> list[str]:
        count = self.get_number_devices()
        serials: list[str] = []
        for device_index in range(count):
            buffer = ctypes.create_string_buffer(64)
            result = self._call("ShamrockGetSerialNumber", ctypes.c_int(device_index), buffer)
            if result == DRV_SUCCESS:
                serials.append(buffer.value.decode("utf-8", errors="replace"))
        return serials

    def set_wavelength(self, device_index: int, wavelength_nm: float) -> float:
        self._call("ShamrockSetWavelength", ctypes.c_int(device_index), ctypes.c_float(wavelength_nm))
        return self.get_wavelength(device_index)

    def get_wavelength(self, device_index: int) -> float:
        wavelength = ctypes.c_float(0.0)
        result = self._call("ShamrockGetWavelength", ctypes.c_int(device_index), ctypes.byref(wavelength))
        if result != DRV_SUCCESS:
            raise RuntimeError(f"ShamrockGetWavelength failed: {status_name(result)}")
        print(f"Central wavelength: {wavelength.value:.3f} nm")
        return float(wavelength.value)

    def get_calibration(self, device_index: int, number_pixels: int, pixel_width_um: float) -> list[float]:
        self._call("ShamrockSetNumberPixels", ctypes.c_int(device_index), ctypes.c_int(number_pixels))
        self._call("ShamrockSetPixelWidth", ctypes.c_int(device_index), ctypes.c_float(pixel_width_um))
        buffer = (ctypes.c_float * number_pixels)()
        result = self._call("ShamrockGetCalibration", ctypes.c_int(device_index), buffer, ctypes.c_int(number_pixels))
        if result != DRV_SUCCESS:
            raise RuntimeError(f"ShamrockGetCalibration failed: {status_name(result)}")
        return [float(value) for value in buffer]


def resolve_dll_path(cli_path: str | None) -> Path:
    if cli_path:
        return Path(cli_path).expanduser().resolve()

    repo_root = Path(__file__).resolve().parents[1]
    candidates = [
        repo_root / "dlls" / "atmcd64d.dll",
        repo_root / "dlls" / "ATMCD64CS.dll",
        repo_root / "atmcd64d.dll",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()

    return candidates[0].resolve()


def resolve_shamrock_dll_path(cli_path: str | None) -> Path | None:
    if cli_path:
        return Path(cli_path).expanduser().resolve()

    repo_root = Path(__file__).resolve().parents[1]
    candidates = [
        repo_root / "dlls" / "ShamrockCIF64.dll",
        repo_root / "dlls" / "atspectrograph.dll",
        repo_root / "dlls" / "atshamrock64.dll",
        repo_root / "dlls" / "ShamrockCIF.dll",
        repo_root / "dlls" / "atshamrock.dll",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()

    return None


def write_spectrum(
    output_dir: Path,
    camera_index: int,
    pixels: list[int],
    exposure_s: float,
    status: int,
    wavelengths_nm: list[float] | None = None,
    central_wavelength_nm: float | None = None,
) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_file = output_dir / f"andor_spectrum_cam{camera_index}_{stamp}.txt"
    with output_file.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"camera_index={camera_index}\n")
        handle.write(f"exposure_s={exposure_s}\n")
        handle.write(f"status={status} ({status_name(status)})\n")
        if central_wavelength_nm is not None:
            handle.write(f"central_wavelength_nm={central_wavelength_nm}\n")
        if wavelengths_nm is None:
            handle.write("pixel,count\n")
            for index, count in enumerate(pixels):
                handle.write(f"{index},{count}\n")
        else:
            handle.write("wavelength_nm,count\n")
            for wavelength, count in zip(wavelengths_nm, pixels):
                handle.write(f"{wavelength:.6f},{count}\n")
    return output_file


def run_capture(cfg: CaptureConfig) -> int:
    if not cfg.dll_path.exists():
        print(f"DLL not found: {cfg.dll_path}", file=sys.stderr)
        return 1

    if hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(cfg.dll_path.parent))

    sdk = AndorSDK(cfg.dll_path)
    shamrock: ShamrockSDK | None = None

    try:
        print(f"Loading SDK from {cfg.dll_path}")
        init_result = sdk.initialize(cfg.ini_dir)
        if init_result != DRV_SUCCESS:
            return init_result

        if cfg.shamrock_dll_path is not None:
            if not cfg.shamrock_dll_path.exists():
                print(f"Shamrock DLL not found: {cfg.shamrock_dll_path}", file=sys.stderr)
            else:
                print(f"Loading Shamrock SDK from {cfg.shamrock_dll_path}")
                shamrock = ShamrockSDK(cfg.shamrock_dll_path)
                shamrock.initialize(cfg.ini_dir)
                if cfg.list_spectrographs:
                    serials = shamrock.list_spectrographs()
                    if serials:
                        for index, serial in enumerate(serials):
                            print(f"Spectrograph {index}: {serial}")
                    else:
                        print("No Shamrock spectrographs reported by the SDK.")

        camera_count = sdk.get_available_cameras()
        if camera_count <= 0:
            print("No Andor cameras reported by the SDK.")
            return 0

        max_index = min(camera_count, 8)
        for camera_index in range(max_index):
            print(f"\nCamera probe {camera_index}")
            try:
                sdk.select_camera(camera_index)
                xpix, ypix = sdk.get_detector()
                print(f"Detector size: {xpix} x {ypix}")
                status = sdk.get_status()
                print(f"Camera status: {status} ({status_name(status)})")
            except Exception as exc:
                print(f"Probe failed for camera {camera_index}: {exc}")
                if not cfg.continue_on_error:
                    break

        print("\nApplying acquisition settings to the requested camera")
        sdk.select_camera(cfg.camera_index)
        xpix, ypix = sdk.get_detector()
        print(f"Using detector {xpix} x {ypix}")

        sdk.set_cooling(cfg.cooling)
        if cfg.temperature_c is not None:
            sdk.set_temperature(cfg.temperature_c)
        try:
            sdk.is_cooler_on()
        except Exception as exc:
            print(f"Cooler state readback failed: {exc}")
        try:
            sdk.get_temperature()
        except Exception as exc:
            print(f"Temperature readback failed: {exc}")

        sdk.configure(cfg.read_mode, cfg.acquisition_mode, cfg.exposure_s, cfg.trigger_mode, xpix)
        sdk.get_status()

        wavelengths_nm: list[float] | None = None
        central_wavelength_nm = cfg.central_wavelength_nm
        if shamrock is not None:
            if central_wavelength_nm is not None:
                central_wavelength_nm = shamrock.set_wavelength(cfg.spectrograph_index, central_wavelength_nm)
            else:
                try:
                    central_wavelength_nm = shamrock.get_wavelength(cfg.spectrograph_index)
                except Exception as exc:
                    print(f"Central wavelength readback failed: {exc}")

            try:
                wavelengths_nm = shamrock.get_calibration(cfg.spectrograph_index, xpix, cfg.pixel_width_um)
                if wavelengths_nm:
                    print(
                        f"Calibration range: {wavelengths_nm[0]:.3f} nm -> {wavelengths_nm[-1]:.3f} nm"
                    )
            except Exception as exc:
                print(f"Wavelength calibration failed: {exc}")

        print("Starting acquisition test")
        sdk.start_acquisition()
        sdk.wait_for_acquisition()
        status = sdk.get_status()
        total_images = sdk.get_total_images()

        if total_images <= 0:
            print("No frames were reported by the SDK.")
            return 0

        pixels = sdk.get_images16(1, total_images, xpix)
        if pixels:
            out_file = write_spectrum(
                cfg.output_dir,
                cfg.camera_index,
                pixels[:xpix],
                cfg.exposure_s,
                status,
                wavelengths_nm=wavelengths_nm[:xpix] if wavelengths_nm else None,
                central_wavelength_nm=central_wavelength_nm,
            )
            print(f"Saved spectrum to {out_file}")

        return 0
    except Exception as exc:
        print(f"Andor smoke test failed: {exc}", file=sys.stderr)
        if cfg.continue_on_error:
            return 0
        return 1
    finally:
        if shamrock is not None:
            try:
                shamrock.close()
            except Exception:
                pass
        try:
            sdk.abort_acquisition()
        except Exception:
            pass
        try:
            sdk.shutdown()
        except Exception:
            pass


def parse_args() -> CaptureConfig:
    parser = argparse.ArgumentParser(description="Andor SDK smoke test for camera selection and single-spectrum capture")
    parser.add_argument("--dll", help="Path to atmcd64d.dll")
    parser.add_argument("--shamrock-dll", help="Path to a Shamrock spectrograph DLL")
    parser.add_argument("--ini-dir", default=".", help="Initialization directory passed to Initialize")
    parser.add_argument("--output-dir", default="captures", help="Folder for saved spectra")
    parser.add_argument("--camera-index", type=int, default=0, help="Camera index to select for the capture test")
    parser.add_argument("--spectrograph-index", type=int, default=0, help="Shamrock spectrograph index")
    parser.add_argument("--read-mode", choices=sorted(READ_MODES), default="FVB", help="Andor read mode")
    parser.add_argument("--trigger-mode", choices=sorted(TRIGGER_MODES), default="Internal", help="Andor trigger mode")
    parser.add_argument("--acquisition-mode", choices=sorted(ACQUISITION_MODES), default="Single", help="Andor acquisition mode")
    parser.add_argument("--exposure", type=float, default=1.0, help="Exposure time in seconds")
    parser.add_argument("--central-wavelength-nm", type=float, help="Set the Shamrock central wavelength before capture")
    parser.add_argument("--pixel-width-um", type=float, default=13.5, help="Camera pixel width in microns for Shamrock calibration")
    parser.add_argument("--cooling", action="store_true", help="Turn the cooler on before capture")
    parser.add_argument("--temperature", type=int, help="Target camera temperature in C")
    parser.add_argument("--continue-on-error", action="store_true", help="Keep probing other cameras after a failure")
    parser.add_argument("--list-spectrographs", action="store_true", help="Print Shamrock spectrograph serial numbers when available")
    args = parser.parse_args()

    return CaptureConfig(
        dll_path=resolve_dll_path(args.dll),
        shamrock_dll_path=resolve_shamrock_dll_path(args.shamrock_dll),
        ini_dir=args.ini_dir,
        output_dir=Path(args.output_dir),
        camera_index=args.camera_index,
        spectrograph_index=args.spectrograph_index,
        read_mode=args.read_mode,
        trigger_mode=args.trigger_mode,
        acquisition_mode=args.acquisition_mode,
        exposure_s=args.exposure,
        central_wavelength_nm=args.central_wavelength_nm,
        pixel_width_um=args.pixel_width_um,
        cooling=args.cooling,
        temperature_c=args.temperature,
        continue_on_error=args.continue_on_error,
        list_spectrographs=args.list_spectrographs,
    )


def main() -> int:
    cfg = parse_args()
    print("Andor smoke test configuration:")
    print(f"  dll_path={cfg.dll_path}")
    print(f"  ini_dir={cfg.ini_dir}")
    print(f"  output_dir={cfg.output_dir}")
    print(f"  camera_index={cfg.camera_index}")
    print(f"  spectrograph_index={cfg.spectrograph_index}")
    print(f"  read_mode={cfg.read_mode}")
    print(f"  trigger_mode={cfg.trigger_mode}")
    print(f"  acquisition_mode={cfg.acquisition_mode}")
    print(f"  exposure_s={cfg.exposure_s}")
    print(f"  central_wavelength_nm={cfg.central_wavelength_nm}")
    print(f"  pixel_width_um={cfg.pixel_width_um}")
    print(f"  cooling={cfg.cooling}")
    print(f"  temperature_c={cfg.temperature_c}")
    return run_capture(cfg)


if __name__ == "__main__":
    raise SystemExit(main())