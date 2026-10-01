#include "Config.hpp"
#if PIPCORE_TARGET_ESP32
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "Platforms/ESP32/Input/Touch.hpp"

namespace pipcore::esp32::services
{
    namespace
    {
        constexpr const char *kTag = "Touch";

        constexpr uint8_t RegDevMode = 0x00;
        constexpr uint8_t RegTdStatus = 0x02;
        constexpr uint8_t RegP1 = 0x03;
        constexpr uint8_t RegP2 = 0x09;
        constexpr uint8_t RegGMode = 0xA4;

        void IRAM_ATTR touchIsrHandler(void *arg)
        {
            if (arg)
            {
                static_cast<Touch *>(arg)->markTouched();
            }
        }
    }

    Touch::~Touch()
    {
        end();
    }

    bool Touch::configure(const pipcore::TouchConfig &cfg) noexcept
    {
        end();
        if (cfg.intr < 0 || cfg.intr >= GPIO_NUM_MAX)
        {
            ESP_LOGE(kTag, "configure: INT pin is invalid or not configured");
            return false;
        }
        _sda = cfg.sda;
        _scl = cfg.scl;
        _intr = cfg.intr;
        _i2cAddr = cfg.i2cAddr ? cfg.i2cAddr : 0x38;
        _freqHz = cfg.freqHz ? cfg.freqHz : 400000U;
        _width = cfg.width;
        _height = cfg.height;
        _rotation = cfg.rotation & 3u;
        return true;
    }

