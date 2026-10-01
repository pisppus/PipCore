#include "Config.hpp"

#if PIPCORE_ENABLE_STORAGE && PIPCORE_TARGET_ESP32
#include <esp_littlefs.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>

#include <Storage.hpp>
#include "Core/Storage.hpp"

namespace pipcore::storage
{
    namespace
    {
        constexpr const char *kMount = "/littlefs";

        struct FileImpl
        {
            FILE *fp = nullptr;
            DIR *dir = nullptr;
            std::string path;
            bool isDir = false;
        };

        std::string join(const char *path) noexcept
        {
            if (!detail::isSafePath(path))
                return {};
            std::string out(kMount);
            if (path[0] != '/')
                out += '/';
            out += path;
            return out;
        }
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
        return im->isDir ? im->dir != nullptr : im->fp != nullptr;
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
        if (im->fp)
            std::fclose(im->fp);
        if (im->dir)
            ::closedir(im->dir);
        delete im;
        _impl = nullptr;
    }

    const char *File::name() const noexcept
    {
        static thread_local char nameBuf[128];
        if (!_impl)
            return "";
        auto *im = static_cast<FileImpl *>(_impl);
        const size_t start = (im->path.find_last_of('/') == std::string::npos) ? 0 : im->path.find_last_of('/') + 1;
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", im->path.c_str() + start);
        return nameBuf;
    }

    File File::openNextFile() noexcept
    {
        if (!_impl)
            return {};
        auto *im = static_cast<FileImpl *>(_impl);
        if (!im->dir)
            return {};

        while (true)
        {
            const dirent *ent = ::readdir(im->dir);
            if (!ent)
                return {};

            if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0)
                continue;

            FileImpl *child = new FileImpl();
            child->path = im->path + "/" + ent->d_name;

            struct stat st{};
            if (::stat(child->path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            {
                child->isDir = true;
                child->dir = ::opendir(child->path.c_str());
            }
            else
            {
                child->fp = std::fopen(child->path.c_str(), "rb");
            }
            return File(child);
        }
    }

    int File::read(uint8_t *buf, size_t size) noexcept
    {
        if (!_impl)
            return -1;
        auto *im = static_cast<FileImpl *>(_impl);
        if (!im->fp)
            return -1;
        return static_cast<int>(std::fread(buf, 1, size, im->fp));
    }

    size_t File::write(const uint8_t *buf, size_t size) noexcept
    {
        if (!_impl)
            return 0;
        auto *im = static_cast<FileImpl *>(_impl);
        if (!im->fp)
            return 0;
        return std::fwrite(buf, 1, size, im->fp);
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
        if (!im->fp)
            return 0;
        const long cur = std::ftell(im->fp);
        std::fseek(im->fp, 0, SEEK_END);
        const long end = std::ftell(im->fp);
        std::fseek(im->fp, cur, SEEK_SET);
        return end > 0 ? static_cast<size_t>(end) : 0;
    }

    bool File::seek(size_t pos) noexcept
    {
        if (!_impl)
            return false;
        auto *im = static_cast<FileImpl *>(_impl);
        if (!im->fp)
            return false;
        return std::fseek(im->fp, static_cast<long>(pos), SEEK_SET) == 0;
    }

    void File::flush() noexcept
    {
        if (!_impl)
            return;
        auto *im = static_cast<FileImpl *>(_impl);
        if (im->fp)
            std::fflush(im->fp);
    }

    bool begin(bool formatOnFail) noexcept
    {

        size_t total = 0;
        size_t used = 0;
        if (esp_littlefs_info(CONFIG_PIPCORE_STORAGE_PARTITION_LABEL, &total, &used) == ESP_OK)
            return true;

        esp_vfs_littlefs_conf_t conf = {};
        conf.base_path = kMount;
        conf.partition_label = CONFIG_PIPCORE_STORAGE_PARTITION_LABEL;
        conf.format_if_mount_failed = formatOnFail;
        return esp_vfs_littlefs_register(&conf) == ESP_OK;
    }

    File open(const char *path) noexcept
    {
        const std::string full = join(path);
        struct stat st{};
        if (::stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        {
            auto *im = new FileImpl();
            im->path = full;
            im->isDir = true;
            im->dir = ::opendir(full.c_str());
            return File(im);
        }

        auto *im = new FileImpl();
        im->path = full;
        im->fp = std::fopen(full.c_str(), "rb");
        return File(im);
    }

    File open(const char *path, OpenMode mode) noexcept
    {
        const char *fmode = (mode == OpenMode::Write) ? "wb" : (mode == OpenMode::Append) ? "ab"
                                                                                          : "rb";
        auto *im = new FileImpl();
        im->path = join(path);
        im->fp = std::fopen(im->path.c_str(), fmode);
        return File(im);
    }

    bool remove(const char *path) noexcept
    {
        return ::unlink(join(path).c_str()) == 0;
    }

    bool rename(const char *from, const char *to) noexcept
    {
        return std::rename(join(from).c_str(), join(to).c_str()) == 0;
    }

    bool mkdir(const char *path) noexcept
    {
        return ::mkdir(join(path).c_str(), 0775) == 0;
    }

    bool exists(const char *path) noexcept
    {
        struct stat st{};
        return ::stat(join(path).c_str(), &st) == 0;
    }
}

#endif
