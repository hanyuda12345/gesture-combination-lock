#ifndef KEY_STORE_H
#define KEY_STORE_H

#include "gesture.h"

#include <cstdint>

namespace mbed {
class FlashIAP;
}

constexpr int KEY_GESTURE_COUNT = 3;

struct StoredKey {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    GestureTemplate gestures[KEY_GESTURE_COUNT];
    uint32_t checksum;
};

class KeyStore {
public:
    bool load(StoredKey &key);
    bool save(const GestureTemplate gestures[KEY_GESTURE_COUNT]);
    void make_default(StoredKey &key) const;

private:
    uint32_t storage_address(mbed::FlashIAP &flash, uint32_t *sector_size_out = nullptr) const;
    uint32_t checksum(const StoredKey &key) const;
    bool validate(const StoredKey &key) const;
};

#endif
