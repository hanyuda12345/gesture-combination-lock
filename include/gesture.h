#ifndef GESTURE_H
#define GESTURE_H

#include "imu_lsm6dsl.h"
#include "mbed.h"

#include <cstdint>

enum class GestureKind : uint8_t {
    None = 0,
    Accel = 1,
    Gyro = 2,
};

struct GestureTemplate {
    GestureKind kind;
    int8_t axis;
    int8_t sign;
    uint8_t reserved;
    float peak_acc_g;
    float peak_gyro_dps;
    float energy;
    uint16_t duration_ms;
    uint16_t reserved2;
};

struct GestureResult {
    bool ok;
    GestureTemplate gesture;
};

const char *gesture_name(const GestureTemplate &gesture);
GestureResult capture_gesture(Lsm6dsl &imu, DigitalOut &activity_led, int slot_number);
bool gestures_match(const GestureTemplate &expected, const GestureTemplate &actual, float *score_out = nullptr);

#endif
