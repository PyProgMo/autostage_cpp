# PI GCS 2 DLL Ambiguous Exports Analysis

This document provides confidence-annotated descriptions for exports in the PI GCS 2 DLL that are not fully confirmed by public sources. These entries are part of the exported interface but lack complete documentation in widely available references.

## Methodology

Confidence levels are assigned based on:
- **High**: Function behavior is inferred from export name, nearby confirmed functions, or appears in multiple public sources (e.g., Micro-Manager, partial PDF references).
- **Medium**: Function name suggests a clear purpose and is related to confirmed exports, but exact parameter semantics are not fully documented in public sources.
- **Low**: Function name is ambiguous, very rare, or not found in public sources; parameter and behavior semantics require official PI manuals to confirm.

## Ambiguous Exports by Category

### Motion and Execution Helpers

| Export | Confidence | Description | Notes |
| --- | --- | --- | --- |
| `_PI_MVE@12` | Medium | Motion variant or motion execution helper; related to `MOV` and `MVR`. | Likely handles motion with specific execution mode or feedback. Name suggests "Motion Variant Execute" or "Motion Velocity Execute". |
| `_PI_MEX@8` | Low | Motion/execution helper; behavior is controller-specific and unclear from name alone. | Ambiguous abbreviation; could relate to macro execution, motion execution, or macro expansion. Requires official manual. |
| `_PI_SVA@12` | Medium | Servo-related axis setting command; similar to `SVO` (servo enable/disable) but with extended parameters. | Likely "Servo Axis" control with additional options. See also `qSVA` (query). |
| `_PI_NLM@12` | Medium | Negative limit constraint or motion boundary helper. | Name suggests "Negative Limit"; related to motion limits and soft-limit enforcement. |
| `_PI_POS@12` | Medium | Position-related command or helper; distinct from `qPOS` (query position). | Possibly "Position Set" or a variant mode. Requires clarification. |

### Referencing and Homing Helpers

| Export | Confidence | Description | Notes |
| --- | --- | --- | --- |
| `_PI_FED@16` | Medium | Reference edge or end-stop detection helper; related to FRF, FNL, FPL homing commands. | Likely "Find End/Edge Detect" or "Find End-stop Default". Used during homing sequences. |
| `_PI_BDR@8` | Low | Build / define reference direction or stage behavior; controller-specific. | Ambiguous abbreviation. Could relate to reference setup, bidirectional homing, or BrushDC motor referencing. Requires manual. |

### Configuration and Stage Setup

| Export | Confidence | Description | Notes |
| --- | --- | --- | --- |
| `_PI_CCL@12` | Medium | Closed-loop configuration or cycle/limit helper. | Name suggests "Closed-loop Control" or "Cycle Limit". Related to feedback and stability settings. |
| `_PI_CCT@8` | Low | Controller cycle timing or calibration control; exact semantics unclear. | Ambiguous. Could be "Controller Cycle Timing", "Calibration Control", or "Clock Tuning". Requires manual. |
| `_PI_CTO@20` | Low | Controller timeout, transfer options, or timing configuration; unclear purpose. | Name suggests "Controller Timeout Options" but usage pattern unknown without manual. |
| `_PI_RPA@12` | Medium | Restore parameters or parameter assignment helper. | Likely "Restore Parameter Assignment" or similar. Used during controller initialization or configuration restore. |
| `_PI_RNP@16` | Low | Network parameter setup or runtime parameter configuration. | Ambiguous. Could relate to network setup, named parameters, or runtime parameter negotiation. |
| `_PI_RON@12` | Low | Runtime or output enable/disable helper; controller-specific behavior. | Ambiguous abbreviation. Could be "Runtime Operational", "Output Enable", or "Relay Operation". Requires manual. |

### Digital and Analog I/O Helpers

