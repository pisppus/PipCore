#pragma once

#include <algorithm>
#include <cstring>

#include "Config.hpp"
#include "Core/Pixel.hpp"
#include <Display.hpp>
#include <Platform.hpp>
#include "Display/ST/Driver.hpp"

namespace pipcore::detail
{
    using pipcore::util::copySwap565;

    template <typename DriverType>
    class StDisplay : public pipcore::Display
    {
    public:
        StDisplay() = default;

        ~StDisplay() override { freeLineBuf(); }

        StDisplay(const StDisplay &) = delete;
        StDisplay &operator=(const StDisplay &) = delete;
        StDisplay(StDisplay &&) = delete;
        StDisplay &operator=(StDisplay &&) = delete;

        [[nodiscard]] bool begin(uint8_t rotation) noexcept override { return _drv.begin(rotation); }
        [[nodiscard]] bool setRotation(uint8_t rotation) noexcept override { return _drv.setRotation(rotation); }
        [[nodiscard]] uint16_t width() const noexcept override { return _drv.width(); }
        [[nodiscard]] uint16_t height() const noexcept override { return _drv.height(); }
        void reset() noexcept { _drv.reset(); }
        [[nodiscard]] auto lastError() const noexcept { return _drv.lastError(); }
        [[nodiscard]] const char *lastErrorText() const noexcept { return _drv.lastErrorText(); }
        [[nodiscard]] bool ioOk() const noexcept { return _drv.lastError() == DriverType::IoError::None; }

        void fillScreen565(uint16_t color565) noexcept override { (void)_drv.fillScreen565(color565, _drv.swapBytes()); }

        void waitDMA() noexcept override { (void)_drv.waitComplete(); }

        void writeRect565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels,
                          int32_t stridePixels) noexcept override;

        void writeRect565Async(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels,
                               int32_t stridePixels) noexcept override;

    protected:
        void freeLineBuf() noexcept
        {
            if (_lineBuf && _platform)
            {
                _platform->free(_lineBuf);
            }
            _lineBuf = nullptr;
            _lineBufCapPixels = 0;
        }

        [[nodiscard]] bool configureBase(pipcore::Platform *platform, typename DriverType::Transport *transport,
                                         uint16_t width, uint16_t height, uint8_t order, bool invert, bool swap,
                                         int16_t xOffset, int16_t yOffset) noexcept
        {
            if (!platform)
            {
                freeLineBuf();
                return false;
            }

            _platform = platform;
            freeLineBuf();
            _halfInFlight[0] = _halfInFlight[1] = false;

            constexpr size_t fixedCap = StageHalfPixels * 2;
            _lineBuf = static_cast<uint16_t *>(_platform->alloc(fixedCap * sizeof(uint16_t), AllocCaps::PreferInternal));

            _lineBufCapPixels = _lineBuf ? fixedCap : 0;

            const bool success = _drv.configure(transport, width, height, order, invert, swap, xOffset, yOffset);
            if (!success)
            {
                freeLineBuf();
                return false;
            }

            return true;
        }

    private:
        struct ClipResult
        {
            int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            int16_t cW = 0, cH = 0;
            const uint16_t *pixels = nullptr;
            bool visible = false;
        };