    bool Touch::begin() noexcept
    {
        if (_ready)
            return true;

        if (_intr < 0 || _intr >= GPIO_NUM_MAX || _sda < 0 || _sda >= GPIO_NUM_MAX || _scl < 0 || _scl >= GPIO_NUM_MAX)
        {
            ESP_LOGW(kTag, "begin: pins not fully configured. Touch is disabled.");
            return false;
        }

        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = I2C_NUM_0;
        bus_cfg.sda_io_num = static_cast<gpio_num_t>(_sda);
        bus_cfg.scl_io_num = static_cast<gpio_num_t>(_scl);
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = true;

        i2c_master_bus_handle_t bus_handle = nullptr;
        esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus_handle);
        if (err != ESP_OK || !bus_handle)
        {
            ESP_LOGE(kTag, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
            return false;
        }

        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = _i2cAddr;
        dev_cfg.scl_speed_hz = _freqHz;

        i2c_master_dev_handle_t dev_handle = nullptr;
        err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
        if (err != ESP_OK || !dev_handle)
        {
            ESP_LOGE(kTag, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
            i2c_del_master_bus(bus_handle);
            return false;
        }

        _bus = bus_handle;
        _dev = dev_handle;

        uint8_t devMode = 0xFF;
        if (!i2cReadRegs(RegDevMode, &devMode, 1))
        {
            ESP_LOGE(kTag, "no ACK at touch addr 0x%02X", _i2cAddr);
            end();
            return false;
        }

        (void)i2cWriteReg(RegGMode, 0x00);

        for (uint8_t i = 0; i < MaxPoints; ++i)
            _slots[i] = Slot{};
        _reported = 0;
        _touchedFlag = false;

        const gpio_num_t intPin = static_cast<gpio_num_t>(_intr);
        gpio_config_t io{};
        io.pin_bit_mask = 1ULL << static_cast<uint8_t>(_intr);
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_NEGEDGE;
        if (gpio_config(&io) != ESP_OK)
        {
            ESP_LOGE(kTag, "gpio_config failed for INT pin %d", _intr);
            end();
            return false;
        }

        const esp_err_t isrErr = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (isrErr != ESP_OK && isrErr != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(kTag, "gpio_install_isr_service failed: %s", esp_err_to_name(isrErr));
            end();
            return false;
        }

        if (gpio_isr_handler_add(intPin, touchIsrHandler, this) != ESP_OK)
        {
            ESP_LOGE(kTag, "gpio_isr_handler_add failed for pin %d", _intr);
            end();
            return false;
        }
        _isrAttached = true;

        _ready = true;
        ESP_LOGI(kTag, "ready (INT pin %d, addr 0x%02X)", _intr, _i2cAddr);
        return true;
    }

    void Touch::end() noexcept
    {
        if (_isrAttached)
        {
            gpio_isr_handler_remove(static_cast<gpio_num_t>(_intr));

            (void)gpio_set_intr_type(static_cast<gpio_num_t>(_intr), GPIO_INTR_DISABLE);
            _isrAttached = false;
        }

        if (_dev)
        {
            i2c_master_bus_rm_device(_dev);
            _dev = nullptr;
        }
        if (_bus)
        {
            i2c_del_master_bus(_bus);
            _bus = nullptr;
        }

        _ready = false;
        _reported = 0;
        _touchedFlag = false;
        for (uint8_t i = 0; i < MaxPoints; ++i)
            _slots[i] = Slot{};
    }

    bool Touch::i2cWriteReg(uint8_t reg, uint8_t value) noexcept
    {
        if (!_dev)
            return false;
        const uint8_t data[] = {reg, value};
        return i2c_master_transmit(_dev, data, sizeof(data), 50) == ESP_OK;
    }

    bool Touch::i2cReadRegs(uint8_t reg, uint8_t *buf, size_t len) noexcept
    {
        if (!_dev || !buf || len == 0)
            return false;
        return i2c_master_transmit_receive(_dev, &reg, 1, buf, len, 50) == ESP_OK;
    }

    Touch::Slot *Touch::findSlotById(uint8_t id) noexcept
    {
        for (uint8_t i = 0; i < MaxPoints; ++i)
            if (_slots[i].id == id)
                return &_slots[i];
        return nullptr;
    }

    Touch::Slot *Touch::findFreeSlot() noexcept
    {
        for (uint8_t i = 0; i < MaxPoints; ++i)
            if (_slots[i].id == 0xFF)
                return &_slots[i];
        return nullptr;
    }

    void Touch::remap(uint16_t inX, uint16_t inY, uint16_t &outX, uint16_t &outY) const noexcept
    {
        switch (_rotation)
        {
        default:
        case 0:
            outX = inX;
            outY = inY;
            break;
        case 1:
            outX = inY;
            outY = static_cast<uint16_t>(_height - 1 - inX);
            break;
        case 2:
            outX = static_cast<uint16_t>(_width - 1 - inX);
            outY = static_cast<uint16_t>(_height - 1 - inY);
            break;
        case 3:
            outX = static_cast<uint16_t>(_width - 1 - inY);
            outY = static_cast<uint16_t>(_height - 1 - inX);
            break;
        }
        if (outX >= _width)
            outX = static_cast<uint16_t>(_width - 1);
        if (outY >= _height)
            outY = static_cast<uint16_t>(_height - 1);
    }

    void Touch::update() noexcept
    {
        if (!_ready)
            return;

        for (uint8_t i = 0; i < MaxPoints; ++i)
        {
            Slot &s = _slots[i];
            if (s.state == pipcore::TouchState::Released && !s.present)
                s = Slot{};
        }

        const bool flag = _touchedFlag;
        _touchedFlag = false;

        bool physicallyPressed = false;
        if (_intr >= 0 && _intr < GPIO_NUM_MAX)
        {
            physicallyPressed = (gpio_get_level(static_cast<gpio_num_t>(_intr)) == 0);
        }

        if (!flag && !physicallyPressed && _reported == 0)
        {
            return;
        }

        for (uint8_t i = 0; i < MaxPoints; ++i)
        {
            _slots[i].wasPresent = _slots[i].present;
            _slots[i].present = false;
        }

        uint8_t td = 0;
        if (!i2cReadRegs(RegTdStatus, &td, 1))
            return;

        uint8_t n = td & 0x0F;
        if (n > MaxPoints)
            n = MaxPoints;

        if (n > 0)
        {
            uint8_t buf[RegP2 - RegP1 + 6] = {};
            const size_t want = (n >= 2) ? sizeof(buf) : 6u;
            if (!i2cReadRegs(RegP1, buf, want))
                return;

            auto decodePoint = [](const uint8_t *r, uint16_t &outX, uint16_t &outY, uint8_t &outId) noexcept
            {
                outX = (static_cast<uint16_t>(r[0] & 0x0F) << 8) | r[1];
                outY = (static_cast<uint16_t>(r[2] & 0x0F) << 8) | r[3];
                outId = (r[2] >> 4) & 0x0F;
            };

            for (uint8_t i = 0; i < n; ++i)
            {
                const uint8_t *pointBuf = (i == 0) ? buf : (buf + (RegP2 - RegP1));
                uint16_t rx = 0, ry = 0;
                uint8_t rid = 0;
                decodePoint(pointBuf, rx, ry, rid);

                Slot *s = findSlotById(rid);
                if (!s)
                    s = findFreeSlot();
                if (!s)
                    continue;

                const bool fresh = (s->id == 0xFF);
                if (fresh)
                    s->id = rid;

                uint16_t mx = 0, my = 0;
                remap(rx, ry, mx, my);

                if (fresh)
                {
                    s->x = mx;
                    s->y = my;
                    s->state = pipcore::TouchState::Pressed;
                }
                else
                {
                    const uint16_t prevX = s->x;
                    const uint16_t prevY = s->y;

                    const int32_t dx = std::abs(static_cast<int32_t>(mx) - static_cast<int32_t>(prevX));
                    const int32_t dy = std::abs(static_cast<int32_t>(my) - static_cast<int32_t>(prevY));
                    const int32_t dist = dx + dy;

                    if (dist <= 2)
                    {
                        mx = prevX;
                        my = prevY;
                    }
                    else if (dist <= 12)
                    {
                        mx = static_cast<uint16_t>((mx + prevX) >> 1);
                        my = static_cast<uint16_t>((my + prevY) >> 1);
                    }

                    s->x = mx;
                    s->y = my;

                    if (mx != prevX || my != prevY)
                    {
                        s->state = pipcore::TouchState::Moved;
                    }
                    else
                    {
                        s->state = pipcore::TouchState::Held;
                    }
                }
                s->present = true;
            }
        }

        uint8_t reported = 0;
        for (uint8_t i = 0; i < MaxPoints; ++i)
        {
            Slot &s = _slots[i];
            if (!s.present)
            {
                if (s.wasPresent)
                    s.state = pipcore::TouchState::Released;
                else if (s.state == pipcore::TouchState::Released)
                    s.id = 0xFF;
            }

            if (s.id != 0xFF)
                ++reported;
        }
        _reported = reported;
    }

    pipcore::TouchPoint Touch::point(uint8_t index) const noexcept
    {
        if (index >= MaxPoints)
            return {};

        uint8_t out = 0;
        for (uint8_t i = 0; i < MaxPoints; ++i)
        {
            const Slot &s = _slots[i];

            if (s.id == 0xFF)
                continue;
            if (out == index)
            {
                pipcore::TouchPoint p;
                p.x = s.x;
                p.y = s.y;
                p.id = s.id;
                p.state = s.state;
                return p;
            }
            ++out;
        }
        return {};
    }
}

#endif
