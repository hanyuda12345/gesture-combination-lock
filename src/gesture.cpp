#include "gesture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using std::chrono::duration_cast;
using std::chrono::milliseconds;

namespace {
constexpr int SAMPLE_PERIOD_MS = 10;
constexpr int BASELINE_SAMPLES = 24;
constexpr int MAX_CAPTURE_MS = 1300;
constexpr int QUIET_TO_END_MS = 260;
constexpr int ARM_TIMEOUT_MS = 5000;

constexpr float START_ACCEL_G = 0.55f;
constexpr float START_GYRO_DPS = 120.0f;
constexpr float QUIET_ACCEL_G = 0.22f;
constexpr float QUIET_GYRO_DPS = 55.0f;
constexpr float MIN_ACCEL_PEAK_G = 0.65f;
constexpr float MIN_GYRO_PEAK_DPS = 145.0f;

float abs3_max(float x, float y, float z)
{
    return std::max(std::fabs(x), std::max(std::fabs(y), std::fabs(z)));
}

int strongest_axis(const float values[3])
{
    int axis = 0;
    float best = std::fabs(values[0]);
    for (int i = 1; i < 3; ++i) {
        const float candidate = std::fabs(values[i]);
        if (candidate > best) {
            axis = i;
            best = candidate;
        }
    }
    return axis;
}

float relative_error(float a, float b, float floor_value)
{
    const float denom = std::max(std::max(std::fabs(a), std::fabs(b)), floor_value);
    return std::fabs(a - b) / denom;
}

void clear_template(GestureTemplate &gesture)
{
    gesture.kind = GestureKind::None;
    gesture.axis = -1;
    gesture.sign = 0;
    gesture.reserved = 0;
    gesture.peak_acc_g = 0.0f;
    gesture.peak_gyro_dps = 0.0f;
    gesture.energy = 0.0f;
    gesture.duration_ms = 0;
    gesture.reserved2 = 0;
}
}

const char *gesture_name(const GestureTemplate &gesture)
{
    static char name[16];
    if (gesture.kind == GestureKind::None || gesture.axis < 0 || gesture.axis > 2) {
        return "NONE";
    }

    const char axes[] = {'X', 'Y', 'Z'};
    const char *kind = gesture.kind == GestureKind::Gyro ? "ROT" : "FLICK";
    std::snprintf(name, sizeof(name), "%s_%c%c", kind, axes[gesture.axis], gesture.sign >= 0 ? '+' : '-');
    return name;
}

