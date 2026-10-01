#pragma once

#include <cmath>

#include <Platform.hpp>
#include <Input/Button.hpp>

namespace pipcore
{
    struct AnalogAxisConfig
    {
        uint8_t pin = 0xFF;
        int16_t minValue = 0;
        int16_t maxValue = 4095;
        float deadZone = 0.12f;
        bool inverted = false;
    };

    class AnalogAxis
    {
    private:
        Platform *_platform;
        AnalogAxisConfig cfg;
        float filtered;
        bool initialized;
        float rangeInv;
        float deadZone;
        float invDeadSpan;

    public:
        AnalogAxis(Platform *plat = nullptr, const AnalogAxisConfig &c = AnalogAxisConfig())
            : _platform(plat), cfg(c), filtered(0.0f), initialized(false), rangeInv(0.0f), deadZone(c.deadZone),
              invDeadSpan(1.0f) {}

        void begin()
        {
            if (cfg.pin == 0xFF)
                return;
            filtered = 0.0f;
            initialized = true;

            if (cfg.maxValue > cfg.minValue)
            {
                rangeInv = 1.0f / static_cast<float>(cfg.maxValue - cfg.minValue);
            }
            else
            {
                rangeInv = 0.0f;
            }

            deadZone = cfg.deadZone;
            if (deadZone < 0.0f)
                deadZone = 0.0f;
            if (deadZone > 0.999f)
                deadZone = 0.999f;
            float span = 1.0f - deadZone;
            invDeadSpan = (span > 1e-6f) ? (1.0f / span) : 1.0f;
        }

        float readNormalized()
        {
            if (!initialized)
                return 0.0f;

            Platform *plat = _platform ? _platform : GetPlatform();
            if (!plat)
                return 0.0f;
            const int raw = plat->analogRead(cfg.pin);

            float v = 0.0f;
            if (rangeInv > 0.0f)
            {
                float n = static_cast<float>(raw - cfg.minValue) * rangeInv;
                if (n < 0.0f)
                    n = 0.0f;
                if (n > 1.0f)
                    n = 1.0f;
                v = n * 2.0f - 1.0f;
            }

            if (cfg.inverted)
                v = -v;
            return v;
        }

        float applyDeadZone(float v) const
        {
            float av = std::abs(v);
            if (av < deadZone)
                return 0.0f;

            float k = (av - deadZone) * invDeadSpan;
            if (k > 1.0f)
                k = 1.0f;
            return (v > 0.0f ? 1.0f : -1.0f) * k;
        }

        float update(float deltaTime)
        {
            if (!initialized)
                return filtered;

            const float v = applyDeadZone(readNormalized());

            float alpha;
            if (deltaTime > 0.0f)
            {
                float cutoff = 16.0f;
                alpha = cutoff * deltaTime;
                if (alpha > 1.0f)
                    alpha = 1.0f;
            }
            else
            {
                alpha = 0.25f;
            }

            filtered += (v - filtered) * alpha;
            return filtered;
        }

        float value() const { return filtered; }
    };

    struct JoystickConfig
    {
        AnalogAxisConfig axisX;
        AnalogAxisConfig axisY;
        float deadZone = 0.12f;
        uint8_t buttonPin = 0xFF;
        InputMode buttonPull = InputMode::Pullup;
    };

    class Joystick
    {
    private:
        Platform *_platform;
        AnalogAxis ax;
        AnalogAxis ay;
        Button btn;
        bool hasButton;
        float fx;
        float fy;
        float deadZone;
        float invDeadSpan;

        [[nodiscard]] static float clampDeadZone(float dz) noexcept
        {
            if (dz < 0.0f)
                dz = 0.0f;
            if (dz > 0.999f)
                dz = 0.999f;
            return dz;
        }

    public:
        Joystick(Platform *plat = nullptr, const JoystickConfig &cfg = JoystickConfig())
            : _platform(plat), ax(plat, cfg.axisX), ay(plat, cfg.axisY), btn(plat, cfg.buttonPin, cfg.buttonPull),
              hasButton(cfg.buttonPin != 0xFF), fx(0.0f), fy(0.0f), deadZone(clampDeadZone(cfg.deadZone)),
              invDeadSpan((1.0f - deadZone) > 1e-6f ? 1.0f / (1.0f - deadZone) : 1.0f)
        {
        }

        ~Joystick() = default;

        void begin()
        {
            ax.begin();
            ay.begin();
            if (hasButton)
            {
                btn.begin();
            }
        }

        void update(float deltaTime)
        {

            const float rx = ax.readNormalized();
            const float ry = ay.readNormalized();

            float vx = 0.0f;
            float vy = 0.0f;
            const float m = std::sqrt(rx * rx + ry * ry);
            if (m > deadZone)
            {
                float k = (m - deadZone) * invDeadSpan;
                if (k > 1.0f)
                    k = 1.0f;
                const float s = k / m;
                vx = rx * s;
                vy = ry * s;
            }

            float alpha;
            if (deltaTime > 0.0f)
            {
                float cutoff = 16.0f;
                alpha = cutoff * deltaTime;
                if (alpha > 1.0f)
                    alpha = 1.0f;
            }
            else
            {
                alpha = 0.25f;
            }

            fx += (vx - fx) * alpha;
            fy += (vy - fy) * alpha;

            if (hasButton)
            {
                btn.update();
            }
        }

        float x() const { return fx; }
        float y() const { return fy; }

        bool isPressed() const { return hasButton ? btn.isDown() : false; }
        bool wasPressed() { return hasButton ? btn.wasPressed() : false; }
    };
}
