#pragma once

#include <cstddef>

#include <driver/i2c_master.h>

#include "Config.hpp"
#include <Input/Touch.hpp>

#if !PIPCORE_TARGET_ESP32
#error "pipcore::esp32::services::Touch requires ESP32"
#endif

namespace pipcore::esp32::services
{
    class Touch final : public pipcore::Touch
    {
    public:
        Touch() = default;
        ~Touch() override;

        [[nodiscard]] bool configure(const pipcore::TouchConfig &cfg) noexcept override;

        [[nodiscard]] bool begin() noexcept override;
        void end() noexcept override;
        void update() noexcept override;

        [[nodiscard]] bool ready() const noexcept override { return _ready; }
        [[nodiscard]] uint8_t count() const noexcept override { return _reported; }
        [[nodiscard]] pipcore::TouchPoint point(uint8_t index) const noexcept override;

        void markTouched() noexcept
        {
            _touchedFlag = true;
        }

    private:
        struct Slot
        {
            uint8_t id = 0xFF;
            uint16_t x = 0;
            uint16_t y = 0;
            bool present = false;
            bool wasPresent = false;
            pipcore::TouchState state = pipcore::TouchState::Released;
        };

        void remap(uint16_t inX, uint16_t inY, uint16_t &outX, uint16_t &outY) const noexcept;
        Slot *findSlotById(uint8_t id) noexcept;
        Slot *findFreeSlot() noexcept;

        [[nodiscard]] bool i2cWriteReg(uint8_t reg, uint8_t value) noexcept;
        [[nodiscard]] bool i2cReadRegs(uint8_t reg, uint8_t *buf, size_t len) noexcept;

        Slot _slots[MaxPoints] = {};

        uint16_t _width = 0;
        uint16_t _height = 0;
        uint8_t _rotation = 0;

        int8_t _sda = -1;
        int8_t _scl = -1;
        int8_t _intr = -1;
        uint8_t _i2cAddr = 0x38;
        uint32_t _freqHz = 400000;

        i2c_master_bus_handle_t _bus = nullptr;
        i2c_master_dev_handle_t _dev = nullptr;

        bool _ready = false;
        bool _isrAttached = false;
        uint8_t _reported = 0;

        volatile bool _touchedFlag = false;
    };
}
