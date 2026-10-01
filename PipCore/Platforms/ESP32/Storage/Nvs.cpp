#include <nvs_flash.h>
#include <nvs.h>

#include "Platforms/ESP32/Storage/Nvs.hpp"
#include "Platforms/ESP32/Core/Core.hpp"

namespace pipcore::esp32::services
{
    namespace
    {
        nvs_handle_t s_nvs = 0;

        [[nodiscard]] bool ensureNvsOpen() noexcept
        {
            if (s_nvs != 0)
                return true;

            if (!pipcore::esp32::nvsEnsureReady())
                return false;

            return nvs_open("pipcore", NVS_READWRITE, &s_nvs) == ESP_OK;
        }

        [[nodiscard]] bool commitAnd(bool wrote) noexcept
        {
            return wrote && nvs_commit(s_nvs) == ESP_OK;
        }
    }

    bool Nvs::getU8(const char *key, uint8_t &out) noexcept
    {
        return ensureNvsOpen() && nvs_get_u8(s_nvs, key, &out) == ESP_OK;
    }

    bool Nvs::getU16(const char *key, uint16_t &out) noexcept
    {
        return ensureNvsOpen() && nvs_get_u16(s_nvs, key, &out) == ESP_OK;
    }

    bool Nvs::getU32(const char *key, uint32_t &out) noexcept
    {
        return ensureNvsOpen() && nvs_get_u32(s_nvs, key, &out) == ESP_OK;
    }

    bool Nvs::getU64(const char *key, uint64_t &out) noexcept
    {
        return ensureNvsOpen() && nvs_get_u64(s_nvs, key, &out) == ESP_OK;
    }

    bool Nvs::getI32(const char *key, int32_t &out) noexcept
    {
        return ensureNvsOpen() && nvs_get_i32(s_nvs, key, &out) == ESP_OK;
    }

    bool Nvs::getStr(const char *key, char *out, size_t cap) noexcept
    {
        if (!out || cap == 0)
            return false;
        return ensureNvsOpen() && nvs_get_str(s_nvs, key, out, &cap) == ESP_OK;
    }

    bool Nvs::getBlob(const char *key, void *out, size_t &len) noexcept
    {
        return ensureNvsOpen() && nvs_get_blob(s_nvs, key, out, &len) == ESP_OK;
    }

    bool Nvs::setU8(const char *key, uint8_t v) noexcept
    {
        return ensureNvsOpen() && commitAnd(nvs_set_u8(s_nvs, key, v) == ESP_OK);
    }

    bool Nvs::setU16(const char *key, uint16_t v) noexcept
    {
        return ensureNvsOpen() && commitAnd(nvs_set_u16(s_nvs, key, v) == ESP_OK);
    }

    bool Nvs::setU32(const char *key, uint32_t v) noexcept
    {
        return ensureNvsOpen() && commitAnd(nvs_set_u32(s_nvs, key, v) == ESP_OK);
    }

    bool Nvs::setU64(const char *key, uint64_t v) noexcept
    {
        return ensureNvsOpen() && commitAnd(nvs_set_u64(s_nvs, key, v) == ESP_OK);
    }

    bool Nvs::setI32(const char *key, int32_t v) noexcept
    {
        return ensureNvsOpen() && commitAnd(nvs_set_i32(s_nvs, key, v) == ESP_OK);
    }

    bool Nvs::setStr(const char *key, const char *v) noexcept
    {
        return ensureNvsOpen() && commitAnd(nvs_set_str(s_nvs, key, v) == ESP_OK);
    }

    bool Nvs::setBlob(const char *key, const void *data, size_t len) noexcept
    {
        return ensureNvsOpen() && commitAnd(nvs_set_blob(s_nvs, key, data, len) == ESP_OK);
    }

    bool Nvs::eraseKey(const char *key) noexcept
    {
        return ensureNvsOpen() && commitAnd(nvs_erase_key(s_nvs, key) == ESP_OK);
    }

    bool Nvs::eraseAll() noexcept
    {
        return ensureNvsOpen() && commitAnd(nvs_erase_all(s_nvs) == ESP_OK);
    }
}
