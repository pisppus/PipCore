#pragma once

namespace pipcore::storage::detail
{
    [[nodiscard]] inline bool isSafePath(const char *path) noexcept
    {
        if (!path || !path[0])
            return false;
        const char *p = path;
        while (*p)
        {
            while (*p == '/')
                ++p;
            const char *start = p;
            while (*p && *p != '/')
                ++p;
            const size_t len = static_cast<size_t>(p - start);
            if (len == 2 && start[0] == '.' && start[1] == '.')
                return false;
        }
        return true;
    }
}
