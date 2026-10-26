#pragma once
#include <cstddef>
#include <cstdint>
#define LOG_I2C_DEVICE(...) do {} while (0)
namespace esphome::i2c {
enum ErrorCode { ERROR_OK = 0, ERROR_NOT_ACKNOWLEDGED = 2 };
class I2CBus {
 public:
  virtual ~I2CBus() = default;
  virtual ErrorCode write_readv(uint8_t, const uint8_t *, size_t, uint8_t *, size_t) = 0;
};
class I2CDevice {
 public:
  void set_i2c_bus(I2CBus *bus) { bus_ = bus; }
  void set_i2c_address(uint8_t address) { address_ = address; }
  ErrorCode write(const uint8_t *data, size_t length) const {
    return bus_->write_readv(address_, data, length, nullptr, 0);
  }
  bool write_byte_16(uint8_t reg, uint16_t value) {
    const uint8_t data[]{reg, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return write(data, 3) == ERROR_OK;
  }
  bool read_byte_16(uint8_t reg, uint16_t *value) {
    uint8_t data[2];
    if (bus_->write_readv(address_, &reg, 1, data, 2) != ERROR_OK) return false;
    *value = static_cast<uint16_t>((data[0] << 8) | data[1]);
    return true;
  }
 protected:
  I2CBus *bus_{nullptr};
  uint8_t address_{0};
};
}  // namespace esphome::i2c
