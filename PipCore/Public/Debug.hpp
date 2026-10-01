#pragma once

#include <cstdint>

#include "Config.hpp"

namespace pipcore::debug
{
    [[nodiscard]] uint64_t profileCycles() noexcept;

#if PIPCORE_ENABLE_DEBUG
    struct AllocStats
    {
        uint32_t currentBytes = 0;
        uint32_t peakBytes = 0;
    };

    [[nodiscard]] AllocStats allocStats() noexcept;
#endif

    struct ProfileNode
    {
        const char *name = nullptr;
        ProfileNode *next = nullptr;
        ProfileNode *parent = nullptr;
        uint64_t totalCycles = 0;
        uint64_t selfCycles = 0;
        uint32_t callCount = 0;
        uint32_t maxCycles = 0;
        bool registered = false;
    };

    namespace detail
    {
        void profilerLock() noexcept;
        void profilerUnlock() noexcept;
    }

    class Profiler
    {
    public:
        ProfileNode *_head = nullptr;
        ProfileNode *_current = nullptr;

        static Profiler &instance() noexcept
        {
            static Profiler prof;
            return prof;
        }

        void lock() noexcept { detail::profilerLock(); }

        void unlock() noexcept { detail::profilerUnlock(); }

        void enterNode(ProfileNode *node, const char *name) noexcept
        {
            lock();
            if (!node->registered)
            {
                node->name = name;
                node->next = _head;
                _head = node;
                node->registered = true;
            }
            node->parent = _current;
            node->callCount++;
            _current = node;
            unlock();
        }

        void exitNode(ProfileNode *node, uint64_t startCycles) noexcept
        {
            const uint64_t endCycles = profileCycles();
            const uint64_t elapsed =
                (endCycles >= startCycles) ? (endCycles - startCycles) : (0xFFFFFFFFu - startCycles + endCycles);

            lock();
            node->totalCycles += elapsed;
            if (elapsed > node->maxCycles)
            {
                node->maxCycles = static_cast<uint32_t>(elapsed);
            }
            _current = node->parent;
            unlock();
        }

        void calculateSelfCycles() noexcept
        {
            lock();
            for (ProfileNode *curr = _head; curr != nullptr; curr = curr->next)
            {
                uint64_t childCycles = 0;
                for (ProfileNode *child = _head; child != nullptr; child = child->next)
                {
                    if (child->parent == curr)
                    {
                        childCycles += child->totalCycles;
                    }
                }
                curr->selfCycles = (curr->totalCycles >= childCycles) ? (curr->totalCycles - childCycles) : 0;
            }
            unlock();
        }

        void clear() noexcept
        {
            lock();
            for (ProfileNode *curr = _head; curr != nullptr; curr = curr->next)
            {
                curr->totalCycles = 0;
                curr->selfCycles = 0;
                curr->callCount = 0;
                curr->maxCycles = 0;
            }
            _current = nullptr;
            unlock();
        }
    };

    class ScopeZone
    {
    public:
        ScopeZone(ProfileNode *node, const char *name) noexcept : _node(node)
        {
            _startCycles = profileCycles();
            Profiler::instance().enterNode(node, name);
        }
        ~ScopeZone() noexcept { Profiler::instance().exitNode(_node, _startCycles); }

    private:
        ProfileNode *_node;
        uint64_t _startCycles;
    };

}

#if PIPCORE_ENABLE_DEBUG

#define PIP_PROFILE_ZONE(name)                                               \
    static ::pipcore::debug::ProfileNode PIPCORE_PP_CAT(pipNode_, __LINE__); \
    ::pipcore::debug::ScopeZone PIPCORE_PP_CAT(pipZone_, __LINE__)(&PIPCORE_PP_CAT(pipNode_, __LINE__), name)

#define PIP_PROFILE_FUNCTION() PIP_PROFILE_ZONE(__PRETTY_FUNCTION__)

#else

#define PIP_PROFILE_ZONE(name) ((void)0)
#define PIP_PROFILE_FUNCTION() ((void)0)

#endif
