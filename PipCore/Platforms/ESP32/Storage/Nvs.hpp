#pragma once

#include <cstddef>
#include <cstdint>

#include <Prefs.hpp>

#if !PIPCORE_TARGET_ESP32
#error "pipcore::esp32::services::Nvs requires ESP32 target"
#endif

namespace pipcore::esp32::services
{
    class Nvs final : public pipcore::prefs::Backend
    {
    public:
        Nvs() = default;

        [[nodiscard]] bool getU8(const char *key, uint8_t &out) noexcept override;
        [[nodiscard]] bool getU16(const char *key, uint16_t &out) noexcept override;
        [[nodiscard]] bool getU32(const char *key, uint32_t &out) noexcept override;
        [[nodiscard]] bool getU64(const char *key, uint64_t &out) noexcept override;
        [[nodiscard]] bool getI32(const char *key, int32_t &out) noexcept override;
        [[nodiscard]] bool getStr(const char *key, char *out, size_t cap) noexcept override;
        [[nodiscard]] bool getBlob(const char *key, void *out, size_t &len) noexcept override;

        bool setU8(const char *key, uint8_t v) noexcept override;
        bool setU16(const char *key, uint16_t v) noexcept override;
        bool setU32(const char *key, uint32_t v) noexcept override;
        bool setU64(const char *key, uint64_t v) noexcept override;
        bool setI32(const char *key, int32_t v) noexcept override;
        bool setStr(const char *key, const char *v) noexcept override;
        bool setBlob(const char *key, const void *data, size_t len) noexcept override;

        bool eraseKey(const char *key) noexcept override;
        bool eraseAll() noexcept override;
    };
}
