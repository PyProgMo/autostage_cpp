# PI GCS 2 DLL Safety Guide

This document enumerates functions in the PI GCS 2 DLL that can cause hardware damage, unintended motion, power cycling, or loss of reference if called incorrectly or without appropriate safeguards. It provides rationale for why each function is considered high-risk and recommends mitigation strategies for safe usage.

## Overview

Certain PI GCS 2 DLL functions directly control stage motion, servo power, calibration states, and controller resets. Improper use of these functions can result in:
- **Uncontrolled motion**: moving stages into hard limits, collisions, or destructive paths.
- **Loss of reference**: resetting calibration without proper homing, making position data unreliable.
- **Power faults**: enabling/disabling servo amplifiers without proper sequencing, damaging piezo or stepper stages.
- **System resets**: rebooting the controller unexpectedly, losing runtime state and connections.
- **Hardware damage**: issuing commands to unreferenced or unpowered axes, or exceeding safe motion ranges.

## High-Risk Function Categories

### 1. Absolute and Relative Motion Commands

| Function | Risk | Dangerous Patterns | Mitigation |
| --- | --- | --- | --- |
| `_PI_MOV@12` | **High** | Moving an unreferenced axis; moving without servo enabled; ignoring on-target status; moving beyond soft limits. | **Before calling:** (1) verify `qSVO` returns 1 (servo enabled) for target axis; (2) verify `qONT` returns 1 (on-target) after previous moves; (3) ensure stage is homed (call `ATZ`, `FRF`, `FNL`, or `FPL` and wait for completion); (4) enforce soft limits in code. **After calling:** wait for `qONT` to return 1 before reading positions or commanding further motion. |
| `_PI_MVR@12` | **High** | Relative move from an unknown starting position; moving without servo enabled; accumulating errors via repeated relative moves without on-target checks. | Same as `MOV`: verify servo state, on-target status, and homing. Relative moves should only be issued after an absolute reference or `qONT` confirmation. |
| `_PI_MVE@12` | **Medium** | Motion execution with unclear execution semantics; controller-specific behavior may differ from `MOV`. | If used, apply same safeguards as `MOV`. Test extensively with hardware before production use. Document exact behavior for your controller model. |

### 2. Servo Control and Power Commands

| Function | Risk | Dangerous Patterns | Mitigation |
| --- | --- | --- | --- |
| `_PI_SVO@12` | **High** | Disabling servo (SVO axis 0) on a moving stage; toggling servo rapidly; enabling servo on a stage not yet referenced. | **Servo disable:** (1) first verify stage is at rest via `qONT=1`; (2) command move to a safe rest position if needed; (3) wait for completion; (4) then disable servo. **Servo enable:** (1) first home the stage (call `ATZ`, `FRF`, `FNL`, or `FPL`); (2) wait for homing completion; (3) then enable servo. Do NOT toggle servo rapidly. |
| `_PI_RON@12` | **Low/High** | If "Runtime ON/Output Enable": enabling power without prior servo/stage setup; disabling power during motion. | If this controls output amplifiers, apply similar safeguards to `SVO`: (1) check stage state before toggling; (2) do not disable during motion; (3) allow settling time after power changes. Test behavior on your controller first. |

### 3. Referencing and Homing Commands

| Function | Risk | Dangerous Patterns | Mitigation |
| --- | --- | --- | --- |
| `_PI_ATZ@16` | **Medium** | Autozero on a stage not physically mounted or with obstacles; autozero without servo enabled; ignoring completion status. | **Before calling:** (1) ensure stage is physically present and unobstructed; (2) verify servo is enabled via `qSVO=1`; (3) ensure stage has not been damaged (no error codes from `GetError`). **After calling:** (1) wait for autozero to complete (poll `qATZ` or sleep for expected duration based on controller docs); (2) verify success via `GetError` + `TranslateError`; (3) confirm position readback is reasonable (not NaN or extreme values). |
| `_PI_FRF@8`, `_PI_FNL@8`, `_PI_FPL@8` | **Medium** | Reference to limit switches on a stage with no physical stops or switches; referencing a stage already at limits (will stall or damage stage). | **Before calling:** (1) verify stage has installed limit switches or physical end-stops; (2) verify stage is NOT already pressed against a limit; (3) verify servo is enabled. **After calling:** (1) wait for homing to complete; (2) verify success via error code; (3) command a small move to confirm reference is stable. Do NOT re-reference repeatedly without clear reason. |
| `_PI_GOH@8` | **Medium** | Go-home on a stage with no home switch, or home position not properly configured. | **Before calling:** (1) verify stage type supports go-home; (2) verify home position is configured and reasonable; (3) ensure servo is enabled and stage is referenced. **After calling:** wait for completion and verify success via error code. |
| `_PI_FED@16` | **Low/High** | Edge detection for homing on a stage with no edge trigger or sensor; triggering edge detection during unrelated motion. | If this controls edge-triggered homing: (1) verify appropriate edge sensor/trigger is installed and configured; (2) do not call while other motion is in progress; (3) verify success before relying on reference. Test on your hardware first. |

