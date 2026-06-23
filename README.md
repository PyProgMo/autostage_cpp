========================================================================
HARDWARE CONTROL SYSTEM (CLI) - PROJECT README
========================================================================

Overview
--------
This repository contains the command-line interface (CLI) software system 
designed for the high-precision coordination and automated execution of 
scientific imaging workflows. 

The software acts as a unified control hub, interfacing simultaneously with:
1. High-precision nano-positioning translation stages (X, Y, Z axes).
2. Advanced scientific Andor CCD/EMCCD/sCMOS imaging detectors.

Features & Subsystems
---------------------
The CLI is split into three core functional modules:

1. Stage Controller Subsystem (`stage`)
   - Manages high-precision nano-positioning stages with nanometer (nm) resolution.
   - Supports absolute positioning, continuous velocity-driven motion vectors, 
     velocity calibration, and real-time coordinate logging.
   - Features built-in trajectory correction matrices (`rowcorrected`) for 
     automated scans.

2. Andor Camera Subsystem (`andor`)
   - Native wrapper around the specialized Andor SDK/API functions.
   - Controls thermoelectric sensor cooling, exposure windows, and data 
     acquisition profiles (Single, Continuous, Kinetic bursts).
   - Manages sensor array read modes (FVB, MultiTrack, FullImage) alongside 
     custom Region of Interest (ROI) and binning selections.

3. Global Automation Subsystem (`scan`)
   - Provides a synchronized orchestration command (`scan`) that automates 
     multi-axis raster scanning routines, mapping positioning trajectories 
     directly against high-speed camera frame capture series.

Getting Started
---------------
Launch the console application executable to open the CLI environment. Use 
the following standard subsystem prefixes to execute hardware parameters:

- `stage [command]` -> For physical stage movement, telemetry, or calibration.
- `andor [command]` -> For detector parameters, sensor cooling, and exposure settings.
- `scan`            -> For initiating automated synchronized raster scans.

Refer to the complete 'ConsoleApp_doc.md' inside this repository for the 
full syntactical command manual, expected arguments, and usage examples.

========================================================================