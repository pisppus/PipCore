#pragma once

#if __has_include(<config.hpp>)
#include <config.hpp>
#endif

#include <pipCore/Display.hpp>

#define PIPCORE_PP_CAT_IMPL(a, b) a##b
#define PIPCORE_PP_CAT(a, b) PIPCORE_PP_CAT_IMPL(a, b)

#define PIPCORE_DISPLAY_TAG_ST7789 1
#define PIPCORE_DISPLAY_TAG_ILI9488 2
#define PIPCORE_DISPLAY_ID(name) PIPCORE_PP_CAT(PIPCORE_DISPLAY_TAG_, name)

#ifndef PIPCORE_DISPLAY
#define PIPCORE_DISPLAY ST7789
#endif

#ifndef PIPCORE_DISPLAY
#error "Display not selected. Define PIPCORE_DISPLAY in config.hpp"
#endif

#if PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ST7789
#include <pipCore/Displays/ST7789/Display.hpp>
#elif PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ILI9488
#include <pipCore/Displays/ILI9488/Display.hpp>
#endif

namespace pipcore
{
#if PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ST7789
    using SelectedDisplay = st7789::Display;
#elif PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ILI9488
    using SelectedDisplay = ili9488::Display;
#else
#error "Unsupported PIPCORE_DISPLAY value"
#endif
}

#undef PIPCORE_DISPLAY_ID
#undef PIPCORE_DISPLAY_TAG_ST7789
#undef PIPCORE_DISPLAY_TAG_ILI9488
#undef PIPCORE_PP_CAT
#undef PIPCORE_PP_CAT_IMPL
