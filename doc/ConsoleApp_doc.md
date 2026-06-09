# Hardware Control System Command Documentation

This document provides a comprehensive reference manual for the CLI commands used to interface with the scientific stage controller and the Andor camera imaging hardware.

---

## 1. Stage Controller Commands (`stage`)

The `stage` subsystem controls high-precision nano-positioning translation stages (typically working along X, Y, and Z axes). All positional metrics are processed in nanometers (nm).

### `stage connect`
* **Description:** Establishes a hardware connection with the translation stage controller.
* **Syntax:** `stage connect`
* **Example:** `stage connect`

### `stage disconnect`
* **Description:** Safely terminates the connection with the translation stage controller.
* **Syntax:** `stage disconnect`
* **Example:** `stage disconnect`

### `stage pos`
* **Description:** Queries and prints the current absolute position of a specified axis.
* **Syntax:** `stage pos [axis]`
* **Arguments:**
    * `[axis]`: The targeted axis name or identifier (e.g., `x`, `y`, or `z`).
* **Example:** `stage pos x`

### `stage qpos`
* **Description:** Quick position query. Returns the current coordinates for all available axes simultaneously.
* **Syntax:** `stage qpos`
* **Example:** `stage qpos`

### `stage moveto`
* **Description:** Executes an absolute multi-axis movement to the specified $(X, Y, Z)$ coordinates.
* **Syntax:** `stage moveto [x] [y] [z]`
* **Arguments:**
    * `[x]`: Target absolute position along the X-axis in nanometers (nm).
    * `[y]`: Target absolute position along the Y-axis in nanometers (nm).
    * `[z]`: Target absolute position along the Z-axis in nanometers (nm).
* **Example:** `stage moveto 10000 5000 0` *(Moves to X=10µm, Y=5µm, Z=0µm)*

### `stage adda`
* **Description:** Initiates a continuous velocity-driven motion vector (Asynchronous/Continuous Movement). The stage moves continuously at the specified rates until halted.
* **Syntax:** `stage adda [vx] [vy] [vz]`
* **Arguments:**
    * `[vx]`: Velocity component along the X-axis in nm/s.
    * `[vy]`: Velocity component along the Y-axis in nm/s.
    * `[vz]`: Velocity component along the Z-axis in nm/s.
* **Example:** `stage adda 500 0 0` *(Drives X-axis continuously forward at 500 nm/s)*

### `stage halt`
* **Description:** Immediately stops all active motion vectors across all axes (emergency or manual software brake).
* **Syntax:** `stage halt`
* **Example:** `stage halt`

### `stage velocitytest`
* **Description:** Executes a calibration or validation routine to test axis response profiles under explicit velocity parameters across a designated distance.
* **Syntax:** `stage velocitytest [velocity_nm_s] [x_distance_nm]`
* **Arguments:**
    * `[velocity_nm_s]`: Target testing velocity in nanometers per second.
    * `[x_distance_nm]`: Linear travel distance along the X-axis over which to measure performance.
* **Example:** `stage velocitytest 2000 50000`

### `stage rowcorrected`
* **Description:** Performs a time-bounded trajectory scan across a set distance with built-in row/trajectory correction matrices. Optionally generates log files for positioning metrics.
* **Syntax:** `stage rowcorrected [duration_s] [x_distance_nm] [log 0|1]`
* **Arguments:**
    * `[duration_s]`: Total execution time of the trajectory profile in seconds.
    * `[x_distance_nm]`: Linear target distance to cover along the X-axis.
    * `[log 0|1]`: Boolean flag toggling data streaming/logging (`0` = Disabled, `1` = Enabled).
* **Example:** `stage rowcorrected 10 100000 1`

### `stage m`
* **Description:** Moves a solitary axis to an absolute coordinate targets (Single Axis Absolute Move).
* **Syntax:** `stage m [axis] [pos]`
* **Arguments:**
    * `[axis]`: Designated coordinate axis (e.g., `x`, `y`, `z`).
    * `[pos]`: Absolute target location in nanometers (nm).
* **Example:** `stage m z 1250`

### `stage wait`
* **Description:** Blocks the execution thread or command line interface until the specified axis completes its current motion routine and stabilizes.
* **Syntax:** `stage wait [axis]`
* **Arguments:**
    * `[axis]`: The monitored axis identifier.
* **Example:** `stage wait x`

---

## 2. Andor Camera Subsystem Commands (`andor`)

The `andor` commands wrap the specialized API functions provided by Andor scientific CCD/EMCCD/sCMOS detectors.

### `andor connect`
* **Description:** Initializes and links the execution environment with an Andor camera at a given index.
* **Syntax:** `andor connect [cameraIndex]`
* **Arguments:**
    * `[cameraIndex]`: Integer index identifying the target hardware detector.
* **Example:** `andor connect 0`

### `andor cameras`
* **Description:** Queries and lists all available Andor camera hardware systems connected to the local server or workstation.
* **Syntax:** `andor cameras`
* **Example:** `andor cameras`

### `andor selectCamera`
* **Description:** Switches the command focus to a specific Andor device without re-initializing the connections.
* **Syntax:** `andor selectCamera [cameraIndex]`
* **Arguments:**
    * `[cameraIndex]`: Target integer index of the camera to activate.
* **Example:** `andor selectCamera 1`