GestureResult capture_gesture(Lsm6dsl &imu, DigitalOut &activity_led, int slot_number)
{
    GestureResult result;
    result.ok = false;
    clear_template(result.gesture);

    printf("Gesture %d: hold still...\n", slot_number);

    float base_ax = 0.0f;
    float base_ay = 0.0f;
    float base_az = 0.0f;
    float base_gx = 0.0f;
    float base_gy = 0.0f;
    float base_gz = 0.0f;
    int collected = 0;

    while (collected < BASELINE_SAMPLES) {
        ImuSample sample;
        if (imu.read(sample)) {
            base_ax += sample.ax_g;
            base_ay += sample.ay_g;
            base_az += sample.az_g;
            base_gx += sample.gx_dps;
            base_gy += sample.gy_dps;
            base_gz += sample.gz_dps;
            collected++;
        }
        activity_led = !activity_led.read();
        ThisThread::sleep_for(milliseconds(SAMPLE_PERIOD_MS));
    }

    base_ax /= BASELINE_SAMPLES;
    base_ay /= BASELINE_SAMPLES;
    base_az /= BASELINE_SAMPLES;
    base_gx /= BASELINE_SAMPLES;
    base_gy /= BASELINE_SAMPLES;
    base_gz /= BASELINE_SAMPLES;

    printf("Gesture %d: move now.\n", slot_number);
    activity_led = 1;

    Timer timer;
    timer.start();
    bool started = false;
    int quiet_ms = 0;
    int capture_ms = 0;

    float peak_acc_axis[3] = {0.0f, 0.0f, 0.0f};
    float peak_gyro_axis[3] = {0.0f, 0.0f, 0.0f};
    float energy = 0.0f;

    while (duration_cast<milliseconds>(timer.elapsed_time()).count() < ARM_TIMEOUT_MS) {
        ImuSample sample;
        if (!imu.read(sample)) {
            ThisThread::sleep_for(milliseconds(SAMPLE_PERIOD_MS));
            continue;
        }

        const float lin_acc[3] = {
            sample.ax_g - base_ax,
            sample.ay_g - base_ay,
            sample.az_g - base_az,
        };
        const float gyro[3] = {
            sample.gx_dps - base_gx,
            sample.gy_dps - base_gy,
            sample.gz_dps - base_gz,
        };

        const float accel_peak = abs3_max(lin_acc[0], lin_acc[1], lin_acc[2]);
        const float gyro_peak = abs3_max(gyro[0], gyro[1], gyro[2]);
        const bool moving = accel_peak > START_ACCEL_G || gyro_peak > START_GYRO_DPS;

        if (!started) {
            activity_led = ((duration_cast<milliseconds>(timer.elapsed_time()).count() / 120) % 2) != 0;
            if (!moving) {
                ThisThread::sleep_for(milliseconds(SAMPLE_PERIOD_MS));
                continue;
            }
            started = true;
            timer.reset();
            printf("Gesture %d: capturing...\n", slot_number);
        }

        for (int i = 0; i < 3; ++i) {
            if (std::fabs(lin_acc[i]) > std::fabs(peak_acc_axis[i])) {
                peak_acc_axis[i] = lin_acc[i];
            }
            if (std::fabs(gyro[i]) > std::fabs(peak_gyro_axis[i])) {
                peak_gyro_axis[i] = gyro[i];
            }
        }

        energy += accel_peak + (gyro_peak / 250.0f);
        capture_ms = static_cast<int>(duration_cast<milliseconds>(timer.elapsed_time()).count());

        const bool quiet = accel_peak < QUIET_ACCEL_G && gyro_peak < QUIET_GYRO_DPS;
        quiet_ms = quiet ? quiet_ms + SAMPLE_PERIOD_MS : 0;

        if ((capture_ms > 250 && quiet_ms >= QUIET_TO_END_MS) || capture_ms >= MAX_CAPTURE_MS) {
            break;
        }

        ThisThread::sleep_for(milliseconds(SAMPLE_PERIOD_MS));
    }

    activity_led = 0;

    if (!started) {
        printf("Gesture %d timeout: no clear motion detected.\n", slot_number);
        return result;
    }

    const int accel_axis = strongest_axis(peak_acc_axis);
    const int gyro_axis = strongest_axis(peak_gyro_axis);
    const float accel_peak = std::fabs(peak_acc_axis[accel_axis]);
    const float gyro_peak = std::fabs(peak_gyro_axis[gyro_axis]);

    const float accel_score = accel_peak / 1.05f;
    const float gyro_score = gyro_peak / 260.0f;

    if (gyro_score > accel_score * 0.9f && gyro_peak >= MIN_GYRO_PEAK_DPS) {
        result.gesture.kind = GestureKind::Gyro;
        result.gesture.axis = static_cast<int8_t>(gyro_axis);
        result.gesture.sign = peak_gyro_axis[gyro_axis] >= 0.0f ? 1 : -1;
    } else if (accel_peak >= MIN_ACCEL_PEAK_G) {
        result.gesture.kind = GestureKind::Accel;
        result.gesture.axis = static_cast<int8_t>(accel_axis);
        result.gesture.sign = peak_acc_axis[accel_axis] >= 0.0f ? 1 : -1;
    } else {
        printf("Gesture %d rejected: movement was too weak.\n", slot_number);
        return result;
    }

    result.gesture.peak_acc_g = accel_peak;
    result.gesture.peak_gyro_dps = gyro_peak;
    result.gesture.energy = energy;
    result.gesture.duration_ms = static_cast<uint16_t>(std::min(capture_ms, 65535));
    result.ok = true;

    printf("Gesture %d captured: %s  acc=%.2fg gyro=%.0fdps dur=%ums\n",
           slot_number,
           gesture_name(result.gesture),
           result.gesture.peak_acc_g,
           result.gesture.peak_gyro_dps,
           result.gesture.duration_ms);
    return result;
}

bool gestures_match(const GestureTemplate &expected, const GestureTemplate &actual, float *score_out)
{
    if (expected.kind != actual.kind || expected.axis != actual.axis || expected.sign != actual.sign) {
        if (score_out) {
            *score_out = 99.0f;
        }
        return false;
    }

    const float peak_error = expected.kind == GestureKind::Gyro
                                 ? relative_error(expected.peak_gyro_dps, actual.peak_gyro_dps, 180.0f)
                                 : relative_error(expected.peak_acc_g, actual.peak_acc_g, 0.75f);
    const float energy_error = relative_error(expected.energy, actual.energy, 8.0f);
    const float duration_error = relative_error(expected.duration_ms, actual.duration_ms, 400.0f);

    const float score = 0.58f * peak_error + 0.27f * energy_error + 0.15f * duration_error;
    if (score_out) {
        *score_out = score;
    }

    return score < 0.55f;
}