| Export | Confidence | Description | Notes |
| --- | --- | --- | --- |
| `_PI_DBR@8` | Low | Digital bus / bridge related helper; exact purpose unclear. | Ambiguous. Could relate to digital communication protocols, relay bridges, or buffer routing. |
| `_PI_DCO@12` | Low | Digital control or configuration helper for I/O operations. | Likely "Digital Control" or "Digital Configuration Outputs". Used for configuring digital I/O pins. |
| `_PI_DDL@20` | Low | Digital delay, data line, or digital logic helper. | Could be "Digital Delay Line" or "Digital Data Logic". Requires manual for parameter semantics. |
| `_PI_DEC@12` | Low | Digital encoder or decrement helper; meaning depends on context. | Ambiguous. Could relate to encoder configuration, counter decrement, or digital expansion cards. |
| `_PI_DEL@8` | Low | Digital event, delay, or electronic limit helper. | Ambiguous. Could relate to event-triggered operations or configurable delays. |
| `_PI_DFH@8` | Low | Digital function / home helper or device function handler. | Ambiguous. Could relate to homing logic routing or digital function configuration. |
| `_PI_DRC@16` | Low | Digital routing or control helper for complex I/O mappings. | Likely "Digital Routing Control". Used for advanced I/O configuration. |
| `_PI_DRT@20` | Low | Digital routing, trigger, or data routing helper. | Likely "Digital Routing Trigger". Used for event-based I/O triggering. |
| `_PI_DTC@12` | Low | Digital trigger configuration or data transfer control. | Likely "Digital Trigger Configuration". Used to set up trigger conditions. |
| `_PI_OAC@16` | Low | Analog output / analog control helper. | Likely "Analog Output" or "Analog Control". Used for analog feedback and signal configuration. |
| `_PI_OAD@16` | Low | Analog data output helper for real-time or streaming data. | Likely "Output Analog Data". Used for outputting analog waveforms or sensor readings. |
| `_PI_OCD@16` | Low | Output configuration data helper. | Likely "Output Configuration Data". Used to configure output signal properties. |
| `_PI_ODC@16` | Low | Output digital control or digital command output. | Likely "Output Digital Control". Used for commanding digital outputs. |
| `_PI_OMA@12` | Low | Output mode assignment or operational mode helper. | Likely "Output Mode Assignment". Used for selecting output operational modes. |
| `_PI_OMR@12` | Low | Output mode readback or mode register query. | Likely "Output Mode Readback". Used to query current output mode. |
| `_PI_ONL@16` | Low | Output link or output-to-input connection helper. | Likely "Output Link". Used for routing or linking output signals. |
| `_PI_OSM@16` | Low | Output smoothing or output setup/configuration helper. | Likely "Output Smoothing" or "Output Setup". Used for signal conditioning. |
| `_PI_OSMf@16` | Low | Output smoothing helper variant (float or frequency related). | Variant of `OSM` with floating-point or frequency parameters. |
| `_PI_OVL@16` | Low | Output voltage limit or output level constraint. | Likely "Output Voltage Limit". Used to enforce output signal bounds. |

### Joystick and Manual Control

| Export | Confidence | Description | Notes |
| --- | --- | --- | --- |
| `_PI_JAX@16` | Medium | Configure joystick-to-axis mapping. | "Joystick Axis" mapping. Used to bind joystick inputs to motion axes. |
| `_PI_JDT@20` | Medium | Joystick timing, deadzone, or direct-drive mapping helper. | Likely "Joystick Drive Timing" or similar. Used for configuring joystick response. |
| `_PI_JLT@24` | Medium | Joystick limit trajectory or joystick motion constraint helper. | Likely "Joystick Limit Trajectory". Used to set joystick-driven motion limits. |
| `_PI_JON@16` | Medium | Enable or configure joystick / HID control. | "Joystick ON/Enable". Used to activate joystick control modes. |

### Waveform and Generator Functions

| Export | Confidence | Description | Notes |
| --- | --- | --- | --- |
| `_PI_PGS@12` | Medium | Program / generator setup or parameter assignment for waveforms. | Likely "Program Generator Setup". Used to configure waveform generation. |
| `_PI_PLM@12` | Low | Program limit, motion profile, or program load management helper. | Ambiguous. Could relate to waveform program limits, motion profile configuration, or playback management. |
| `_PI_WAC@8` | Low | Waveform acquisition or waveform axis channel helper. | Likely "Waveform Axis Channel" or "Waveform Acquisition Control". |
| `_PI_WCL@12` | Low | Waveform cycle length or waveform channel loop helper. | Likely "Waveform Cycle Length". Used to set waveform repetition parameters. |
| `_PI_WGC@16` | Medium | Waveform generator control; general control for waveform execution. | "Waveform Generator Control". Used to start, stop, or configure waveform playback. |
| `_PI_WGO@16` | Medium | Waveform generator output or waveform output routing. | "Waveform Generator Output". Used to enable and configure waveform output to axes. |
| `_PI_WGR@4` | Low | Waveform generator reset, run, or rate helper. | Likely "Waveform Generator Reset/Run". Used to initialize or restart waveform generation. |
| `_PI_WOS@16` | Low | Waveform offset or output setup helper. | Likely "Waveform Offset Setup". Used to add offsets to waveform outputs. |
| `_PI_WPA@16` | Low | Waveform parameter assignment or point assignment. | Likely "Waveform Parameter Assignment". Used to define waveform properties. |
| `_PI_WSL@16` | Low | Waveform slew rate / slope limit helper. | Likely "Waveform Slew Limit". Used to constrain output rate of change. |
| `_PI_WTR@20` | Low | Waveform trigger routing or waveform timing routing helper. | Likely "Waveform Trigger Routing". Used to configure trigger sources for waveforms. |

