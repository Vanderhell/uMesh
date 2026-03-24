# uMesh - Power Management

This document describes the three power profiles available in uMesh v1.3.0.

## Modes

- `UMESH_POWER_ACTIVE`: radio always on.
- `UMESH_POWER_LIGHT`: periodic sleep/listen duty cycle.
- `UMESH_POWER_DEEP`: long sleep between telemetry wakeups.

## LIGHT mode

Default profile:
- Sleep interval: 1000 ms
- Listen window: 100 ms

Approximate average current:
- Active 100 ms @ 60 mA
- Sleep 900 ms @ 2 mA
- Average ~= 7.8 mA

## DEEP mode

Default profile:
- Active phase: 500 ms
- Deep sleep interval: 30000 ms

Approximate average current:
- Active 500 ms @ 80 mA
- Sleep 29500 ms @ 0.01 mA
- Average ~= 1.3 mA

Deep mode is intended for end nodes using gradient routing.

## Battery life estimate

Given battery capacity `C_mAh` and average current `I_mA`:

`battery_hours = C_mAh / I_mA`

Example:
- 2000 mAh battery
- 8 mA average (LIGHT mode)
- battery life ~= 250 hours

## Recommendations

- Coordinator: ACTIVE
- Router: ACTIVE
- End node (frequent telemetry): LIGHT
- End node (sparse telemetry): DEEP + gradient routing
