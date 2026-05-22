# PI GCS 2 DLL Function Reference

This file documents the exported functions listed in [`_PI_Commands.txt`](./_PI_Commands.txt). It is a best-effort reference built from the export names, the local code in this workspace, and public PI/GCS documentation.

## Sources used

- [Micro-Manager PI GCS 2](https://micro-manager.org/PI_GCS_2)
- Mirrored PI GCS 2.0 DLL reference PDF discovered from online search: `PIGCS_2_0_DLL_SM151E210.pdf`
- Local export list: [`_PI_Commands.txt`](./_PI_Commands.txt)

## Notes

- `q...` functions are query variants.
- Functions with short abbreviations are sometimes controller-specific and can be ambiguous without the official PI manual.
- The descriptions below prefer the most common PI GCS meaning and mark uncertain entries as likely / ambiguous.

## Connection and discovery

| Function | Description |
| --- | --- |
| `_PI_ConnectUSB@4` | Open a USB connection to a PI controller using the device description string. |
| `_PI_ConnectUSBWithBaudRate@8` | Open a USB connection while also specifying a baud rate for USB-serial style devices. |
| `_PI_ConnectTCPIP@8` | Open a TCP/IP connection to a PI controller. |
| `_PI_ConnectTCPIPByDescription@4` | Open a TCP/IP connection using a controller description string. |
| `_PI_ConnectRS232@8` | Open an RS-232 connection to a PI controller. |
| `_PI_ConnectRS232details@4` | Open an RS-232 connection using detailed port settings. |
| `_PI_ConnectDaisyChainDevice@8` | Connect to a device that is part of a daisy chain. |
| `_PI_CloseConnection@4` | Close an open controller connection. |
| `_PI_CloseDaisyChain@4` | Close a daisy-chain connection. |
| `_PI_IsConnected@4` | Check whether a connection is currently open. |
| `_PI_EnumerateUSB@12` | Enumerate attached PI USB devices. |
| `_PI_EnumerateTCPIPDevices@12` | Discover PI devices on the network. |
| `_PI_EnableTCPIPScan@4` | Enable or disable TCP/IP scanning. |
| `_PI_GetSupportedControllers@8` | Return controllers supported by the DLL or device family. |
| `_PI_GetSupportedFunctions@20` | Return the supported PI functions. |
| `_PI_GetSupportedParameters@36` | Return the supported parameters for the current controller. |
| `_PI_InterfaceSetupDlg@4` | Open the standard PI interface setup dialog. |

## Raw GCS command channel and error handling

| Function | Description |
| --- | --- |
| `_PI_GcsCommandset@8` | Send a raw GCS command string to the controller. |
| `_PI_GcsGetAnswer@12` | Read the textual answer for the last command. |
| `_PI_GcsGetAnswerSize@8` | Read the size of the pending answer buffer. |
| `_PI_GetAsyncBuffer@8` | Access the asynchronous answer buffer. |
| `_PI_GetAsyncBufferIndex@4` | Get the current index into the asynchronous buffer. |
| `_PI_GetError@4` | Return the last PI error code. |
| `_PI_TranslateError@12` | Convert a numeric PI error code to a human-readable string. |
| `_PI_SetErrorCheck@8` | Enable or disable automatic error checking after commands. |
| `_PI_SetTimeout@8` | Set the communication timeout. |

## Motion, positioning, and servo control

| Function | Description |
| --- | --- |
| `_PI_MOV@12` | Absolute move to a target position. |
| `_PI_MVR@12` | Relative move by an offset. |
| `_PI_MVE@12` | Motion variant related to move execution; exact behavior depends on controller. |
| `_PI_MEX@8` | Motion/execution helper; exact behavior is controller-specific. |
| `_PI_POS@12` | Position-related command or position handling helper. |
| `_PI_qPOS@12` | Query the current position of one or more axes. |
| `_PI_VEL@12` | Set velocity. |
| `_PI_ACC@12` | Set acceleration. |
| `_PI_DEC@12` | Set deceleration. |
| `_PI_VMA@16` | Set maximum velocity / motion limit parameter. |
| `_PI_VMI@16` | Set minimum velocity / motion limit parameter. |
| `_PI_SVO@12` | Enable or disable servo control for an axis. |
| `_PI_qSVO@12` | Query servo state. |
| `_PI_SVR@12` | Servo-related reset or servo regulation command; controller-specific. |
| `_PI_STP@4` | Stop motion. |
| `_PI_IsMoving@12` | Check whether an axis is currently moving. |
| `_PI_qONT@12` | Query on-target status. |
| `_PI_IsControllerReady@8` | Check whether the controller is ready for commands. |
| `_PI_qERR@8` | Query the controller error state. |

## Referencing and homing

| Function | Description |
| --- | --- |
| `_PI_ATZ@16` | Autozero / referencing command for supported axes. |
| `_PI_qATZ@12` | Query autozero / referencing state. |
| `_PI_FRF@8` | Reference to the reference switch. |
| `_PI_qFRF@12` | Query FRF referencing state. |
| `_PI_FNL@8` | Home or reference toward the negative limit switch. |
| `_PI_FPL@8` | Home or reference toward the positive limit switch. |
| `_PI_GOH@8` | Go home command. |
| `_PI_FED@16` | Reference-related command; likely tied to end-stop or edge detection. |
| `_PI_BDR@8` | Build / define reference direction or stage behavior; controller-specific. |
| `_PI_CST@12` | Configuration / stage setup command used during referencing and stage definition. |
| `_PI_qCST@16` | Query stage configuration state. |

## Configuration, stage setup, and dialogs

| Function | Description |
| --- | --- |
| `_PI_AddStage@8` | Add a stage definition to the controller or configuration. |
| `_PI_RemoveStage@8` | Remove a stage definition. |
| `_PI_DisableSingleStagesDatFiles@8` | Disable loading of single-stage database files. |
| `_PI_DisableUserStagesDatFiles@8` | Disable loading of user stage database files. |
| `_PI_OpenPiStagesEditDialog@4` | Open the PI stages editor dialog. |
| `_PI_OpenUserStagesEditDialog@4` | Open the user stage editor dialog. |
| `_PI_INI@8` | Initialize the controller or configuration. |
| `_PI_SMO@12` | Motion mode / smoothing-related command. |
| `_PI_qSMO@12` | Query smoothing or motion mode state. |
| `_PI_SPA@20` | Set parameter values for an axis. |
| `_PI_SPA_String@16` | Set a parameter using a string value. |
| `_PI_qSPA@24` | Query parameter values for an axis. |
| `_PI_qSPA_String@20` | Query a parameter and return it as a string. |
| `_PI_SSA@16` | Set stage-specific axis parameters. |
| `_PI_qSSA@16` | Query stage-specific axis parameters. |
| `_PI_SSL@12` | Set stage soft limits. |
| `_PI_qSSL@12` | Query stage soft limits. |
| `_PI_qSVA@12` | Query servo-related axis settings. |
| `_PI_SVA@12` | Servo-related axis setting command. |
| `_PI_qVMA@16` | Query maximum velocity. |
| `_PI_qVMI@16` | Query minimum velocity. |
| `_PI_qVEL@12` | Query velocity. |
| `_PI_qACC@12` | Query acceleration. |
| `_PI_qDEC@12` | Query deceleration. |
| `_PI_qPOS@12` | Query position. |

## Joystick, axis, and controller state helpers

| Function | Description |
| --- | --- |
| `_PI_JAX@16` | Configure joystick-to-axis mapping. |
| `_PI_JDT@20` | Joystick or direct drive timing / mapping helper. |
| `_PI_JLT@24` | Joystick limit or trajectory helper. |
| `_PI_JON@16` | Enable or configure joystick / HID control. |
| `_PI_IsRunningMacro@8` | Check whether a macro is currently running. |
| `_PI_IsGeneratorRunning@16` | Check whether a generator or waveform engine is active. |
| `_PI_GetAsyncBufferIndex@4` | Query asynchronous buffer position. |
| `_PI_GetAsyncBuffer@8` | Read asynchronous text buffer content. |

## Macro functions

| Function | Description |
| --- | --- |
| `_PI_MAC_BEG@8` | Begin a macro definition or macro execution block. |
| `_PI_MAC_DEF@8` | Define a macro. |
| `_PI_MAC_DEL@8` | Delete a macro. |
| `_PI_MAC_END@4` | End a macro block. |
| `_PI_MAC_START@8` | Start macro execution. |
| `_PI_MAC_NSTART@12` | Start a macro in a named or controlled way; controller-specific. |
| `_PI_MAC_qDEF@12` | Query a macro definition. |
| `_PI_MAC_qERR@12` | Query a macro error. |

## Digital, analog, and I/O helpers

| Function | Description |
| --- | --- |
| `_PI_DIO@16` | Digital I/O read/write helper. |
| `_PI_DPO@8` | Digital output command. |
| `_PI_DBR@8` | Digital bus / bridge related helper. |
| `_PI_DCO@12` | Digital control / configuration helper. |
| `_PI_DDL@20` | Digital delay or digital data line helper. |
| `_PI_DEC@12` | Digital encoder or decrement helper; controller-specific. |
| `_PI_DEL@8` | Digital event / delay helper. |
| `_PI_DFH@8` | Digital function / home helper. |
| `_PI_DRC@16` | Digital routing / control helper. |
| `_PI_DRT@20` | Digital routing / trigger helper. |
| `_PI_DTC@12` | Digital trigger configuration helper. |
| `_PI_OAC@16` | Analog output / analog control helper. |
| `_PI_OAD@16` | Analog data output helper. |
| `_PI_OCD@16` | Output configuration data helper. |
| `_PI_ODC@16` | Output digital control helper. |
| `_PI_OMA@12` | Output mode assignment helper. |
| `_PI_OMR@12` | Output mode readback helper. |
| `_PI_ONL@16` | Output link helper. |
| `_PI_OSM@16` | Output smoothing / setup helper. |
| `_PI_OSMf@16` | Output smoothing helper variant. |
| `_PI_OVL@16` | Output voltage limit helper. |
| `_PI_PGS@12` | Program / generator setup helper. |
| `_PI_PLM@12` | Program limit / motion profile helper. |
| `_PI_RTO@8` | Routing or timeout helper. |
| `_PI_RTR@8` | Routing or trigger helper. |

## Miscellaneous utility functions

| Function | Description |
| --- | --- |
| `_PI_RBT@4` | Reboot or reset the controller. |
| `_PI_REP@4` | Repeat / replay helper; controller-specific. |
| `_PI_RPA@12` | Restore / parameter assignment helper; controller-specific. |
| `_PI_RNP@16` | Controller-specific helper, likely related to network or parameter setup. |
| `_PI_RON@12` | Controller-specific helper, likely related to runtime or output enable. |
| `_PI_SAI@12` | Stage axis information helper. |
| `_PI_SCN@16` | Scan / channel configuration helper. |
| `_PI_SEP@24` | Separator / sequence / parameter helper. |
| `_PI_SEP_String@20` | String variant of `_PI_SEP`. |
| `_PI_STE@12` | Step / stage enable helper. |
| `_PI_VAR@12` | Variable assignment helper. |
| `_PI_VCO@12` | Velocity / controller option helper. |
| `_PI_WAC@8` | Waveform / acquisition helper. |
| `_PI_WAV_LIN@44` | Generate a linear waveform. |
| `_PI_WAV_PNT@24` | Generate a point-based waveform. |
| `_PI_WAV_RAMP@48` | Generate a ramp waveform. |
| `_PI_WAV_SIN_P@44` | Generate a sine waveform. |
| `_PI_WCL@12` | Waveform cycle / channel helper. |
| `_PI_WGC@16` | Waveform generator control. |
| `_PI_WGO@16` | Waveform generator output. |
| `_PI_WGR@4` | Waveform generator reset / run helper. |
| `_PI_WOS@16` | Waveform offset / setup helper. |
| `_PI_WPA@16` | Waveform parameter assignment. |
| `_PI_WSL@16` | Waveform slew / slope limit. |
| `_PI_WTR@20` | Waveform trigger routing helper. |

## Less common motion and query helpers

| Function | Description |
| --- | --- |
| `_PI_AOS@12` | Axis / analog offset helper. |
| `_PI_APG@12` | Absolute positioning / page helper. |
| `_PI_ATC@16` | Auto-tuning / controller setup helper. |
| `_PI_AVG@8` | Average / smoothing helper. |
| `_PI_CCL@12` | Closed-loop configuration helper. |
| `_PI_CCT@8` | Controller or cycle timing helper. |
| `_PI_CTO@20` | Controller timeout / transfer option helper. |
| `_PI_DFH@8` | Device / reference helper. |
| `_PI_FED@16` | Reference edge / end-stop helper. |
| `_PI_HLT@8` | Halt motion or halt task execution. |
| `_PI_IMP@12` | Impact / impulse / import-related helper; ambiguous. |
| `_PI_IMP_PulseWidth@20` | Impulse pulse-width configuration helper. |
| `_PI_IFC@12` | Interface configuration helper. |
| `_PI_IFS@16` | Interface setup helper. |
| `_PI_JAX@16` | Joystick axis mapping. |
| `_PI_JDT@20` | Joystick timing / direct-drive mapping helper. |
| `_PI_JLT@24` | Joystick limit helper. |
| `_PI_JON@16` | Joystick enable / HID control. |
| `_PI_NLM@12` | Negative limit / motion constraint helper. |
| `_PI_WGO@16` | Waveform generator output. |

## Practical notes for this workspace

- The code in `t5.py` uses `_PI_ConnectUSB@4`, `_PI_qPOS@12`, `_PI_GcsCommandset@8`, `_PI_GetError@4`, and `_PI_TranslateError@12`.
- For motion, the most important commands are `SVO`, `FRF` / `FNL` / `FPL` / `GOH`, `MOV`, `MVR`, and `qONT`.
- The error handler in `t5.py` relies on `GetError` + `TranslateError`, which is a good pattern for surfacing controller-side failures.

## Best-effort / ambiguous entries

The following exports are present in the DLL but are not fully confirmed from the public sources I could reach. They are still listed here because they are part of the exported interface:

- `_PI_MVE@12`
- `_PI_MEX@8`
- `_PI_SVA@12`
- `_PI_BDR@8`
- `_PI_CCL@12`
- `_PI_CCT@8`
- `_PI_CTO@20`
- `_PI_DBR@8`
- `_PI_DCO@12`
- `_PI_DDL@20`
- `_PI_DEC@12`
- `_PI_DEL@8`
- `_PI_DFH@8`
- `_PI_DRC@16`
- `_PI_DRT@20`
- `_PI_DTC@12`
- `_PI_FED@16`
- `_PI_IMP@12`
- `_PI_JAX@16`
- `_PI_JDT@20`
- `_PI_JLT@24`
- `_PI_JON@16`
- `_PI_MAC_NSTART@12`
- `_PI_NLM@12`
- `_PI_OAC@16`
- `_PI_OAD@16`
- `_PI_OCD@16`
- `_PI_ODC@16`
- `_PI_OMA@12`
- `_PI_OMR@12`
- `_PI_ONL@16`
- `_PI_OSM@16`
- `_PI_OSMf@16`
- `_PI_OVL@16`
- `_PI_PGS@12`
- `_PI_PLM@12`
- `_PI_RPA@12`
- `_PI_RNP@16`
- `_PI_RON@12`
- `_PI_SAI@12`
- `_PI_SCN@16`
- `_PI_SEP@24`
- `_PI_SEP_String@20`
- `_PI_STE@12`
- `_PI_VAR@12`
- `_PI_VCO@12`
- `_PI_WAC@8`
- `_PI_WCL@12`
- `_PI_WGC@16`
- `_PI_WGO@16`
- `_PI_WGR@4`
- `_PI_WOS@16`
- `_PI_WPA@16`
- `_PI_WSL@16`
- `_PI_WTR@20`

If you want, I can turn this into a stricter reference next by splitting the ambiguous entries into a separate appendix and adding the exact parameter shapes for the functions you actually call from Python.

---

## Appendix: Supplemental Documentation

### Ambiguous Exports Analysis

For confidence-annotated descriptions of ambiguous exports that are part of the DLL but lack complete public documentation, see:

**[PI_GCS2_DLL_ambiguous_analysis.md](./PI_GCS2_DLL_ambiguous_analysis.md)**

This document provides:
- Confidence levels (High/Medium/Low) for each ambiguous export
- Best-effort descriptions based on export names and related confirmed functions
- Notes on which entries require official PI manuals for definitive parameter semantics
- Sources and methodology for confidence assignment
- Guidance on safe usage of ambiguous exports in code

### Safety Guide for High-Risk Functions

For recommendations on safe usage of functions that can cause hardware damage, unintended motion, or loss of reference, see:

**[PI_GCS2_DLL_safety.md](./PI_GCS2_DLL_safety.md)**

This document provides:
- Categorized list of high-risk functions (`MOV`, `RBT`, `SVO`, `HLT`, homing commands, etc.)
- Specific dangerous usage patterns and why they are dangerous
- Pre-motion checks and safety gates (servo verification, on-target waiting, homing verification)
- Recommended software safeguards (authorization flags, pre-flight checks, post-motion waits)
- Error handling templates and testing procedures
- Reference to PI documentation for further details

---

*Last updated: May 7, 2026.*