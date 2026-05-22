# PI GCS2 Motion and Scan Exports

This document maps the PI E-712 / E7XX GCS2 DLL exports to the motion modes used in this workspace: normal positioning, fast repositioning, high-precision closed-loop motion, constant-velocity motion, and autostage scanning.

The goal is practical: if you are extending the wrapper or writing a new driver, these are the exports that matter for the current command flow.

## Summary

| Motion mode | Required exports | Purpose |
| --- | --- | --- |
| Normal point-to-point move | `_PI_SVO@12`, `_PI_MOV@12`, `_PI_qONT@12`, `_PI_qPOS@12` | Enable closed loop, command an absolute move, wait until the controller reports on-target, then read back position. |
| Fast repositioning | `_PI_SVO@12`, `_PI_MOV@12`, `_PI_qONT@12` | Same as normal motion, but with a higher velocity configured before the move. |
| High-precision closed-loop move | `_PI_SVO@12`, `_PI_MOV@12`, `_PI_qONT@12`, `_PI_qPOS@12`, `_PI_GetError@4`, `_PI_TranslateError@12` | Same closed-loop move path, with stricter wait logic and error reporting. |
| Constant-velocity line move | `_PI_SVO@12`, `_PI_VEL@12`, `_PI_MOV@12`, `_PI_qONT@12`, `_PI_qPOS@12` | Set the feed rate, start the move, and poll until on-target. This is the core pattern used for scan rows. |
| Autostage / raster scan | `_PI_SVO@12`, `_PI_VEL@12`, `_PI_MOV@12`, `_PI_qONT@12`, `_PI_qPOS@12`, `_PI_HLT@8` | Reposition to the next row, sweep the fast axis at constant velocity, monitor position, and halt immediately if a safety condition is violated. |

## What the workspace currently uses

The working Python and C++ references in this repository use the same core pattern:

1. Enable closed loop with `_PI_SVO@12` or `SVO`.
2. Send an absolute move with `_PI_MOV@12` or `MOV`.
3. Set motion speed with `_PI_VEL@12` or `VEL` when the move is meant to run at a controlled feed rate.
4. Wait for completion using `_PI_qONT@12` or `qONT`.
5. Read back the actual position using `_PI_qPOS@12` or `qPOS`.

That is the current implementation style in [t6.py](../t6.py) and the C++ minimal qPOS wrapper under [minimaltest/qpos](../minimaltest/qpos).

## Export groups by behavior

### Normal motion

Use these exports for ordinary absolute positioning:

- `_PI_SVO@12` to enable the servo loop.
- `_PI_MOV@12` to command the target position.
- `_PI_qONT@12` to confirm the move is complete.
- `_PI_qPOS@12` to verify the final coordinates.

This is the minimum closed-loop set for a safe move.

### Fast motion

Fast motion is not a separate motion primitive in the current code. It is still `MOV`, but with a larger `VEL` setting before the move begins.

Required exports:

- `_PI_VEL@12`
- `_PI_MOV@12`
- `_PI_qONT@12`

Optional but useful:

- `_PI_qPOS@12` for readback after the move.

### High precision / closed loop

For higher precision motion, the export set is the same as normal motion, but you should also retain controller error reporting:

- `_PI_SVO@12`
- `_PI_MOV@12`
- `_PI_qONT@12`
- `_PI_qPOS@12`
- `_PI_GetError@4`
- `_PI_TranslateError@12`

If the stage must be referenced first, add one of the homing exports below.

### Referencing / homing

These exports are needed when the stage has not yet been homed or if the controller loses a trusted position state:

- `_PI_ATZ@16`
- `_PI_FRF@8`
- `_PI_FNL@8`
- `_PI_FPL@8`
- `_PI_GOH@8`

The correct method depends on the stage mechanics and controller configuration.

### Constant-velocity scan motion

The current scan implementation does not rely on waveform generation. It uses a simple closed-loop feed pattern:

1. Set the axis velocity with `_PI_VEL@12`.
2. Start the line move with `_PI_MOV@12`.
3. Poll `_PI_qONT@12` until the controller reports on-target.
4. Use `_PI_qPOS@12` for confirmation or logging.

This is the practical export set for scan rows in the current workspace.

### Autostage / raster scan

For autostage or raster scanning, the current workspace uses the following behavior:

- `MOV` to reposition to the next row or column start.
- `VEL` to establish the feed rate for the fast axis.
- `MOV` again for the actual constant-velocity line move.
- `qONT` to wait for completion.
- `qPOS` to record the motion result.
- `HLT` for emergency stop or boundary enforcement.

That means the exports that must be present for the current style of autostage are:

- `_PI_SVO@12`
- `_PI_VEL@12`
- `_PI_MOV@12`
- `_PI_qONT@12`
- `_PI_qPOS@12`
- `_PI_HLT@8`

## What is not required for the current implementation

These exports may exist in the DLL, but they are not required by the current working motion path in this repository:

- `_PI_ACC@12`
- `_PI_DEC@12`
- `_PI_VMA@16`
- `_PI_VMI@16`
- `_PI_SMO@12`
- `_PI_WGO@16`
- `_PI_WGC@16`
- `_PI_WGR@4`

Those functions may still be useful for a future controller-specific optimization, but the current scan implementation does not depend on them.

## Safety note

`_PI_HLT@8` is an emergency-stop export. It should be reserved for boundary violations, fault handling, or operator stop conditions.

After a halt, re-check controller state before resuming motion. Depending on the controller and stage, you may need to re-home or re-reference the axes before the next move.

For a fuller safety checklist, see [PI_GCS2_DLL_safety.md](PI_GCS2_DLL_safety.md).

## Practical recommendation

If you are building a minimal driver for the same motion style as this workspace, the core export set to implement first is:

- `_PI_ConnectUSB@4`
- `_PI_GcsCommandset@8`
- `_PI_qPOS@12`
- `_PI_SVO@12`
- `_PI_MOV@12`
- `_PI_VEL@12`
- `_PI_qONT@12`
- `_PI_HLT@8`
- `_PI_GetError@4`
- `_PI_TranslateError@12`

Add the homing exports next if your stage needs an explicit reference sequence.