### 4. Motion Halt and Emergency Stop

| Function | Risk | Dangerous Patterns | Mitigation |
| --- | --- | --- | --- |
| `_PI_HLT@8` | **Medium** | Halting motion mid-move can leave stage in unknown state; halting without re-referencing may corrupt position tracking. | **Use only for emergency stops:** (1) when safety is at risk; (2) immediately after a halt, re-check stage state via `qONT` and `qPOS`; (3) consider re-homing after a halt to restore reference. Document halt behavior for your controller. Do NOT use halt as a normal motion termination method; use `MOV` to intermediate safe positions instead. |
| `_PI_STP@4` | **High** | Equivalent to or related to `HLT`; motion interruption without proper state management. | Same as `HLT`: use only for emergency stops; verify stage state after; consider re-homing. |

### 5. Controller Resets and Reboots

| Function | Risk | Dangerous Patterns | Mitigation |
| --- | --- | --- | --- |
| `_PI_RBT@4` | **High** | Rebooting the controller during motion, data collection, or communication with host. Causes connection loss, loss of runtime state, and possible data corruption. | **Use only when:** (1) controller is unresponsive and no other recovery method works; (2) all connected stages are at safe rest positions; (3) user is physically present to monitor hardware. **Before calling:** (1) ensure no motion is in progress; (2) close any open data files or logging; (3) inform operator of imminent reboot. **After calling:** (1) allow controller to restart (~30 seconds); (2) reconnect via `ConnectUSB` or similar; (3) re-home all stages. Avoid using in automated scripts unless explicitly required. |

### 6. Waveform and Generator Commands

| Function | Risk | Dangerous Patterns | Mitigation |
| --- | --- | --- | --- |
| `_PI_WGO@16` | **Medium** | Enabling waveform output to an unreferenced or unpowered axis; overwriting motion with waveform without stopping prior motion; waveforms that exceed safe motion ranges. | **Before calling:** (1) verify axis is powered and referenced; (2) verify servo is enabled; (3) ensure waveform parameters (amplitude, offset, frequency) are within safe limits; (4) test waveform on a non-critical axis first. **After calling:** (1) monitor stage motion and error codes; (2) prepare to halt waveform if behavior is unexpected; (3) do NOT re-configure waveform while output is active. |
| `_PI_WGC@16` | **Medium** | Starting a waveform with unchecked parameters; starting multiple conflicting waveforms; not stopping waveforms after use. | Apply same safeguards as `WGO`. Always explicitly stop waveforms with an appropriate command (e.g., disabling waveform output) before starting new motion commands. |
| `_PI_WGR@4` | **Low/Medium** | If this is waveform reset, resetting while waveform is active or expected to continue. | If uncertain of behavior, avoid calling during active motion or waveform playback. Test on non-critical hardware first. |

### 7. Controller Power and I/O Commands

| Function | Risk | Dangerous Patterns | Mitigation |
| --- | --- | --- | --- |
| `_PI_DPO@8`, `_PI_DCO@12`, `_PI_DRC@16`, `_PI_DRT@20` | **Medium** | Setting digital outputs to values that trigger unintended external hardware behavior (e.g., valve actuation, relay closure, external motion); not verifying output state before setting. | **Before using:** (1) understand what external device each digital output controls; (2) document safe output values; (3) test in isolation before integration. **When calling:** (1) verify current output state via query variant (if available); (2) set outputs only to known-safe values; (3) log output changes for audit; (4) have a manual override or kill switch for external hardware. Do NOT use automated digital outputs to control safety-critical systems without fail-safe hardware interlocks. |

## Recommended Software Safeguards

### 1. Authorization Flag

Add a module-level or config-based flag to gate high-risk operations:

```python
ALLOW_HARDWARE_MOVES = False  # Set to True only when hardware is ready and supervised

def moveto(x, y, z):
    if not ALLOW_HARDWARE_MOVES:
        raise RuntimeError("Hardware moves are disabled. Set ALLOW_HARDWARE_MOVES=True to enable.")
    # ... rest of move logic
```

