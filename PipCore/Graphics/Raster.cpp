#include <Sprite.hpp>
#include "Core/Pixel.hpp"

namespace pipcore
{
    PIPCORE_HOT void Sprite::fillScreen(uint16_t color565) noexcept
    {
        if (!_buf || _w <= 0 || _h <= 0)
            return;

        pipcore::util::fillSwap565(_buf, static_cast<size_t>(_w) * static_cast<size_t>(_h), color565);
    }

    PIPCORE_HOT void Sprite::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color565) noexcept
    {
        if (!_buf)
            return;

        const auto clip = clipRegion(x, y, w, h);
        if (!clip.visible)
            return;

        uint16_t *ptr = _buf + clip.ry1 * _w + clip.rx1;

        if (clip.cw == _w)
        {
            pipcore::util::fillSwap565(ptr, static_cast<size_t>(clip.cw) * static_cast<size_t>(clip.ch), color565);
            return;
        }

        int16_t lines = clip.ch;
        while (lines--)
        {
            pipcore::util::fillSwap565(ptr, static_cast<size_t>(clip.cw), color565);
            ptr += _w;
        }
    }

    PIPCORE_HOT void Sprite::pushImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *__restrict pixels565) noexcept
    {
        if (!_buf || !pixels565 || _clipW <= 0 || _clipH <= 0)
            return;

        const auto clip = clipRegion(x, y, w, h);
        if (!clip.visible)
            return;

        if (clip.cw == w && clip.rx1 == 0 && _w == w)
        {
            pipcore::util::copySwap565(_buf + static_cast<size_t>(clip.ry1) * _w,
                                       pixels565 + static_cast<size_t>(clip.ry1 - y) * w,
                                       static_cast<size_t>(clip.cw) * static_cast<size_t>(clip.ch));
            return;
        }

        const uint16_t *srcLine = pixels565 + (size_t)(clip.ry1 - y) * w + (clip.rx1 - x);
        uint16_t *dstLine = _buf + (size_t)clip.ry1 * _w + clip.rx1;

        int16_t lines = clip.ch;
        while (lines--)
        {
            pipcore::util::copySwap565(dstLine, srcLine, static_cast<size_t>(clip.cw));
            srcLine += w;
            dstLine += _w;
        }
    }
}
