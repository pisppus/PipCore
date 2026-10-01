#pragma once

#include <cstddef>
#include <cstdint>

#include "Config.hpp"

namespace pipcore::storage
{
    enum class OpenMode : uint8_t
    {
        Read,
        Write,
        Append,
    };
    class File;
}

#if !PIPCORE_ENABLE_STORAGE

namespace pipcore::storage
{
    [[nodiscard]] bool begin(bool formatOnFail = false) noexcept = delete;
    [[nodiscard]] File open(const char *path) noexcept = delete;
    [[nodiscard]] File open(const char *path, OpenMode mode) noexcept = delete;
    [[nodiscard]] bool remove(const char *path) noexcept = delete;
    [[nodiscard]] bool rename(const char *from, const char *to) noexcept = delete;
    [[nodiscard]] bool mkdir(const char *path) noexcept = delete;
    [[nodiscard]] bool exists(const char *path) noexcept = delete;
}

#else

namespace pipcore::storage
{
    class File
    {
    public:
        File() noexcept = default;
        File(File &&other) noexcept;
        File &operator=(File &&other) noexcept;
        File(const File &) = delete;
        File &operator=(const File &) = delete;
        ~File() noexcept;

        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] bool isDirectory() const noexcept;

        void close() noexcept;

        [[nodiscard]] const char *name() const noexcept;
        [[nodiscard]] File openNextFile() noexcept;

        [[nodiscard]] int read(uint8_t *buf, size_t size) noexcept;
        [[nodiscard]] size_t write(const uint8_t *buf, size_t size) noexcept;
        [[nodiscard]] size_t write(const char *buf, size_t size) noexcept;
        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] bool seek(size_t pos) noexcept;
        void flush() noexcept;

    private:
        friend File open(const char *path) noexcept;
        friend File open(const char *path, OpenMode mode) noexcept;

        File(void *impl) noexcept : _impl(impl) {}

        void *_impl = nullptr;
    };

    [[nodiscard]] bool begin(bool formatOnFail = false) noexcept;
    [[nodiscard]] File open(const char *path) noexcept;
    [[nodiscard]] File open(const char *path, OpenMode mode) noexcept;
    [[nodiscard]] bool remove(const char *path) noexcept;
    [[nodiscard]] bool rename(const char *from, const char *to) noexcept;
    [[nodiscard]] bool mkdir(const char *path) noexcept;
    [[nodiscard]] bool exists(const char *path) noexcept;
}

#endif
