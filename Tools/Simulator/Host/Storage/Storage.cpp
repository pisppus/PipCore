#include "Config.hpp"
#if PIPCORE_TARGET_DESKTOP && PIPCORE_ENABLE_STORAGE
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <Storage.hpp>
#include "Core/Storage.hpp"

namespace pipcore::storage
{
    namespace
    {
        constexpr const char *kRoot = "sim_fs";

        std::string join(const char *path) noexcept
        {
            std::filesystem::path p(kRoot);
            if (detail::isSafePath(path) && path[0])
            {
                const char *rel = (path[0] == '/') ? path + 1 : path;
                p /= std::filesystem::path(rel).relative_path();
            }
            return p.string();
        }

        struct FileImpl
        {
            std::shared_ptr<std::fstream> stream;
            std::string path;
            bool isDir = false;
            bool dirIterStarted = false;
            std::filesystem::directory_iterator dirIt;
        };
    }

    File::File(File &&other) noexcept : _impl(other._impl)
    {
        other._impl = nullptr;
    }

    File &File::operator=(File &&other) noexcept
    {
        if (this != &other)
        {
            close();
            _impl = other._impl;
            other._impl = nullptr;
        }
        return *this;
    }

    File::~File() noexcept
    {
        close();
    }

    File::operator bool() const noexcept
    {
        if (!_impl)
            return false;
        auto *im = static_cast<const FileImpl *>(_impl);
        return im->isDir ? true : (im->stream && im->stream->is_open());
    }

    bool File::isDirectory() const noexcept
    {
        return _impl && static_cast<FileImpl *>(_impl)->isDir;
    }

    void File::close() noexcept
    {
        if (!_impl)
            return;
        auto *im = static_cast<FileImpl *>(_impl);
        im->stream.reset();
        delete im;
        _impl = nullptr;
    }

    const char *File::name() const noexcept
    {
        static thread_local char nameBuf[128];
        if (!_impl)
            return "";
        auto *im = static_cast<FileImpl *>(_impl);
        const std::filesystem::path fileName = std::filesystem::path(im->path).filename();
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", fileName.string().c_str());
        return nameBuf;
    }

    File File::openNextFile() noexcept
    {
        if (!_impl)
            return {};
        auto *im = static_cast<FileImpl *>(_impl);
        if (!im->isDir)
            return {};

        try
        {
            if (!im->dirIterStarted)
            {
                im->dirIt = std::filesystem::directory_iterator(im->path);
                im->dirIterStarted = true;
            }
            if (im->dirIt == std::filesystem::directory_iterator{})
                return {};

            const std::filesystem::directory_entry entry = *im->dirIt;
            ++im->dirIt;

            auto *child = new FileImpl();
            child->path = entry.path().string();
            child->isDir = entry.is_directory();
            if (!child->isDir)
            {
                child->stream = std::make_shared<std::fstream>(child->path, std::ios::binary | std::ios::in);
            }
            return File(child);
        }
        catch (...)
        {
            return {};
        }
    }

    int File::read(uint8_t *buf, size_t size) noexcept
    {
        if (!_impl)
            return -1;
        auto *im = static_cast<FileImpl *>(_impl);
        if (!im->stream || !im->stream->is_open())
            return -1;
        im->stream->read(reinterpret_cast<char *>(buf), static_cast<std::streamsize>(size));
        return static_cast<int>(im->stream->gcount());
    }

    size_t File::write(const uint8_t *buf, size_t size) noexcept
    {
        if (!_impl)
            return 0;
        auto *im = static_cast<FileImpl *>(_impl);
        if (!im->stream || !im->stream->is_open())
            return 0;
        im->stream->write(reinterpret_cast<const char *>(buf), static_cast<std::streamsize>(size));
        return im->stream->good() ? size : 0;
    }

    size_t File::write(const char *buf, size_t size) noexcept
    {
        return write(reinterpret_cast<const uint8_t *>(buf), size);
    }

    size_t File::size() const noexcept
    {
        if (!_impl)
            return 0;
        auto *im = static_cast<FileImpl *>(_impl);
        if (im->isDir)
            return 0;
        try
        {
            return static_cast<size_t>(std::filesystem::file_size(im->path));
        }
        catch (...)
        {
            return 0;
        }
    }

    bool File::seek(size_t pos) noexcept
    {
        if (!_impl)
            return false;
        auto *im = static_cast<FileImpl *>(_impl);
        if (!im->stream || !im->stream->is_open())
            return false;
        im->stream->clear();
        im->stream->seekg(static_cast<std::streamoff>(pos));
        im->stream->seekp(static_cast<std::streamoff>(pos));
        return true;
    }

    void File::flush() noexcept
    {
        if (!_impl)
            return;
        auto *im = static_cast<FileImpl *>(_impl);
        if (im->stream && im->stream->is_open())
            im->stream->flush();
    }

    bool begin(bool formatOnFail) noexcept
    {
        (void)formatOnFail;
        try
        {
            std::filesystem::create_directories(kRoot);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    File open(const char *path) noexcept
    {
        const std::string full = join(path);
        auto *im = new FileImpl();
        im->path = full;
        try
        {
            if (std::filesystem::is_directory(full))
            {
                im->isDir = true;
                return File(im);
            }
            im->stream = std::make_shared<std::fstream>(full, std::ios::binary | std::ios::in);
        }
        catch (...)
        {
            im->isDir = false;
        }
        return File(im);
    }

    File open(const char *path, OpenMode mode) noexcept
    {
        const std::string full = join(path);
        auto *im = new FileImpl();
        im->path = full;

        std::ios_base::openmode iosMode = std::ios::binary;
        if (mode == OpenMode::Read)
        {
            iosMode |= std::ios::in;
        }
        else if (mode == OpenMode::Write)
        {
            iosMode |= std::ios::out | std::ios::trunc;
        }
        else
        {
            iosMode |= std::ios::out | std::ios::app;
        }

        if (mode != OpenMode::Read)
        {
            try
            {
                std::filesystem::create_directories(std::filesystem::path(full).parent_path());
            }
            catch (...)
            {
            }
        }

        im->stream = std::make_shared<std::fstream>(full, iosMode);
        return File(im);
    }

    bool remove(const char *path) noexcept
    {
        try
        {
            return std::filesystem::remove(join(path));
        }
        catch (...)
        {
            return false;
        }
    }

    bool rename(const char *from, const char *to) noexcept
    {
        try
        {
            const std::filesystem::path dst = join(to);
            std::filesystem::create_directories(dst.parent_path());
            std::filesystem::rename(join(from), dst);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool mkdir(const char *path) noexcept
    {
        try
        {
            return std::filesystem::create_directories(join(path));
        }
        catch (...)
        {
            return false;
        }
    }

    bool exists(const char *path) noexcept
    {
        try
        {
            return std::filesystem::exists(join(path));
        }
        catch (...)
        {
            return false;
        }
    }
}

#endif
