#include "gesture.h"
#include "imu_lsm6dsl.h"
#include "key_store.h"
#include "mbed.h"

#include <algorithm>
#include <cstdio>

using std::chrono::duration_cast;
using std::chrono::milliseconds;

namespace {
constexpr int LONG_PRESS_MS = 1100;
constexpr int DEBOUNCE_MS = 35;

enum class ButtonEvent {
    None,
    ShortPress,
    LongPress,
};

class ButtonReader {
public:
    explicit ButtonReader(PinName pin)
        : button_(pin, PullNone),
          idle_level_(0),
          stable_level_(0),
          last_raw_(0),
          pressed_(false),
          last_change_ms_(0),
          pressed_since_ms_(0)
    {
        ThisThread::sleep_for(milliseconds(80));
        idle_level_ = button_.read();
        stable_level_ = idle_level_;
        last_raw_ = idle_level_;
    }

    ButtonEvent poll(uint32_t now_ms)
    {
        const int raw = button_.read();
        if (raw != last_raw_) {
            last_raw_ = raw;
            last_change_ms_ = now_ms;
        }

        if ((now_ms - last_change_ms_) < DEBOUNCE_MS || raw == stable_level_) {
            return ButtonEvent::None;
        }

        stable_level_ = raw;
        const bool is_down = stable_level_ != idle_level_;
        if (is_down && !pressed_) {
            pressed_ = true;
            pressed_since_ms_ = now_ms;
            return ButtonEvent::None;
        }

        if (!is_down && pressed_) {
            pressed_ = false;
            const uint32_t held_ms = now_ms - pressed_since_ms_;
            return held_ms >= LONG_PRESS_MS ? ButtonEvent::LongPress : ButtonEvent::ShortPress;
        }

        return ButtonEvent::None;
    }

private:
    DigitalIn button_;
    int idle_level_;
    int stable_level_;
    int last_raw_;
    bool pressed_;
    uint32_t last_change_ms_;
    uint32_t pressed_since_ms_;
};

uint32_t millis_since_boot(Timer &timer)
{
    return static_cast<uint32_t>(duration_cast<milliseconds>(timer.elapsed_time()).count());
}

void set_leds(DigitalOut &led1, DigitalOut &led2, DigitalOut &led3, int a, int b, int c)
{
    led1 = a;
    led2 = b;
    led3 = c;
}

void blink(DigitalOut &led, int count, int on_ms = 100, int off_ms = 100)
{
    for (int i = 0; i < count; ++i) {
        led = 1;
        ThisThread::sleep_for(milliseconds(on_ms));
        led = 0;
        ThisThread::sleep_for(milliseconds(off_ms));
    }
}

void success_animation(DigitalOut &led1, DigitalOut &led2, DigitalOut &led3)
{
    for (int i = 0; i < 8; ++i) {
        set_leds(led1, led2, led3, 1, 1, i % 2);
        ThisThread::sleep_for(120ms);
    }
    set_leds(led1, led2, led3, 0, 1, 0);
}

void failure_animation(DigitalOut &led1, DigitalOut &led2, DigitalOut &led3)
{
    for (int i = 0; i < 4; ++i) {
        set_leds(led1, led2, led3, 0, 0, 1);
        ThisThread::sleep_for(180ms);
        set_leds(led1, led2, led3, 0, 0, 0);
        ThisThread::sleep_for(180ms);
    }
}

void countdown(DigitalOut &led, const char *label)
{
    printf("%s starts in 3...\n", label);
    blink(led, 1, 180, 220);
    printf("2...\n");
    blink(led, 1, 180, 220);
    printf("1...\n");
    blink(led, 1, 180, 220);
}

bool capture_sequence(Lsm6dsl &imu, DigitalOut &activity_led, DigitalOut &confirm_led,
                      GestureTemplate out[KEY_GESTURE_COUNT])
{
    for (int i = 0; i < KEY_GESTURE_COUNT; ++i) {
        GestureResult gesture = capture_gesture(imu, activity_led, i + 1);
        if (!gesture.ok) {
            return false;
        }
        out[i] = gesture.gesture;
        blink(confirm_led, i + 1, 80, 80);
        ThisThread::sleep_for(350ms);
    }
    return true;
}

bool record_key(Lsm6dsl &imu, KeyStore &store, DigitalOut &led1, DigitalOut &led2, DigitalOut &led3)
{
    printf("\n=== RECORD MODE ===\n");
    printf("Record three gestures. Keep the board in a closed fist and pause before each move.\n");
    set_leds(led1, led2, led3, 0, 0, 0);
    countdown(led3, "Recording");

    GestureTemplate gestures[KEY_GESTURE_COUNT];
    if (!capture_sequence(imu, led3, led2, gestures)) {
        printf("Record failed. Try again with larger, cleaner motions.\n");
        failure_animation(led1, led2, led3);
        return false;
    }

    if (!store.save(gestures)) {
        printf("Record failed: could not write Flash.\n");
        failure_animation(led1, led2, led3);
        return false;
    }

    printf("New key saved to MCU Flash:\n");
    for (int i = 0; i < KEY_GESTURE_COUNT; ++i) {
        printf("  %d: %s\n", i + 1, gesture_name(gestures[i]));
    }
    success_animation(led1, led2, led3);
    return true;
}

bool unlock(Lsm6dsl &imu, const StoredKey &key, DigitalOut &led1, DigitalOut &led2, DigitalOut &led3)
{
    printf("\n=== UNLOCK MODE ===\n");
    printf("Repeat the saved three-gesture sequence.\n");
    set_leds(led1, led2, led3, 0, 0, 0);
    countdown(led1, "Unlock");

    for (int i = 0; i < KEY_GESTURE_COUNT; ++i) {
        GestureResult actual = capture_gesture(imu, led1, i + 1);
        if (!actual.ok) {
            printf("Unlock failed at gesture %d: no valid gesture.\n", i + 1);
            failure_animation(led1, led2, led3);
            return false;
        }

        float score = 0.0f;
        if (!gestures_match(key.gestures[i], actual.gesture, &score)) {
            printf("Unlock failed at gesture %d. Expected %s, got %s, score %.2f.\n",
                   i + 1,
                   gesture_name(key.gestures[i]),
                   gesture_name(actual.gesture),
                   score);
            failure_animation(led1, led2, led3);
            return false;
        }

        printf("Gesture %d matched: %s, score %.2f.\n", i + 1, gesture_name(actual.gesture), score);
        blink(led2, i + 1, 80, 80);
        ThisThread::sleep_for(300ms);
    }

    printf("UNLOCK SUCCESS.\n");
    success_animation(led1, led2, led3);
    return true;
}

void print_loaded_key(const StoredKey &key, bool has_key)
{
    if (!has_key) {
        printf("No saved key found. Long-press B2 to record one.\n");
        return;
    }

    printf("Saved key loaded from MCU Flash:\n");
    for (int i = 0; i < KEY_GESTURE_COUNT; ++i) {
        printf("  %d: %s\n", i + 1, gesture_name(key.gestures[i]));
    }
}
}

