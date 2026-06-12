#pragma once

#include <PipCore/Config/Features.hpp>

#if PIPCORE_TARGET_DESKTOP

#include <PipCore/Platforms/Desktop/Runtime.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

inline constexpr unsigned char BIN = 2;
inline constexpr unsigned char OCT = 8;
inline constexpr unsigned char DEC = 10;
inline constexpr unsigned char HEX = 16;

class String
{
public:
    String() = default;
    String(const char *text) : _value(text ? text : "") {}
    String(const std::string &text) : _value(text) {}
    String(std::string &&text) noexcept : _value(std::move(text)) {}
    String(char ch) : _value(1, ch) {}
    String(unsigned char value) : _value(std::to_string(static_cast<unsigned>(value))) {}
    String(int value) : _value(std::to_string(value)) {}
    String(unsigned value) : _value(std::to_string(value)) {}
    String(long value) : _value(std::to_string(value)) {}
    String(unsigned long value) : _value(std::to_string(value)) {}
    String(long long value) : _value(std::to_string(value)) {}
    String(unsigned long long value) : _value(std::to_string(value)) {}
    String(int value, unsigned base) : _value(formatSignedBase(value, base)) {}
    String(unsigned value, unsigned base) : _value(formatUnsignedBase(value, base)) {}
    String(long value, unsigned base) : _value(formatSignedBase(value, base)) {}
    String(unsigned long value, unsigned base) : _value(formatUnsignedBase(value, base)) {}
    String(long long value, unsigned base) : _value(formatSignedBase(value, base)) {}
    String(unsigned long long value, unsigned base) : _value(formatUnsignedBase(value, base)) {}
    String(float value, unsigned decimals = 2) : _value(formatFloat(static_cast<double>(value), decimals)) {}
    String(double value, unsigned decimals = 2) : _value(formatFloat(value, decimals)) {}

    String &operator=(const char *text)
    {
        _value = text ? text : "";
        return *this;
    }

    String &operator=(const std::string &text)
    {
        _value = text;
        return *this;
    }

    [[nodiscard]] size_t length() const noexcept { return _value.length(); }
    [[nodiscard]] bool reserve(size_t capacity)
    {
        _value.reserve(capacity);
        return true;
    }

    void remove(size_t index)
    {
        if (index >= _value.length())
        {
            _value.clear();
            return;
        }
        _value.erase(index);
    }

    [[nodiscard]] int indexOf(char ch, unsigned fromIndex = 0) const noexcept
    {
        if (fromIndex >= _value.length())
            return -1;
        const size_t pos = _value.find(ch, fromIndex);
        return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
    }

    [[nodiscard]] String substring(unsigned from) const
    {
        if (from >= _value.length())
            return String();
        return String(_value.substr(from));
    }

    [[nodiscard]] String substring(unsigned from, unsigned to) const
    {
        if (from >= _value.length() || to <= from)
            return String();
        const size_t safeTo = std::min<size_t>(to, _value.length());
        return String(_value.substr(from, safeTo - from));
    }

    [[nodiscard]] const char *c_str() const noexcept { return _value.c_str(); }
    [[nodiscard]] char operator[](size_t index) const noexcept { return _value[index]; }

    String &operator+=(const String &rhs)
    {
        _value += rhs._value;
        return *this;
    }

    String &operator+=(const char *rhs)
    {
        _value += rhs ? rhs : "";
        return *this;
    }

    String &operator+=(char rhs)
    {
        _value += rhs;
        return *this;
    }

    [[nodiscard]] friend String operator+(const String &lhs, const String &rhs)
    {
        return String(lhs._value + rhs._value);
    }

    [[nodiscard]] friend String operator+(const String &lhs, const char *rhs)
    {
        return String(lhs._value + (rhs ? rhs : ""));
    }

    [[nodiscard]] friend String operator+(const char *lhs, const String &rhs)
    {
        return String(std::string(lhs ? lhs : "") + rhs._value);
    }