### Macro and Script Control

| Export | Confidence | Description | Notes |
| --- | --- | --- | --- |
| `_PI_MAC_NSTART@12` | Medium | Start a named macro or macro execution with named/numbered control. | Likely "Macro Named Start". Used to invoke specific named macros. Related to `MAC_START@8`. |

### Stage Configuration and Helpers

| Export | Confidence | Description | Notes |
| --- | --- | --- | --- |
| `_PI_SAI@12` | Medium | Stage axis information or stage axis initialization helper. | "Stage Axis Info" or "Stage Axis Initialize". Used to query or set up stage configuration. |
| `_PI_SCN@16` | Low | Scan / channel configuration or scan control helper. | Likely "Scan Channel" or "Scan Configuration". Used for raster or pattern scan setup. |
| `_PI_STE@12` | Medium | Stage enable or step/execution helper related to staging. | Likely "Stage Enable" or "Stage Setup/Execute". Used for stage-specific initialization. |

### Variable and Sequence Helpers

| Export | Confidence | Description | Notes |
| --- | --- | --- | --- |
| `_PI_VAR@12` | Medium | Variable assignment or variable management for macro/script control. | "Variable Assignment". Used to set or manage named variables in macros. |
| `_PI_VCO@12` | Low | Velocity control options or velocity / controller option helper. | Ambiguous. Could relate to advanced velocity modes or controller-specific options. |
| `_PI_SEP@24` | Low | Separator, sequence, or parameter helper; usage pattern unclear. | Ambiguous. Could relate to command separation, sequence management, or parameter grouping. |
| `_PI_SEP_String@20` | Low | String variant of `SEP`; separator or sequence helper with string parameters. | Variant of `SEP` that accepts string data instead of numeric parameters. |

## Sources and Further Investigation

- **Micro-Manager PI GCS 2**: Provided function descriptions for motion, servo, and query functions.
- **Export Names**: Function names themselves provide strong hints about purpose (e.g., `_PI_qPOS` = query position, `_PI_SVO` = servo on/off).
- **Related Confirmed Exports**: Ambiguous exports often cluster near confirmed ones (e.g., `_PI_MVE` near `_PI_MOV` and `_PI_MVR`).
- **Controller-Specific Behavior**: Many functions exhibit controller-specific semantics and are difficult to generalize without PI documentation.

## To Obtain Authoritative Definitions

To replace Low and Medium confidence entries with definitive parameter signatures and semantics:
1. Request the official **PI GCS 2.0 DLL Reference Manual** from PI support (e.g., `PIGCS_2_0_DLL_SM151E210.pdf` or equivalent).
2. Cross-reference with your specific controller model documentation (E-712, E-709, etc.) which often lists supported commands and parameters.
3. Consider examining PI's C/C++ header files (if available in their software suite downloads) which may define function prototypes and constants.

## Use in Code

When calling ambiguous exports from Python:
- If confidence is **High** or **Medium**: document the expected parameters and behavior in code comments; test with hardware if possible.
- If confidence is **Low**: avoid use unless you have official PI documentation, or treat the call as experimental and add extensive error handling.
- Always pair calls with `_PI_GetError@4` + `_PI_TranslateError@12` to surface controller-side failures.

---

*Last updated: May 7, 2026. For questions or updates, consult the main reference [PI_GCS2_DLL_reference.md](./PI_GCS2_DLL_reference.md).*