### `andor cooling`
* **Description:** Toggles the thermoelectric cooler (TEC) state to manage internal sensor noise.
* **Syntax:** `andor cooling on|off`
* **Options:** `on`, `off`
* **Example:** `andor cooling on`

### `andor setTemp`
* **Description:** Programs the target core temperature set-point for the detector's cooling engine.
* **Syntax:** `andor setTemp [celsius]`
* **Arguments:**
    * `[celsius]`: Target integer value in degrees Celsius.
* **Example:** `andor setTemp -70`

### `andor getTemp`
* **Description:** Queries and prints the real-time sensor temperature and current thermal locking status.
* **Syntax:** `andor getTemp`
* **Example:** `andor getTemp`

### `andor measurebg`
* **Description:** Captures a background/dark frame reference array to use for baseline dark current corrections.
* **Syntax:** `andor measurebg`
* **Example:** `andor measurebg`

### `andor measure`
* **Description:** Executes a solitary frame data acquisition cycle based on current settings.
* **Syntax:** `andor measure`
* **Example:** `andor measure`

### `andor setTint`
* **Description:** Sets the sensor integration time (integration window duration) using millisecond units.
* **Syntax:** `andor setTint [milliseconds]`
* **Arguments:**
    * `[milliseconds]`: Exposure duration limit.
* **Example:** `andor setTint 500`

### `andor setReadMode`
* **Description:** Reconfigures the pixel sensor array processing array format.
* **Syntax:** `andor setReadMode [mode]`
* **Supported Modes:**
    * `FVB`: Full Vertical Binning
    * `MultiTrack`: Multi-Track mode
    * `RandomTrack`: Random Track mode
    * `SingleTrack`: Single Track mode
    * `FullImage`: Full Image custom sub-array visualization
* **Example:** `andor setReadMode FullImage`

### `andor setAcquisitionMode`
* **Description:** Sets the physical structural profile for capturing image sequences.
* **Syntax:** `andor setAcquisitionMode [mode]`
* **Supported Modes:**
    * `Single`: Single exposure loop capture.
    * `Continuous`: Loop acquisitions endlessly (live feed mode).
    * `Kinetic`: Predefined frame bursts with exact step timing intervals.
* **Example:** `andor setAcquisitionMode Kinetic`

### `andor setExposureTime`
* **Description:** Standard method to configure detector exposure time boundaries using seconds.
* **Syntax:** `andor setExposureTime [seconds]`
* **Arguments:**
    * `[seconds]`: Core exposure window metric.
* **Example:** `andor setExposureTime 0.05`

### `andor setTriggerMode`
* **Description:** Configures how hardware or software signals initiate frame digitization events.
* **Syntax:** `andor setTriggerMode [mode]`
* **Supported Modes:**
    * `Internal`: Internal clock cycles dictate frame capture rate.
    * `External`: Starts each exposure frame on incoming TTL transitions.
    * `ExternalStart`: External TTL triggers the sequence start; internal clocks dictate sub-elements.
    * `FastExternal`: High-speed continuous external TTL trigger sync profile.
    * `Software`: Programmatic SDK command signals dictate frame execution.
* **Example:** `andor setTriggerMode External`

### `andor setImage`
* **Description:** Configures horizontal/vertical binning metrics alongside structural Region of Interest (ROI) boundaries.
* **Syntax:** `andor setImage [h] [v] [hs] [he] [vs] [ve]`
* **Arguments:**
    * `[h]`: Horizontal binning factor.
    * `[v]`: Vertical binning factor.
    * `[hs]`: Horizontal coordinate start position.
    * `[he]`: Horizontal coordinate end position.
    * `[vs]`: Vertical coordinate start position.
    * `[ve]`: Vertical coordinate end position.
* **Example:** `andor setImage 1 1 1 512 1 512`

### `andor getStatus`
* **Description:** Queries the low-level operating condition code of the physical device engine (e.g., Idle, Acquiring, Error states).
* **Syntax:** `andor getStatus`
* **Example:** `andor getStatus`

### `andor setKineticCycleTime`
* **Description:** For kinetic series captures, updates the temporal period threshold assigned between successive exposures.
* **Syntax:** `andor setKineticCycleTime [s]`
* **Arguments:**
    * `[s]`: Time buffer interval spacing tracked in seconds.
* **Example:** `andor setKineticCycleTime 0.1`

### `andor setNumberKinetics`
* **Description:** Defines the total count of sequential acquisitions gathered when running a `Kinetic` sequence loop.
* **Syntax:** `andor setNumberKinetics [num]`
* **Arguments:**
    * `[num]`: Target burst collection series quantity.
* **Example:** `andor setNumberKinetics 100`

### `andor test`
* **Description:** Performs diagnostic health checks on internal hardware buffers and connection speeds.
* **Syntax:** `andor test`
* **Example:** `andor test`

### `andor disconnect`
* **Description:** Shuts down camera control registries safely and releases SDK driver focus.
* **Syntax:** `andor disconnect`
* **Example:** `andor disconnect`

---

## 3. Global Scan Command (`scan`)

### `scan`
* **Description:** Launches the automated raster configuration or acquisition workflow sequence using the combined parameter frameworks configured across the `stage` and `andor` elements.
* **Syntax:** `scan`
* **Example:** `scan`