    [[nodiscard]] bool operator==(const String &rhs) const noexcept { return _value == rhs._value; }
    [[nodiscard]] bool operator!=(const String &rhs) const noexcept { return _value != rhs._value; }

private:
    template <typename T>
    [[nodiscard]] static std::string formatUnsignedBase(T value, unsigned base)
    {
        if (base < 2 || base > 16)
            return std::to_string(static_cast<unsigned long long>(value));

        static constexpr char digits[] = "0123456789ABCDEF";
        unsigned long long current = static_cast<unsigned long long>(value);
        char buf[65];
        size_t index = sizeof(buf);
        buf[--index] = '\0';

        do
        {
            buf[--index] = digits[current % base];
            current /= base;
        } while (current != 0);

        return std::string(&buf[index]);
    }

    template <typename T>
    [[nodiscard]] static std::string formatSignedBase(T value, unsigned base)
    {
        if (base < 2 || base > 16)
            return std::to_string(static_cast<long long>(value));
        if (value >= 0)
            return formatUnsignedBase(static_cast<unsigned long long>(value), base);
        return std::string("-") + formatUnsignedBase(static_cast<unsigned long long>(-(value + 1)) + 1ull, base);
    }

    [[nodiscard]] static std::string formatFloat(double value, unsigned decimals)
    {
        if (decimals > 9)
            decimals = 9;
        char fmt[8];
        std::snprintf(fmt, sizeof(fmt), "%%.%uf", decimals);
        char buf[64];
        std::snprintf(buf, sizeof(buf), fmt, value);
        return std::string(buf);
    }

private:
    std::string _value;
};

class HardwareSerial
{
public:
    void begin(unsigned long) noexcept {}

    [[nodiscard]] int available() noexcept
    {
        return pipcore::desktop::Runtime::instance().serialAvailable();
    }

    [[nodiscard]] int read() noexcept
    {
        return pipcore::desktop::Runtime::instance().serialRead();
    }

    [[nodiscard]] size_t availableForWrite() const noexcept
    {
        return pipcore::desktop::Runtime::instance().serialAvailableForWrite();
    }

    size_t write(uint8_t value) noexcept
    {
        return pipcore::desktop::Runtime::instance().serialWrite(value);
    }

    size_t write(const uint8_t *data, size_t len) noexcept
    {
        return pipcore::desktop::Runtime::instance().serialWrite(data, len);
    }

    size_t print(const String &value) noexcept
    {
        return write(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
    }

    size_t print(const char *value) noexcept
    {
        if (!value)
            return 0;
        return write(reinterpret_cast<const uint8_t *>(value), std::strlen(value));
    }

    size_t print(char value) noexcept
    {
        return write(static_cast<uint8_t>(value));
    }

    template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
    size_t print(T value) noexcept
    {
        return print(String(value));
    }

    size_t println() noexcept
    {
        static constexpr uint8_t newline[] = {'\r', '\n'};
        return write(newline, sizeof(newline));
    }

    template <typename T>
    size_t println(const T &value) noexcept
    {
        return print(value) + println();
    }
};

class EspCompat
{
public:
    void restart() noexcept
    {
        std::exit(0);
    }
};

extern HardwareSerial Serial;
extern EspCompat ESP;

[[nodiscard]] inline unsigned long millis() noexcept
{
    return pipcore::desktop::Runtime::instance().nowMs();
}

[[nodiscard]] inline unsigned long micros() noexcept
{
    return static_cast<unsigned long>(pipcore::desktop::Runtime::instance().nowMicros());
}

template <typename T>
[[nodiscard]] constexpr const T &min(const T &a, const T &b) noexcept
{
    return (b < a) ? b : a;
}

template <typename T>
[[nodiscard]] constexpr const T &max(const T &a, const T &b) noexcept
{
    return (a < b) ? b : a;
}

inline void delay(unsigned long ms) noexcept
{
    pipcore::desktop::Runtime::instance().delayMs(static_cast<uint32_t>(ms));
}

#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3

#define LOW 0
#define HIGH 1

inline void pinMode(unsigned char, unsigned char) noexcept {}
inline int digitalRead(unsigned char pin) noexcept
{
    return pipcore::desktop::Runtime::instance().digitalRead(pin) ? HIGH : LOW;
}
inline int analogRead(unsigned char) noexcept { return 0; }

#endif
