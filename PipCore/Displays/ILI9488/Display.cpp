#include <pipCore/Displays/ILI9488/Display.hpp>
#include <pipCore/Platform.hpp>
#include <algorithm>

namespace pipcore::ili9488
{
    Display::~Display()
    {
        if (_stageBuf && _platform)
        {
            _platform->free(_stageBuf);
            _stageBuf = nullptr;
            _stageBufBytes = 0;
        }
    }

    bool Display::ensureStageBuffer(size_t bytes)
    {
        if (_stageBufBytes >= bytes)
            return true;

        uint8_t *newBuf = _platform ? static_cast<uint8_t *>(_platform->alloc(bytes, AllocCaps::PreferInternal)) : nullptr;
        if (!newBuf)
            return false;
        if (_stageBuf)
            _platform->free(_stageBuf);
        _stageBuf = newBuf;
        _stageBufBytes = bytes;
        return true;
    }

    void Display::convert565To666(const uint16_t *src, uint8_t *dst, size_t count) noexcept
    {
        for (size_t i = 0; i < count; ++i)
        {
            const uint16_t c = src[i];
            *dst++ = static_cast<uint8_t>((c >> 8) & 0xF8);
            *dst++ = static_cast<uint8_t>((c >> 3) & 0xFC);
            *dst++ = static_cast<uint8_t>((c << 3) & 0xF8);
        }
    }

    void Display::writeRect565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels, int32_t stridePixels)
    {
        if (!pixels || w <= 0 || h <= 0 || stridePixels < w)
            return;

        const int32_t dispW = _drv.width();
        const int32_t dispH = _drv.height();
        if (dispW <= 0 || dispH <= 0)
            return;

        int32_t x0 = x;
        int32_t y0 = y;
        int32_t x1 = static_cast<int32_t>(x) + w - 1;
        int32_t y1 = static_cast<int32_t>(y) + h - 1;
        if (x1 < 0 || y1 < 0 || x0 >= dispW || y0 >= dispH)
            return;

        x0 = std::max<int32_t>(x0, 0);
        y0 = std::max<int32_t>(y0, 0);
        x1 = std::min<int32_t>(x1, dispW - 1);
        y1 = std::min<int32_t>(y1, dispH - 1);

        const int16_t cW = static_cast<int16_t>(x1 - x0 + 1);
        const int16_t cH = static_cast<int16_t>(y1 - y0 + 1);

        pixels += static_cast<size_t>(y0 - y) * static_cast<size_t>(stridePixels) + static_cast<size_t>(x0 - x);

        if (!_drv.setAddrWindow(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0), static_cast<uint16_t>(x1), static_cast<uint16_t>(y1)))
            return;

        const size_t chunkBytes = _drv.preferredChunkBytes();
        if (chunkBytes < 3U || !ensureStageBuffer(chunkBytes))
            return;

        const size_t chunkPixels = chunkBytes / 3U;
        for (int16_t row = 0; row < cH; ++row)
        {
            const uint16_t *rowPtr = pixels + static_cast<size_t>(row) * static_cast<size_t>(stridePixels);
            size_t offset = 0;
            const size_t rowPixels = static_cast<size_t>(cW);

            while (offset < rowPixels)
            {
                const size_t pixelsNow = std::min(chunkPixels, rowPixels - offset);
                convert565To666(rowPtr + offset, _stageBuf, pixelsNow);
                if (!_drv.writePixels666(_stageBuf, pixelsNow * 3U))
                    return;
                offset += pixelsNow;
            }
        }
    }
}
