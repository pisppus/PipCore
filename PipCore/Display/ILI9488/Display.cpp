#include <algorithm>

#include "Config.hpp"
#if PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ILI9488

#include "Display/ILI9488/Display.hpp"
#include <Platform.hpp>

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

    PIPCORE_HOT void Display::convert565To666(const uint16_t *src, uint8_t *dst, size_t count) noexcept
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(src);
        while (count >= 4U)
        {
            uint8_t hi = bytes[0];
            uint8_t lo = bytes[1];
            dst[0] = static_cast<uint8_t>(hi & 0xF8U);
            dst[1] = static_cast<uint8_t>(((hi & 0x07U) << 5) | ((lo & 0xE0U) >> 3));
            dst[2] = static_cast<uint8_t>((lo << 3) & 0xF8U);

            hi = bytes[2];
            lo = bytes[3];
            dst[3] = static_cast<uint8_t>(hi & 0xF8U);
            dst[4] = static_cast<uint8_t>(((hi & 0x07U) << 5) | ((lo & 0xE0U) >> 3));
            dst[5] = static_cast<uint8_t>((lo << 3) & 0xF8U);

            hi = bytes[4];
            lo = bytes[5];
            dst[6] = static_cast<uint8_t>(hi & 0xF8U);
            dst[7] = static_cast<uint8_t>(((hi & 0x07U) << 5) | ((lo & 0xE0U) >> 3));
            dst[8] = static_cast<uint8_t>((lo << 3) & 0xF8U);

            hi = bytes[6];
            lo = bytes[7];
            dst[9] = static_cast<uint8_t>(hi & 0xF8U);
            dst[10] = static_cast<uint8_t>(((hi & 0x07U) << 5) | ((lo & 0xE0U) >> 3));
            dst[11] = static_cast<uint8_t>((lo << 3) & 0xF8U);

            bytes += 8;
            dst += 12;
            count -= 4U;
        }

        while (count--)
        {
            const uint8_t hi = *bytes++;
            const uint8_t lo = *bytes++;
            *dst++ = static_cast<uint8_t>(hi & 0xF8U);
            *dst++ = static_cast<uint8_t>(((hi & 0x07U) << 5) | ((lo & 0xE0U) >> 3));
            *dst++ = static_cast<uint8_t>((lo << 3) & 0xF8U);
        }
    }

    void Display::writeRect565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels, int32_t stridePixels) noexcept
    {
        (void)writeRectImpl(x, y, w, h, pixels, stridePixels);
        _drv.endWrite();
    }

    void Display::writeRect565Async(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels,
                                    int32_t stridePixels) noexcept
    {
        (void)writeRectImpl(x, y, w, h, pixels, stridePixels);
    }

    PIPCORE_HOT bool Display::writeRectImpl(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels,
                                            int32_t stridePixels) noexcept
    {
        if (!pixels || w <= 0 || h <= 0 || stridePixels < w) [[unlikely]]
            return false;

        const int32_t dispW = _drv.width();
        const int32_t dispH = _drv.height();
        if (dispW <= 0 || dispH <= 0) [[unlikely]]
            return false;

        int32_t x0 = x;
        int32_t y0 = y;
        int32_t x1 = static_cast<int32_t>(x) + w - 1;
        int32_t y1 = static_cast<int32_t>(y) + h - 1;
        if (x1 < 0 || y1 < 0 || x0 >= dispW || y0 >= dispH) [[unlikely]]
            return false;

        x0 = std::max<int32_t>(x0, 0);
        y0 = std::max<int32_t>(y0, 0);
        x1 = std::min<int32_t>(x1, dispW - 1);
        y1 = std::min<int32_t>(y1, dispH - 1);

        const int16_t cW = static_cast<int16_t>(x1 - x0 + 1);
        const int16_t cH = static_cast<int16_t>(y1 - y0 + 1);

        pixels += static_cast<size_t>(y0 - y) * static_cast<size_t>(stridePixels) + static_cast<size_t>(x0 - x);

        const size_t chunkBytes = _drv.preferredChunkBytes();
        if (chunkBytes < 3U) [[unlikely]]
            return false;

        const size_t chunkPixels = chunkBytes / 3U;
        if (!_drv.beginWriteWindow(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0), static_cast<uint16_t>(x1),
                                   static_cast<uint16_t>(y1)))
            return false;

        if (_drv.supportsDirectPixels())
        {
            if (cH == 1 || stridePixels == cW)
            {
                const size_t totalPixels = static_cast<size_t>(cW) * static_cast<size_t>(cH);
                size_t offset = 0;
                while (offset < totalPixels)
                {
                    size_t directCap = 0;
                    uint8_t *directBuf = _drv.directPixelsBuffer(directCap);
                    if (!directBuf || directCap < 3U)
                        return false;

                    const size_t pixelsNow = std::min(totalPixels - offset, directCap / 3U);
                    convert565To666(pixels + offset, directBuf, pixelsNow);
                    if (!_drv.submitDirectPixels(pixelsNow * 3U))
                        return false;
                    offset += pixelsNow;
                }
                return true;
            }

            int16_t yy = 0;
            while (yy < cH)
            {
                size_t directCap = 0;
                uint8_t *directBuf = _drv.directPixelsBuffer(directCap);
                if (!directBuf || directCap < 3U)
                    return false;

                const size_t directPixels = directCap / 3U;
                const size_t rowsPerBatch = std::max<size_t>(1U, directPixels / static_cast<size_t>(cW));
                const int16_t batchRows =
                    static_cast<int16_t>(std::min<size_t>(rowsPerBatch, static_cast<size_t>(cH - yy)));
                uint8_t *dst = directBuf;
                size_t pixelCount = 0;

                for (int16_t row = 0; row < batchRows; ++row)
                {
                    const uint16_t *rowPtr = pixels + static_cast<size_t>(yy + row) * static_cast<size_t>(stridePixels);
                    convert565To666(rowPtr, dst, static_cast<size_t>(cW));
                    dst += static_cast<size_t>(cW) * 3U;
                    pixelCount += static_cast<size_t>(cW);
                }

                if (!_drv.submitDirectPixels(pixelCount * 3U))
                    return false;
                yy = static_cast<int16_t>(yy + batchRows);
            }
            return true;
        }

        if (!ensureStageBuffer(chunkBytes))
            return false;

        if (cH == 1 || stridePixels == cW)
        {
            const size_t totalPixels = static_cast<size_t>(cW) * static_cast<size_t>(cH);
            size_t offset = 0;
            while (offset < totalPixels)
            {
                const size_t pixelsNow = std::min(chunkPixels, totalPixels - offset);
                convert565To666(pixels + offset, _stageBuf, pixelsNow);
                if (!_drv.writePixels666(_stageBuf, pixelsNow * 3U))
                    return false;
                offset += pixelsNow;
            }
            return true;
        }

        const size_t rowsPerBatch = std::max<size_t>(1U, chunkPixels / static_cast<size_t>(cW));
        int16_t yy = 0;
        while (yy < cH)
        {
            const int16_t batchRows = static_cast<int16_t>(std::min<size_t>(rowsPerBatch, static_cast<size_t>(cH - yy)));
            uint8_t *dst = _stageBuf;
            size_t pixelCount = 0;

            for (int16_t row = 0; row < batchRows; ++row)
            {
                const uint16_t *rowPtr = pixels + static_cast<size_t>(yy + row) * static_cast<size_t>(stridePixels);
                convert565To666(rowPtr, dst, static_cast<size_t>(cW));
                dst += static_cast<size_t>(cW) * 3U;
                pixelCount += static_cast<size_t>(cW);
            }

            if (!_drv.writePixels666(_stageBuf, pixelCount * 3U))
                return false;
            yy = static_cast<int16_t>(yy + batchRows);
        }
        return true;
    }

    void Display::waitDMA() noexcept
    {
        _drv.endWrite();
    }
}

#endif
