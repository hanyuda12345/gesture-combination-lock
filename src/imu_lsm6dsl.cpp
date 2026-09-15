#include "imu_lsm6dsl.h"

#include <cstdint>

namespace {
constexpr uint8_t REG_WHO_AM_I = 0x0F;
constexpr uint8_t REG_CTRL1_XL = 0x10;
constexpr uint8_t REG_CTRL2_G = 0x11;
constexpr uint8_t REG_CTRL3_C = 0x12;
constexpr uint8_t REG_OUTX_L_G = 0x22;
constexpr uint8_t EXPECTED_WHO_AM_I = 0x6A;

constexpr float ACCEL_4G_SENSITIVITY_G_PER_LSB = 0.000122f;
constexpr float GYRO_500DPS_SENSITIVITY_DPS_PER_LSB = 0.0175f;

int16_t to_i16(uint8_t lo, uint8_t hi)
{
    return static_cast<int16_t>((static_cast<uint16_t>(hi) << 8) | lo);
}
}

Lsm6dsl::Lsm6dsl(PinName sda, PinName scl)
    : i2c_(sda, scl), addr8_(0), whoami_(0)
{
}

bool Lsm6dsl::init()
{
    i2c_.frequency(400000);

    if (!probe_address(0x6A) && !probe_address(0x6B)) {
        return false;
    }

    // Block data update + auto-increment register address.
    if (!write_reg(REG_CTRL3_C, 0x44)) {
        return false;
    }

    // Accelerometer: 104 Hz, +/-4g. Gyro: 104 Hz, +/-500 dps.
    if (!write_reg(REG_CTRL1_XL, 0x48)) {
        return false;
    }
    if (!write_reg(REG_CTRL2_G, 0x44)) {
        return false;
    }

    ThisThread::sleep_for(100ms);
    return true;
}

bool Lsm6dsl::read(ImuSample &sample)
{
    uint8_t raw[12] = {0};
    if (!read_regs(REG_OUTX_L_G, raw, sizeof(raw))) {
        return false;
    }

    const int16_t gx = to_i16(raw[0], raw[1]);
    const int16_t gy = to_i16(raw[2], raw[3]);
    const int16_t gz = to_i16(raw[4], raw[5]);
    const int16_t ax = to_i16(raw[6], raw[7]);
    const int16_t ay = to_i16(raw[8], raw[9]);
    const int16_t az = to_i16(raw[10], raw[11]);

    sample.ax_g = ax * ACCEL_4G_SENSITIVITY_G_PER_LSB;
    sample.ay_g = ay * ACCEL_4G_SENSITIVITY_G_PER_LSB;
    sample.az_g = az * ACCEL_4G_SENSITIVITY_G_PER_LSB;
    sample.gx_dps = gx * GYRO_500DPS_SENSITIVITY_DPS_PER_LSB;
    sample.gy_dps = gy * GYRO_500DPS_SENSITIVITY_DPS_PER_LSB;
    sample.gz_dps = gz * GYRO_500DPS_SENSITIVITY_DPS_PER_LSB;
    return true;
}

uint8_t Lsm6dsl::whoami() const
{
    return whoami_;
}

bool Lsm6dsl::probe_address(uint8_t addr7)
{
    addr8_ = addr7 << 1;
    uint8_t id = 0;
    if (!read_reg(REG_WHO_AM_I, id)) {
        return false;
    }

    whoami_ = id;
    return id == EXPECTED_WHO_AM_I;
}

bool Lsm6dsl::write_reg(uint8_t reg, uint8_t value)
{
    char data[2] = {static_cast<char>(reg), static_cast<char>(value)};
    return i2c_.write(addr8_, data, sizeof(data)) == 0;
}

bool Lsm6dsl::read_reg(uint8_t reg, uint8_t &value)
{
    char cmd = static_cast<char>(reg);
    if (i2c_.write(addr8_, &cmd, 1, true) != 0) {
        return false;
    }

    char data = 0;
    if (i2c_.read(addr8_, &data, 1) != 0) {
        return false;
    }

    value = static_cast<uint8_t>(data);
    return true;
}

bool Lsm6dsl::read_regs(uint8_t start_reg, uint8_t *buffer, int length)
{
    char cmd = static_cast<char>(start_reg);
    if (i2c_.write(addr8_, &cmd, 1, true) != 0) {
        return false;
    }

    return i2c_.read(addr8_, reinterpret_cast<char *>(buffer), length) == 0;
}
