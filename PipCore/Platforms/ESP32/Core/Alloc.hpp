#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace pipcore::debug
{

    inline portMUX_TYPE &trackerMux() noexcept
    {
        static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        return mux;
    }

    struct alignas(8) AllocHeader
    {
        AllocHeader *next = nullptr;
        AllocHeader *prev = nullptr;
        uint32_t size = 0;
        const char *tag = nullptr;
        void *caller = nullptr;
        uint32_t magic = 0;
    };

    static_assert(sizeof(AllocHeader) % 8 == 0);

    inline constexpr uint32_t kAllocMagic = 0xDEADBEEF;

    class Tracker
    {
    public:
        AllocHeader *_head = nullptr;
        std::atomic<uint32_t> _totalAllocated{0};
        uint32_t _peakAllocated = 0;

        std::atomic<bool> _dirty{true};

        static Tracker &instance() noexcept
        {
            static Tracker tracker;
            return tracker;
        }

        void lock() noexcept { taskENTER_CRITICAL(&trackerMux()); }

        void unlock() noexcept { taskEXIT_CRITICAL(&trackerMux()); }

        void *trackMalloc(size_t bytes, const char *tag, void *callerPC = nullptr) noexcept
        {
            if (bytes == 0) [[unlikely]]
                return nullptr;
            if (bytes > (SIZE_MAX >> 1)) [[unlikely]]
                return nullptr;

            const size_t totalSize = bytes + sizeof(AllocHeader);
            auto *hdr = static_cast<AllocHeader *>(std::malloc(totalSize));
            if (!hdr) [[unlikely]]
                return nullptr;

            hdr->size = static_cast<uint32_t>(bytes);
            hdr->tag = tag ? tag : "unknown";
            hdr->caller = callerPC;
            hdr->magic = kAllocMagic;

            taskENTER_CRITICAL(&trackerMux());
            hdr->next = _head;
            hdr->prev = nullptr;
            if (_head)
                _head->prev = hdr;
            _head = hdr;

            _totalAllocated.fetch_add(bytes, std::memory_order_relaxed);
            if (_totalAllocated.load(std::memory_order_relaxed) > _peakAllocated)
                _peakAllocated = _totalAllocated.load(std::memory_order_relaxed);
            _dirty.store(true, std::memory_order_relaxed);
            taskEXIT_CRITICAL(&trackerMux());

            return reinterpret_cast<void *>(hdr + 1);
        }

        void trackFree(void *ptr) noexcept
        {
            if (!ptr)
                return;

            auto *hdr = reinterpret_cast<AllocHeader *>(ptr) - 1;

            if (hdr->magic != kAllocMagic) [[unlikely]]
            {
                std::free(ptr);
                return;
            }

            taskENTER_CRITICAL(&trackerMux());
            if (hdr->prev)
                hdr->prev->next = hdr->next;
            if (hdr->next)
                hdr->next->prev = hdr->prev;
            if (_head == hdr)
                _head = hdr->next;

            _totalAllocated.fetch_sub(hdr->size, std::memory_order_relaxed);
            _dirty.store(true, std::memory_order_relaxed);
            taskEXIT_CRITICAL(&trackerMux());

            std::free(hdr);
        }
    };
}