        [[nodiscard]] PIPCORE_HOT inline ClipResult clipRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                                             const uint16_t *pixels,
                                                             int32_t stridePixels) const noexcept
        {
            ClipResult res{};
            res.pixels = pixels;

            const int32_t dispW = _drv.width();
            const int32_t dispH = _drv.height();
            if (dispW <= 0 || dispH <= 0) [[unlikely]]
                return res;

            if (x >= 0 && y >= 0 && (x + w) <= dispW && (y + h) <= dispH)
            {
                res.x0 = x;
                res.y0 = y;
                res.x1 = static_cast<int16_t>(x + w - 1);
                res.y1 = static_cast<int16_t>(y + h - 1);
                res.cW = w;
                res.cH = h;
                res.visible = true;
            }
            else
            {
                const int32_t tx1 = static_cast<int32_t>(x) + w - 1;
                const int32_t ty1 = static_cast<int32_t>(y) + h - 1;
                if (tx1 < 0 || ty1 < 0 || x >= dispW || y >= dispH)
                    return res;

                res.x0 = std::max<int16_t>(x, 0);
                res.y0 = std::max<int16_t>(y, 0);
                res.x1 = static_cast<int16_t>(std::min<int32_t>(tx1, dispW - 1));
                res.y1 = static_cast<int16_t>(std::min<int32_t>(ty1, dispH - 1));

                res.cW = static_cast<int16_t>(res.x1 - res.x0 + 1);
                res.cH = static_cast<int16_t>(res.y1 - res.y0 + 1);
                res.visible = (res.cW > 0 && res.cH > 0);

                res.pixels +=
                    static_cast<size_t>(res.y0 - y) * static_cast<size_t>(stridePixels) + static_cast<size_t>(res.x0 - x);
            }
            return res;
        }

        PIPCORE_HOT void waitStageHalf(int idx) noexcept
        {
            if (_halfInFlight[idx])
            {
                (void)_drv.waitOldest();
                _halfInFlight[idx] = false;
            }
        }

        [[nodiscard]] PIPCORE_HOT inline bool submitPixels(const uint16_t *pixels, size_t count,
                                                           bool asyncMode) noexcept
        {
            return asyncMode ? _drv.writePixels565Async(pixels, count) : _drv.writePixels565(pixels, count);
        }

        PIPCORE_HOT void finishStream(bool asyncMode) noexcept
        {
            if (!asyncMode)
            {
                (void)_drv.waitComplete();
                _halfInFlight[0] = _halfInFlight[1] = false;
            }
        }

        PIPCORE_HOT bool streamRect(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels,
                                    int32_t stridePixels, bool asyncMode) noexcept
        {
            if (!pixels || w <= 0 || h <= 0 || stridePixels < w) [[unlikely]]
                return false;

            const auto clip = clipRect(x, y, w, h, pixels, stridePixels);
            if (!clip.visible)
                return true;

            if (!_drv.setAddrWindow(static_cast<uint16_t>(clip.x0), static_cast<uint16_t>(clip.y0),
                                    static_cast<uint16_t>(clip.x1), static_cast<uint16_t>(clip.y1)))
                return false;

            const bool swap = _drv.swapBytes();
            const size_t totalPixels = static_cast<size_t>(clip.cW) * static_cast<size_t>(clip.cH);
            uint16_t *const localLineBuf = _lineBuf;
            const size_t localLineBufCap = _lineBufCapPixels;
            bool ok = true;

            if ((clip.cH == 1 || stridePixels == clip.cW) && !swap)
            {
                ok = submitPixels(clip.pixels, totalPixels, asyncMode);
                finishStream(asyncMode);
                return ok;
            }

            if (stridePixels == clip.cW && localLineBuf)
            {
                const size_t halfCap = localLineBufCap >> 1;
                uint16_t *bufs[2] = {localLineBuf, localLineBuf + halfCap};
                int bufIdx = 0;

                size_t remaining = totalPixels;
                const uint16_t *srcPtr = clip.pixels;

                while (remaining > 0)
                {
                    const size_t chunk = std::min(remaining, halfCap);

                    waitStageHalf(bufIdx);

                    copySwap565(bufs[bufIdx], srcPtr, chunk);
                    if (!_drv.writePixels565Async(bufs[bufIdx], chunk))
                    {
                        finishStream(asyncMode);
                        return false;
                    }

                    _halfInFlight[bufIdx] = true;
                    srcPtr += chunk;
                    remaining -= chunk;
                    bufIdx ^= 1;
                }
                finishStream(asyncMode);
                return true;
            }

            if (localLineBuf && localLineBufCap >= static_cast<size_t>(clip.cW) * 2)
            {
                const size_t halfCap = localLineBufCap >> 1;
                uint16_t *bufs[2] = {localLineBuf, localLineBuf + halfCap};
                int bufIdx = 0;

                const size_t rowsPerBatch = std::max<size_t>(1, halfCap / static_cast<size_t>(clip.cW));
                int16_t yy = 0;

                while (yy < clip.cH)
                {
                    const int16_t batchRows =
                        static_cast<int16_t>(std::min<size_t>(rowsPerBatch, static_cast<size_t>(clip.cH - yy)));

                    waitStageHalf(bufIdx);

                    uint16_t *activeBuf = bufs[bufIdx];
                    size_t off = 0;

                    const uint16_t *row = clip.pixels + static_cast<size_t>(yy) * stridePixels;

                    if (!swap)
                    {
                        const size_t rowBytes = static_cast<size_t>(clip.cW) * sizeof(uint16_t);
                        int16_t rowIdx = 0;
                        for (; rowIdx + 4 <= batchRows; rowIdx += 4)
                        {
                            std::memcpy(activeBuf + off, row, rowBytes);
                            std::memcpy(activeBuf + off + clip.cW, row + stridePixels, rowBytes);
                            std::memcpy(activeBuf + off + clip.cW * 2, row + stridePixels * 2, rowBytes);
                            std::memcpy(activeBuf + off + clip.cW * 3, row + stridePixels * 3, rowBytes);
                            off += static_cast<size_t>(clip.cW) * 4;
                            row += stridePixels * 4;
                        }
                        for (; rowIdx < batchRows; ++rowIdx)
                        {
                            std::memcpy(activeBuf + off, row, rowBytes);
                            off += static_cast<size_t>(clip.cW);
                            row += stridePixels;
                        }
                    }
                    else
                    {
                        int16_t rowIdx = 0;
                        for (; rowIdx + 4 <= batchRows; rowIdx += 4)
                        {
                            copySwap565(activeBuf + off, row, clip.cW);
                            copySwap565(activeBuf + off + clip.cW, row + stridePixels, clip.cW);
                            copySwap565(activeBuf + off + clip.cW * 2, row + stridePixels * 2, clip.cW);
                            copySwap565(activeBuf + off + clip.cW * 3, row + stridePixels * 3, clip.cW);
                            off += static_cast<size_t>(clip.cW) * 4;
                            row += stridePixels * 4;
                        }
                        for (; rowIdx < batchRows; ++rowIdx)
                        {
                            copySwap565(activeBuf + off, row, static_cast<size_t>(clip.cW));
                            off += static_cast<size_t>(clip.cW);
                            row += stridePixels;
                        }
                    }

                    if (!_drv.writePixels565Async(activeBuf, off))
                    {
                        finishStream(asyncMode);
                        return false;
                    }
                    _halfInFlight[bufIdx] = true;

                    yy = static_cast<int16_t>(yy + batchRows);
                    bufIdx ^= 1;
                }
                finishStream(asyncMode);
                return true;
            }

            if (!swap)
            {
                const uint16_t *row = clip.pixels;
                for (int16_t yy = 0; yy < clip.cH; ++yy)
                {
                    if (!submitPixels(row, static_cast<size_t>(clip.cW), asyncMode))
                    {
                        ok = false;
                        break;
                    }
                    row += stridePixels;
                }
            }
            else
            {
                constexpr size_t StackBufPixels = 128;
                uint16_t temp[StackBufPixels];

                const uint16_t *row = clip.pixels;
                for (int16_t yy = 0; yy < clip.cH; ++yy)
                {
                    size_t remaining = static_cast<size_t>(clip.cW);
                    const uint16_t *srcPtr = row;
                    while (remaining > 0)
                    {
                        const size_t chunk = std::min<size_t>(remaining, StackBufPixels);
                        copySwap565(temp, srcPtr, chunk);
                        if (!_drv.writePixels565(temp, chunk))
                        {
                            ok = false;
                            break;
                        }
                        srcPtr += chunk;
                        remaining -= chunk;
                    }
                    if (!ok)
                        break;
                    row += stridePixels;
                }
            }
            finishStream(asyncMode);
            return ok;
        }

    protected:
        pipcore::Platform *_platform = nullptr;
        DriverType _drv;
        uint16_t *_lineBuf = nullptr;
        size_t _lineBufCapPixels = 0;
        bool _halfInFlight[2] = {};

    private:
        static inline constexpr size_t StageHalfPixels = 4096;
    };

    template <typename DriverType>
    PIPCORE_HOT void StDisplay<DriverType>::writeRect565(int16_t x, int16_t y, int16_t w, int16_t h,
                                                         const uint16_t *pixels, int32_t stridePixels) noexcept
    {
        (void)streamRect(x, y, w, h, pixels, stridePixels, false);
    }

    template <typename DriverType>
    PIPCORE_HOT void StDisplay<DriverType>::writeRect565Async(int16_t x, int16_t y, int16_t w, int16_t h,
                                                              const uint16_t *pixels,
                                                              int32_t stridePixels) noexcept
    {
        (void)streamRect(x, y, w, h, pixels, stridePixels, true);
    }
}
