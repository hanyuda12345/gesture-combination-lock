#include "key_store.h"

#include "FlashIAP.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr uint32_t KEY_MAGIC = 0x59454B47UL; // "GKEY"
constexpr uint16_t KEY_VERSION = 1;

uint32_t align_up(uint32_t value, uint32_t alignment)
{
    if (alignment == 0) {
        return value;
    }
    const uint32_t remainder = value % alignment;
    return remainder == 0 ? value : value + alignment - remainder;
}
}

bool KeyStore::load(StoredKey &key)
{
    mbed::FlashIAP flash;
    if (flash.init() != 0) {
        make_default(key);
        return false;
    }

    const uint32_t address = storage_address(flash);
    flash.read(&key, address, sizeof(key));
    flash.deinit();

    if (!validate(key)) {
        make_default(key);
        return false;
    }

    return true;
}

bool KeyStore::save(const GestureTemplate gestures[KEY_GESTURE_COUNT])
{
    StoredKey key;
    make_default(key);
    for (int i = 0; i < KEY_GESTURE_COUNT; ++i) {
        key.gestures[i] = gestures[i];
    }
    key.checksum = checksum(key);

    mbed::FlashIAP flash;
    if (flash.init() != 0) {
        return false;
    }

    uint32_t sector_size = 0;
    const uint32_t address = storage_address(flash, &sector_size);
    const uint32_t page_size = flash.get_page_size();
    const uint32_t write_size = align_up(sizeof(key), page_size);

    uint8_t buffer[128];
    if (write_size > sizeof(buffer)) {
        flash.deinit();
        return false;
    }

    std::memset(buffer, flash.get_erase_value(), sizeof(buffer));
    std::memcpy(buffer, &key, sizeof(key));

    const int erase_status = flash.erase(address, sector_size);
    const int program_status = erase_status == 0 ? flash.program(buffer, address, write_size) : erase_status;
    flash.deinit();

    return program_status == 0;
}

void KeyStore::make_default(StoredKey &key) const
{
    std::memset(&key, 0, sizeof(key));
    key.magic = KEY_MAGIC;
    key.version = KEY_VERSION;
    key.count = KEY_GESTURE_COUNT;
}

uint32_t KeyStore::storage_address(mbed::FlashIAP &flash, uint32_t *sector_size_out) const
{
    const uint32_t flash_start = flash.get_flash_start();
    const uint32_t flash_end = flash_start + flash.get_flash_size();
    const uint32_t last_sector_size = flash.get_sector_size(flash_end - 1);
    const uint32_t address = flash_end - last_sector_size;

    if (sector_size_out) {
        *sector_size_out = last_sector_size;
    }
    return address;
}

uint32_t KeyStore::checksum(const StoredKey &key) const
{
    StoredKey copy = key;
    copy.checksum = 0;

    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&copy);
    uint32_t hash = 2166136261UL;
    for (size_t i = 0; i < sizeof(copy); ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

bool KeyStore::validate(const StoredKey &key) const
{
    if (key.magic != KEY_MAGIC || key.version != KEY_VERSION || key.count != KEY_GESTURE_COUNT) {
        return false;
    }
    return key.checksum == checksum(key);
}