### 2. Pre-Motion Checks

Before every absolute or relative move, verify:
- Servo is enabled: `qSVO axis 1` should return "1" for each target axis.
- Stage is on-target: `qONT axis` should return "1" after prior motion.
- Stage is referenced: trigger homing if `qONT` indicates unknown state.
- Soft limits are within safe range.

Example:
```python
def verify_ready(axes):
    """Return True if all axes are powered and on-target."""
    for axis in axes:
        # Query servo state
        # Query on-target state
        # If not ready, return False or raise
    return True

def safe_moveto(x, y, z):
    if not ALLOW_HARDWARE_MOVES:
        raise RuntimeError("Moves disabled.")
    if not verify_ready(['1', '2', '3']):
        raise RuntimeError("Axes not ready for motion.")
    moveto(x, y, z)
    wait_on_target(['1', '2', '3'])
```

### 3. On-Target Waiting

After motion commands, always wait for on-target status before issuing new commands or reading positions:

```python
def wait_on_target(axes, timeout_sec=30):
    """Block until all axes report on-target, or timeout."""
    import time
    start = time.time()
    while time.time() - start < timeout_sec:
        # Query qONT for each axis
        # If all on-target, return True
        # Otherwise sleep and retry
        time.sleep(0.1)
    raise TimeoutError("Wait on-target timed out")
```

### 4. Homing and Reference Management

Encapsulate homing logic in a helper function:

```python
def reference_stage(axes, method='ATZ'):
    """Home specified axes using specified method (ATZ, FRF, FNL, FPL, GOH)."""
    if not ALLOW_HARDWARE_MOVES:
        raise RuntimeError("Homing disabled.")
    # Send appropriate GCS command based on method
    # Wait for completion
    # Verify success
    # Return True if successful, False otherwise
```

### 5. Halt and Emergency Stop

Document when `HLT` or `STP` are acceptable:

```python
def emergency_stop():
    """Stop all motion immediately. Requires re-homing afterward."""
    send_command("HLT")
    # Log event
    # Notify operator
    # Set flag indicating reference lost
```

### 6. Disable Dangerous Functions in Production

For production code, consider removing or strictly gating access to:
- `_PI_RBT@4` (reboot)
- `_PI_HLT@8` / `_PI_STP@4` (halt/stop) — only via explicit emergency interface
- `_PI_RON@12` (power disable) — gate behind operator confirmation
- `_PI_WGO@16`, `_PI_WGC@16` (waveforms) — test extensively first

## Recommended Testing Procedure

1. **Offline / Simulation**: Use mock controller or simulation to test command sequences without hardware.
2. **Bench Test**: With hardware present but in a safe, confined space:
   - Test homing sequences for each axis.
   - Test small reference moves to verify on-target detection.
   - Test servo enable/disable logic.
   - Verify error reporting and translation.
3. **Integration Test**: In the final deployment environment with all safety interlocks active.
4. **Regression Test**: After any code changes to motion or homing logic, re-run bench tests to ensure no behavioral changes.

## Error Handling Template

Always wrap high-risk calls with error checking:

```python
def safe_command(gcs_command, axes=None):
    """Send a GCS command and check for errors."""
    try:
        send_command(gcs_command)
    except RuntimeError as e:
        # Log error
        logger.error(f"GCS command failed: {gcs_command}\n{e}")
        
        # If motion was attempted, attempt to halt
        if 'MOV' in gcs_command or 'MVR' in gcs_command:
            try:
                send_command("HLT")
            except:
                pass
        
        # Re-raise for caller to handle
        raise

def safe_moveto(x, y, z):
    """Move to target position with full safety checks."""
    if not ALLOW_HARDWARE_MOVES:
        raise RuntimeError("Moves disabled.")
    
    try:
        verify_ready(['1', '2', '3'])
        safe_command(f"MOV 1 {x} 2 {y} 3 {z}", axes=['1', '2', '3'])
        wait_on_target(['1', '2', '3'], timeout_sec=30)
    except Exception as e:
        logger.error(f"Safe move failed: {e}")
        try:
            emergency_stop()
        except:
            pass
        raise
```

## References

- Main reference: [PI_GCS2_DLL_reference.md](./PI_GCS2_DLL_reference.md)
- Ambiguous exports: [PI_GCS2_DLL_ambiguous_analysis.md](./PI_GCS2_DLL_ambiguous_analysis.md)
- GCS command reference: See controller model documentation (E-712, E-709, etc.)

---

*Last updated: May 7, 2026. For safety concerns or controller-specific questions, contact PI support or consult your controller's user manual.*
