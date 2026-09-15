# KinetiKey Gesture Combination Lock

KinetiKey is a gesture-based combination lock for the ST B-L475E-IOT01A Discovery board. It records a three-gesture key with the board's LSM6DSL accelerometer and gyroscope, stores the key in MCU Flash, and unlocks only when the gestures are repeated in the correct order within a similarity threshold.

The application is written in C++ with Mbed OS and built through PlatformIO. It uses only the microcontroller, IMU, user button, LEDs, and serial output already available on the board.

## Features

- Records and validates a three-gesture sequence.
- Distinguishes accelerometer flicks and gyroscope rotations by axis and direction.
- Stores peak acceleration, peak angular velocity, motion energy, and duration for each gesture.
- Persists the key in the final MCU Flash sector with a magic value, schema version, and checksum.
- Debounces the user button and distinguishes short and long presses.
- Reports capture status, matching scores, and errors over a 115200-baud serial connection.
- Uses separate LED patterns for recording, confirmed gestures, successful unlocks, and failures.

## Hardware

- ST B-L475E-IOT01A Discovery board
- Onboard LSM6DSL accelerometer and gyroscope
- B2 user button
- Onboard LEDs
- USB connection with ST-Link support

No external sensors are required.

## Controls

| Action | Result |
| --- | --- |
| Short press B2 | Attempt to unlock with the saved gesture sequence |
| Long press B2 | Record and save a new three-gesture key |
| Reset button | Restart the application and reload the saved key |

## How It Works

### IMU Sampling

The LSM6DSL is connected over 400 kHz I2C. The accelerometer runs at 104 Hz with a +/-4 g range, and the gyroscope runs at 104 Hz with a +/-500 dps range. The driver probes both supported I2C addresses and verifies the sensor through its `WHO_AM_I` register.

### Gesture Capture

Before each gesture, the application averages 24 stationary samples to establish an orientation-dependent baseline. It then waits for acceleration or angular velocity to cross a start threshold and captures the motion until it becomes quiet or reaches the capture timeout.

Each gesture template records:

- gesture type: accelerometer flick or gyroscope rotation
- strongest X, Y, or Z axis
- positive or negative direction
- peak acceleration
- peak angular velocity
- accumulated motion energy
- gesture duration

### Gesture Matching

The captured gesture must first match the stored gesture's type, axis, and direction. The application then computes a weighted score from peak magnitude, motion energy, and duration errors. The gesture is accepted only when the combined score is below the configured threshold.

### Flash Persistence

The three templates are stored with Mbed OS `FlashIAP`. The saved record includes a magic value, version, gesture count, and FNV-style checksum so invalid or incompatible data can be rejected during startup.

## Repository Structure

```text
.
├── include/
│   ├── gesture.h          # Gesture templates, capture, and matching interfaces
│   ├── imu_lsm6dsl.h      # IMU sample type and LSM6DSL driver interface
│   └── key_store.h        # Flash-backed key format and storage interface
├── src/
│   ├── gesture.cpp        # Baseline calibration, capture, classification, and scoring
│   ├── imu_lsm6dsl.cpp    # I2C register access and unit conversion
│   ├── key_store.cpp      # Flash erase, program, load, and checksum validation
│   └── main.cpp           # Button handling, modes, serial output, and LED feedback
├── mbed_app.json          # Mbed OS serial configuration
└── platformio.ini         # Board, framework, upload, and monitor configuration
```

## Build and Run

### Requirements

- Visual Studio Code with the PlatformIO IDE extension, or PlatformIO Core
- ST B-L475E-IOT01A connected over USB

### Build

```bash
pio run
```

### Upload

```bash
pio run --target upload
```

### Serial Monitor

```bash
pio device monitor --baud 115200
```

## Usage

1. Connect the board and open the serial monitor.
2. Long-press B2 for at least 1.1 seconds.
3. Keep the board still during the baseline prompt, then perform one clear gesture when prompted.
4. Repeat until all three gestures are captured and saved.
5. Short-press B2 and repeat the same sequence to unlock.

Example serial output:

```text
=== RECORD MODE ===
Gesture 1 captured: FLICK_Z-
Gesture 2 captured: ROT_X+
Gesture 3 captured: FLICK_Y+
New key saved to MCU Flash

=== UNLOCK MODE ===
Gesture 1 matched: FLICK_Z-, score 0.12
Gesture 2 matched: ROT_X+, score 0.18
Gesture 3 matched: FLICK_Y+, score 0.09
UNLOCK SUCCESS
```

## Design Notes

- Capture and matching thresholds are tuned for the onboard LSM6DSL and may need adjustment for different users or boards.
- Flash storage uses the final sector reported by the target. Applications sharing that sector must coordinate their memory layout.
- The saved format is versioned so future changes can reject incompatible records safely.

## Course Context

Developed as an individual project for an NYU Embedded Systems course in Spring 2026.
