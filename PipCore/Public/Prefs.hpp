#pragma once

#include <cstddef>
#include <cstdint>

#include "Config.hpp"
#include <Platform.hpp>

namespace pipcore::prefs
{
    class Backend
    {
    public:
        virtual ~Backend() = default;

        [[nodiscard]] virtual bool getU8(const char *key, uint8_t &out) noexcept = 0;
        [[nodiscard]] virtual bool getU16(const char *key, uint16_t &out) noexcept = 0;
        [[nodiscard]] virtual bool getU32(const char *key, uint32_t &out) noexcept = 0;
        [[nodiscard]] virtual bool getU64(const char *key, uint64_t &out) noexcept = 0;
        [[nodiscard]] virtual bool getI32(const char *key, int32_t &out) noexcept = 0;

        [[nodiscard]] virtual bool getStr(const char *key, char *out, size_t cap) noexcept = 0;

        [[nodiscard]] virtual bool getBlob(const char *key, void *out, size_t &len) noexcept = 0;

        virtual bool setU8(const char *key, uint8_t v) noexcept = 0;
        virtual bool setU16(const char *key, uint16_t v) noexcept = 0;
        virtual bool setU32(const char *key, uint32_t v) noexcept = 0;
        virtual bool setU64(const char *key, uint64_t v) noexcept = 0;
        virtual bool setI32(const char *key, int32_t v) noexcept = 0;
        virtual bool setStr(const char *key, const char *v) noexcept = 0;
        virtual bool setBlob(const char *key, const void *data, size_t len) noexcept = 0;

        virtual bool eraseKey(const char *key) noexcept = 0;
        virtual bool eraseAll() noexcept = 0;
    };

#if !PIPCORE_ENABLE_PREFS

    [[nodiscard]] Backend *backend() noexcept = delete;
    [[nodiscard]] bool getU8(const char *key, uint8_t &out) noexcept = delete;
    [[nodiscard]] bool getU16(const char *key, uint16_t &out) noexcept = delete;
    [[nodiscard]] bool getU32(const char *key, uint32_t &out) noexcept = delete;
    [[nodiscard]] bool getU64(const char *key, uint64_t &out) noexcept = delete;
    [[nodiscard]] bool getI32(const char *key, int32_t &out) noexcept = delete;
    [[nodiscard]] bool getStr(const char *key, char *out, size_t cap) noexcept = delete;
    [[nodiscard]] bool getBlob(const char *key, void *out, size_t &len) noexcept = delete;
    bool setU8(const char *key, uint8_t v) noexcept = delete;
    bool setU16(const char *key, uint16_t v) noexcept = delete;
    bool setU32(const char *key, uint32_t v) noexcept = delete;
    bool setU64(const char *key, uint64_t v) noexcept = delete;
    bool setI32(const char *key, int32_t v) noexcept = delete;
    bool setStr(const char *key, const char *v) noexcept = delete;
    bool setBlob(const char *key, const void *data, size_t len) noexcept = delete;
    bool eraseKey(const char *key) noexcept = delete;
    bool eraseAll() noexcept = delete;

#else

    [[nodiscard]] inline Backend *backend() noexcept
    {
        Platform *p = GetPlatform();
        return p ? p->prefs() : nullptr;
    }

    [[nodiscard]] inline bool getU8(const char *key, uint8_t &out) noexcept
    {
        Backend *b = backend();
        return b && b->getU8(key, out);
    }
    [[nodiscard]] inline bool getU16(const char *key, uint16_t &out) noexcept
    {
        Backend *b = backend();
        return b && b->getU16(key, out);
    }
    [[nodiscard]] inline bool getU32(const char *key, uint32_t &out) noexcept
    {
        Backend *b = backend();
        return b && b->getU32(key, out);
    }
    [[nodiscard]] inline bool getU64(const char *key, uint64_t &out) noexcept
    {
        Backend *b = backend();
        return b && b->getU64(key, out);
    }
    [[nodiscard]] inline bool getI32(const char *key, int32_t &out) noexcept
    {
        Backend *b = backend();
        return b && b->getI32(key, out);
    }
    [[nodiscard]] inline bool getStr(const char *key, char *out, size_t cap) noexcept
    {
        Backend *b = backend();
        return b && b->getStr(key, out, cap);
    }
    [[nodiscard]] inline bool getBlob(const char *key, void *out, size_t &len) noexcept
    {
        Backend *b = backend();
        return b && b->getBlob(key, out, len);
    }
    inline bool setU8(const char *key, uint8_t v) noexcept
    {
        Backend *b = backend();
        return b && b->setU8(key, v);
    }
    inline bool setU16(const char *key, uint16_t v) noexcept
    {
        Backend *b = backend();
        return b && b->setU16(key, v);
    }
    inline bool setU32(const char *key, uint32_t v) noexcept
    {
        Backend *b = backend();
        return b && b->setU32(key, v);
    }
    inline bool setU64(const char *key, uint64_t v) noexcept
    {
        Backend *b = backend();
        return b && b->setU64(key, v);
    }
    inline bool setI32(const char *key, int32_t v) noexcept
    {
        Backend *b = backend();
        return b && b->setI32(key, v);
    }
    inline bool setStr(const char *key, const char *v) noexcept
    {
        Backend *b = backend();
        return b && b->setStr(key, v);
    }
    inline bool setBlob(const char *key, const void *data, size_t len) noexcept
    {
        Backend *b = backend();
        return b && b->setBlob(key, data, len);
    }
    inline bool eraseKey(const char *key) noexcept
    {
        Backend *b = backend();
        return b && b->eraseKey(key);
    }
    inline bool eraseAll() noexcept
    {
        Backend *b = backend();
        return b && b->eraseAll();
    }

#endif
}