int main()
{
    DigitalOut led1(LED1, 0);
    DigitalOut led2(LED2, 0);
    DigitalOut led3(LED3, 0);
    ButtonReader button(BUTTON1);
    Timer uptime;
    uptime.start();

    printf("\nGesture Combination Lock\n");
    printf("Board: ST B-L475E-IOT01A, IMU: onboard LSM6DSL\n");
    printf("Controls: short-press B2 = unlock, long-press B2 = record new key.\n");

    Lsm6dsl imu(PB_11, PB_10);
    if (!imu.init()) {
        printf("ERROR: LSM6DSL not found. Check board target and sensor power.\n");
        while (true) {
            blink(led3, 1, 80, 420);
        }
    }
    printf("LSM6DSL ready, WHO_AM_I=0x%02X.\n", imu.whoami());

    KeyStore store;
    StoredKey key;
    bool has_key = store.load(key);
    print_loaded_key(key, has_key);

    uint32_t last_heartbeat_ms = 0;
    bool heartbeat = false;

    while (true) {
        const uint32_t now_ms = millis_since_boot(uptime);
        const ButtonEvent event = button.poll(now_ms);

        if (event == ButtonEvent::LongPress) {
            has_key = record_key(imu, store, led1, led2, led3);
            if (has_key) {
                store.load(key);
            }
            last_heartbeat_ms = millis_since_boot(uptime);
        } else if (event == ButtonEvent::ShortPress) {
            if (!has_key) {
                printf("No key is recorded yet. Long-press B2 first.\n");
                failure_animation(led1, led2, led3);
            } else {
                unlock(imu, key, led1, led2, led3);
            }
            last_heartbeat_ms = millis_since_boot(uptime);
        }

        const uint32_t interval = has_key ? 900U : 250U;
        if ((now_ms - last_heartbeat_ms) >= interval) {
            heartbeat = !heartbeat;
            led1 = heartbeat;
            led2 = has_key ? 0 : heartbeat;
            led3 = 0;
            last_heartbeat_ms = now_ms;
        }

        ThisThread::sleep_for(20ms);
    }
}
