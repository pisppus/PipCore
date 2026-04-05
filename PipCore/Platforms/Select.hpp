#pragma once

#include <PipCore/Config/Features.hpp>
#include <PipCore/Platform.hpp>

#ifndef PIPCORE_PLATFORM
#error "Platform not selected. Define PIPCORE_PLATFORM in config.hpp"
#endif

#if defined(ESP32) && (PIPCORE_PLATFORM == ESP32)
#include <PipCore/Platforms/ESP32/Platform.hpp>
#else
#error "Unsupported PIPCORE_PLATFORM value for this target"
#endif

namespace pipcore
{
    using SelectedPlatform = esp32::Platform;

    [[nodiscard]] inline Platform *GetPlatform() noexcept
    {
        static SelectedPlatform instance;
        return &instance;
    }
}
