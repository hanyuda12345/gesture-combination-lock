#ifndef IMU_LSM6DSL_H
#define IMU_LSM6DSL_H

#include "mbed.h"

struct ImuSample {
    float ax_g;
    float ay_g;
    float az_g;
    float gx_dps;
    float gy_dps;
    float gz_dps;
};

class Lsm6dsl {
public:
    Lsm6dsl(PinName sda, PinName scl);

    bool init();
    bool read(ImuSample &sample);
    uint8_t whoami() const;

private:
    bool write_reg(uint8_t reg, uint8_t value);
    bool read_reg(uint8_t reg, uint8_t &value);
    bool read_regs(uint8_t start_reg, uint8_t *buffer, int length);
    bool probe_address(uint8_t addr7);

    I2C i2c_;
    int addr8_;
    uint8_t whoami_;
};

#endif
