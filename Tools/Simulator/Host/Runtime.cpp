#include "Host/Runtime.hpp"
#if PIPCORE_TARGET_DESKTOP
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <new>
#include <thread>
#include <utility>
#include <vector>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#else
#include <csignal>
#include <unistd.h>
#endif

#include "Host/Windows/WindowWin32.hpp"
#if PIPCORE_ENABLE_TOUCH
#include "Host/Input/Touch.hpp"
#endif

namespace pipcore::desktop
{
    namespace
    {
        constexpr uint8_t kPrevPin =
#ifdef PIPSIM_BTN_PREV_PIN
            static_cast<uint8_t>(PIPSIM_BTN_PREV_PIN);
#else
            4U;
#endif
        constexpr uint8_t kNextPin =
#ifdef PIPSIM_BTN_NEXT_PIN
            static_cast<uint8_t>(PIPSIM_BTN_NEXT_PIN);
#else
            20U;
#endif
        constexpr uint8_t kSelectPin =
#ifdef PIPSIM_BTN_SELECT_PIN
            static_cast<uint8_t>(PIPSIM_BTN_SELECT_PIN);
#else
            21U;
#endif

        constexpr uint64_t kFrameHistoryMaxBytes = 48ULL * 1024ULL * 1024ULL;
        constexpr size_t kFrameHistoryMaxFrames = 120U;
        constexpr size_t kConsoleMaxPending = 4096U;
        constexpr size_t kConsoleLineCap = 512U;

        [[nodiscard]] uint64_t monotonicMicros() noexcept
        {
            using clock = std::chrono::steady_clock;
            static const clock::time_point start = clock::now();
            const auto delta = std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start);
            return static_cast<uint64_t>(delta.count());
        }

        inline constexpr std::array<uint8_t, 32> kExpand5 = []
        {
            std::array<uint8_t, 32> t{};
            for (uint32_t i = 0; i < 32; ++i)
                t[i] = static_cast<uint8_t>((i * 255U + 15U) / 31U);
            return t;
        }();

        inline constexpr std::array<uint8_t, 64> kExpand6 = []
        {
            std::array<uint8_t, 64> t{};
            for (uint32_t i = 0; i < 64; ++i)
                t[i] = static_cast<uint8_t>((i * 255U + 31U) / 63U);
            return t;
        }();

        [[nodiscard]] std::string shellQuote(const std::string &text)
        {
            std::string out = "'";
            for (char ch : text)
                out += (ch == '\'') ? "'\\''" : std::string(1, ch);
            out += "'";
            return out;
        }

        [[nodiscard]] std::string readIniValue(const std::filesystem::path &file, const char *key) noexcept
        {
            try
            {
                std::ifstream in(file, std::ios::binary);
                if (!in)
                    return {};
                const std::string prefix = std::string(key) + "=";
                std::string line;
                while (std::getline(in, line))
                {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line.rfind(prefix, 0) != 0)
                        continue;
                    std::string value = line.substr(prefix.size());
                    const auto first = value.find_first_not_of(" \t");
                    if (first == std::string::npos)
                        return {};
                    const auto last = value.find_last_not_of(" \t");
                    return value.substr(first, last - first + 1);
                }
            }
            catch (...)
            {
            }
            return {};
        }

        [[nodiscard]] std::string normalizePath(const char *raw) noexcept
        {
            std::string path = raw ? raw : "";
            const auto first = path.find_first_not_of(" \t\"");
            if (first == std::string::npos)
                return {};
            const auto last = path.find_last_not_of(" \t\"");
            path = path.substr(first, last - first + 1);
            for (char &ch : path)
            {
                if (ch == '\\')
                    ch = '/';
            }
            while (path.size() > 1 && path.back() == '/')
            {
                if (path.size() == 3 && path[1] == ':')
                    break;
                path.pop_back();
            }
            return path;
        }

        [[nodiscard]] std::string deriveKernelDir(const std::string &projectRoot) noexcept
        {
            return projectRoot.empty() ? std::string() : projectRoot + "/PipCore";
        }

        [[nodiscard]] std::filesystem::path simWorkDir() noexcept
        {
            if (const char *workDir = std::getenv("PIPSIM_WORKDIR"))
            {
                if (*workDir)
                    return std::filesystem::path(workDir);
            }
            return std::filesystem::current_path();
        }

        [[nodiscard]] std::filesystem::path timestampedPath(const char *dirName, const char *ext)
        {
            namespace fs = std::filesystem;
            fs::path dir = simWorkDir() / dirName;
            std::error_code ec;
            fs::create_directories(dir, ec);

            const auto now = std::chrono::system_clock::now();
            const std::time_t tt = std::chrono::system_clock::to_time_t(now);
            std::tm tm = {};
#if defined(_WIN32)
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

            char name[128];
            std::snprintf(name, sizeof(name), "sim_%04d%02d%02d_%02d%02d%02d_%03lld.%s", tm.tm_year + 1900, tm.tm_mon + 1,
                          tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long long>(ms), ext);
            return dir / name;
        }

        [[nodiscard]] std::filesystem::path controlPath(const char *name)
        {
            return simWorkDir() / name;
        }

        void writeControlFile(const char *name) noexcept
        {
            try
            {
                std::ofstream out(controlPath(name), std::ios::binary | std::ios::trunc);
                out << "1\n";
            }
            catch (...)
            {
            }
        }

        constexpr uint32_t kCrc32Poly = 0xEDB88320U;

        constexpr std::array<uint32_t, 256> makeCrc32Table()
        {
            std::array<uint32_t, 256> table{};
            for (uint32_t n = 0; n < 256; ++n)
            {
                uint32_t c = n;
                for (int bit = 0; bit < 8; ++bit)
                    c = (c & 1U) ? (kCrc32Poly ^ (c >> 1)) : (c >> 1);
                table[n] = c;
            }
            return table;
        }
        constexpr auto kCrc32Table = makeCrc32Table();

        uint32_t pngCrc32(const uint8_t *data, size_t len, uint32_t seed = 0xFFFFFFFFU) noexcept
        {
            uint32_t crc = seed;
            for (size_t i = 0; i < len; ++i)
                crc = kCrc32Table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
            return crc;
        }

        uint32_t pngAdler32(const uint8_t *data, size_t len) noexcept
        {
            uint32_t a = 1U;
            uint32_t b = 0U;
            for (size_t i = 0; i < len; ++i)
            {
                a = (a + data[i]) % 65521U;
                b = (b + a) % 65521U;
            }
            return (b << 16U) | a;
        }

        void appendBe32(std::vector<uint8_t> &out, uint32_t value) noexcept
        {
            out.push_back(static_cast<uint8_t>(value >> 24U));
            out.push_back(static_cast<uint8_t>(value >> 16U));
            out.push_back(static_cast<uint8_t>(value >> 8U));
            out.push_back(static_cast<uint8_t>(value));
        }

        void appendBe16(std::vector<uint8_t> &out, uint32_t value) noexcept
        {
            out.push_back(static_cast<uint8_t>(value >> 8U));
            out.push_back(static_cast<uint8_t>(value));
        }

        [[nodiscard]] bool writePngRgba(const std::filesystem::path &path, uint16_t width, uint16_t height,
                                        const uint32_t *argb) noexcept
        {
            try
            {

                std::vector<uint8_t> raw;
                raw.reserve(static_cast<size_t>(height) * (1U + 4U * static_cast<size_t>(width)));
                for (uint16_t y = 0; y < height; ++y)
                {
                    raw.push_back(0U);
                    for (uint16_t x = 0; x < width; ++x)
                    {
                        const uint32_t c = argb[static_cast<size_t>(y) * width + x];
                        raw.push_back(static_cast<uint8_t>((c >> 16) & 0xFFU));
                        raw.push_back(static_cast<uint8_t>((c >> 8) & 0xFFU));
                        raw.push_back(static_cast<uint8_t>(c & 0xFFU));
                        raw.push_back(static_cast<uint8_t>((c >> 24) & 0xFFU));
                    }
                }

                std::vector<uint8_t> idat;
                idat.push_back(0x78U);
                idat.push_back(0x01U);
                size_t offset = 0;
                while (offset < raw.size())
                {
                    const size_t block = std::min<size_t>(65535U, raw.size() - offset);
                    idat.push_back(offset + block >= raw.size() ? 1U : 0U);
                    appendBe16(idat, static_cast<uint32_t>(block));
                    appendBe16(idat, static_cast<uint32_t>(block ^ 0xFFFFU));
                    idat.insert(idat.end(), raw.begin() + static_cast<long>(offset),
                                raw.begin() + static_cast<long>(offset + block));
                    offset += block;
                }
                appendBe32(idat, pngAdler32(raw.data(), raw.size()));

                std::vector<uint8_t> ihdr;
                appendBe32(ihdr, width);
                appendBe32(ihdr, height);
                ihdr.push_back(8U);
                ihdr.push_back(6U);
                ihdr.push_back(0U);
                ihdr.push_back(0U);
                ihdr.push_back(0U);

                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                if (!file)
                    return false;

                constexpr uint8_t signature[8] = {0x89U, 'P', 'N', 'G', '\r', '\n', 0x1AU, '\n'};
                file.write(reinterpret_cast<const char *>(signature), 8);

                auto writeChunk = [&file](const char *type, const std::vector<uint8_t> &payload)
                {
                    const uint32_t length = static_cast<uint32_t>(payload.size());
                    uint8_t header[4] = {static_cast<uint8_t>(length >> 24), static_cast<uint8_t>(length >> 16),
                                         static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)};
                    file.write(reinterpret_cast<const char *>(header), 4);
                    file.write(type, 4);
                    if (!payload.empty())
                        file.write(reinterpret_cast<const char *>(payload.data()),
                                   static_cast<std::streamsize>(payload.size()));

                    uint32_t crc = pngCrc32(reinterpret_cast<const uint8_t *>(type), 4);
                    if (!payload.empty())
                        crc = pngCrc32(payload.data(), payload.size(), crc);
                    const uint8_t crcBe[4] = {static_cast<uint8_t>(crc >> 24), static_cast<uint8_t>(crc >> 16),
                                              static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc)};
                    file.write(reinterpret_cast<const char *>(crcBe), 4);
                };

                writeChunk("IHDR", ihdr);
                writeChunk("IDAT", idat);
                writeChunk("IEND", {});
                return static_cast<bool>(file);
            }
            catch (...)
            {
                return false;
            }
        }

#if defined(_WIN32)

        struct MfVideoWriter
        {
            IMFSinkWriter *writer = nullptr;
            DWORD streamIndex = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            LONGLONG frameDuration = 0;
            LONGLONG frameIndex = 0;

            [[nodiscard]] HRESULT start(const wchar_t *path, uint32_t fbWidth, uint32_t fbHeight, uint32_t fps) noexcept
            {
                width = fbWidth & ~1U;
                height = fbHeight & ~1U;
                frameDuration = 10'000'000LL / static_cast<LONGLONG>(fps);

                HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
                if (FAILED(hr))
                    return hr;
                hr = MFCreateSinkWriterFromURL(path, nullptr, nullptr, &writer);
                if (FAILED(hr))
                {
                    MFShutdown();
                    return hr;
                }

                IMFMediaType *outType = nullptr;
                IMFMediaType *inType = nullptr;
                do
                {
                    hr = MFCreateMediaType(&outType);
                    if (FAILED(hr))
                        break;
                    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                    outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
                    outType->SetUINT32(MF_MT_AVG_BITRATE, 2'500'000U);
                    outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                    MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, width, height);
                    MFSetAttributeRatio(outType, MF_MT_FRAME_RATE, fps, 1U);
                    MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO, 1U, 1U);

                    hr = writer->AddStream(outType, &streamIndex);
                    if (FAILED(hr))
                        break;

                    hr = MFCreateMediaType(&inType);
                    if (FAILED(hr))
                        break;
                    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
                    MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, width, height);
                    MFSetAttributeRatio(inType, MF_MT_FRAME_RATE, fps, 1U);
                    inType->SetUINT32(MF_MT_DEFAULT_STRIDE, width * 4U);

                    hr = writer->SetInputMediaType(streamIndex, inType, nullptr);
                    if (FAILED(hr))
                        break;
                    hr = writer->BeginWriting();
                } while (false);

                if (outType)
                    outType->Release();
                if (inType)
                    inType->Release();
                if (FAILED(hr))
                {
                    writer->Release();
                    writer = nullptr;
                    MFShutdown();
                }
                return hr;
            }

            [[nodiscard]] HRESULT writeFrame(const uint32_t *argb, uint32_t srcWidth) noexcept
            {
                if (!writer)
                    return E_POINTER;
                IMFMediaBuffer *buffer = nullptr;
                HRESULT hr = MFCreateMemoryBuffer(width * height * 4U, &buffer);
                if (FAILED(hr))
                    return hr;
                BYTE *dst = nullptr;
                hr = buffer->Lock(&dst, nullptr, nullptr);
                if (SUCCEEDED(hr))
                {

                    for (uint32_t row = 0; row < height; ++row)
                        std::memcpy(dst + static_cast<size_t>(row) * width * 4U,
                                    argb + static_cast<size_t>(row) * srcWidth, width * 4U);
                    buffer->Unlock();
                    hr = buffer->SetCurrentLength(width * height * 4U);
                }
                if (SUCCEEDED(hr))
                {
                    IMFSample *sample = nullptr;
                    hr = MFCreateSample(&sample);
                    if (SUCCEEDED(hr))
                    {
                        sample->AddBuffer(buffer);
                        sample->SetSampleTime(frameIndex * frameDuration);
                        sample->SetSampleDuration(frameDuration);
                        hr = writer->WriteSample(streamIndex, sample);
                        sample->Release();
                        ++frameIndex;
                    }
                }
                buffer->Release();
                return hr;
            }

            void stop() noexcept
            {
                if (writer)
                {
                    writer->Finalize();
                    writer->Release();
                    writer = nullptr;
                }
                MFShutdown();
            }
        };
#endif
    }

    Runtime &Runtime::instance() noexcept
    {
        static Runtime runtime;
        return runtime;
    }

    Runtime::Runtime() noexcept
    {
        _scale =
#ifdef PIPSIM_SCALE
            static_cast<uint8_t>(PIPSIM_SCALE);
#else
            1U;
#endif
        loadSettings();
        refreshTitle();
    }

    bool Runtime::configureDisplay(uint16_t width, uint16_t height) noexcept
    {
        if (width == 0 || height == 0)
            return false;
        _baseWidth = width;
        _baseHeight = height;
        return setDisplayRotation(_rotation);
    }

    bool Runtime::beginDisplay(uint8_t rotation) noexcept
    {
        return setDisplayRotation(rotation & 3U) && ensureWindow();
    }

    bool Runtime::setDisplayRotation(uint8_t rotation) noexcept
    {
        if (_baseWidth == 0 || _baseHeight == 0)
            return false;
        _rotation = rotation & 3U;
        const bool quarterTurn = ((_rotation & 1U) != 0U);
        _width = quarterTurn ? _baseHeight : _baseWidth;
        _height = quarterTurn ? _baseWidth : _baseHeight;
        _framebuffer.assign(static_cast<size_t>(_width) * static_cast<size_t>(_height), 0xFF000000u);
        _frameHistory.clear();
        _frameHistoryCursor = 0;
        _historyBrowsing = false;
        _lastRenderCpuUs = 0;
        resizeWindow();
        requestPresent();
        return true;
    }

    void Runtime::fillScreen565(uint16_t color565) noexcept
    {
        if (_framebuffer.empty())
            return;
        if (_frameCpuStartUs == 0)
            _frameCpuStartUs = monotonicMicros();
        std::fill(_framebuffer.begin(), _framebuffer.end(), color565ToArgb(color565));
        requestPresent();
    }

    void Runtime::writeRect565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels,
                               int32_t stridePixels) noexcept
    {
        if (!pixels || w <= 0 || h <= 0 || stridePixels <= 0 || _framebuffer.empty())
            return;
        const int32_t dstX1 = std::max<int32_t>(0, x);
        const int32_t dstY1 = std::max<int32_t>(0, y);
        const int32_t dstX2 = std::min<int32_t>(_width, static_cast<int32_t>(x) + w);
        const int32_t dstY2 = std::min<int32_t>(_height, static_cast<int32_t>(y) + h);
        if (dstX1 >= dstX2 || dstY1 >= dstY2)
            return;

        const size_t pixelCount = static_cast<size_t>(dstX2 - dstX1) * static_cast<size_t>(dstY2 - dstY1);
        if (_frameCpuStartUs == 0)
            _frameCpuStartUs = monotonicMicros();

        const int32_t srcOffsetX = dstX1 - x;
        const int32_t srcOffsetY = dstY1 - y;
        for (int32_t row = dstY1; row < dstY2; ++row)
        {
            uint32_t *dst = _framebuffer.data() + static_cast<size_t>(row) * _width + dstX1;
            const uint16_t *src =
                pixels + static_cast<size_t>(srcOffsetY + (row - dstY1)) * static_cast<size_t>(stridePixels) + srcOffsetX;
            for (int32_t col = dstX1; col < dstX2; ++col)
                *dst++ = color565ToArgb(__builtin_bswap16(*src++));
        }
        requestPresent();
    }

    void Runtime::pumpEvents() noexcept
    {
        if (!_window && _baseWidth > 0 && _baseHeight > 0)
            (void)ensureWindow();

        if (_window)
            _window->pumpEvents();

        const uint64_t nowUs = monotonicMicros();
        serviceSimClock(nowUs);
        serviceRecording(nowUs);
        const uint64_t minIntervalUs = _fpsLimit ? (1'000'000ULL / _fpsLimit) : 0ULL;
        if (_dirty && (nowUs - _lastPresentUs) >= minIntervalUs)
            presentNow();
    }

    void Runtime::pinModeInput(uint8_t pin, pipcore::InputMode mode) noexcept
    {
        _pinModes[pin] = mode;
    }

    bool Runtime::digitalRead(uint8_t pin) const noexcept
    {
        const bool pressed = pinPressed(pin);
        if (_pinModes[pin] == pipcore::InputMode::Pullup)
            return !pressed;
        return pressed;
    }

    int16_t Runtime::analogRead(uint8_t pin) const noexcept
    {
        if (pin == 34)
        {
            if (_prevDown)
                return 0;
            if (_nextDown)
                return 4095;
            return 2048;
        }
        if (pin == 35)
        {
            if (_upDown)
                return 0;
            if (_downDown)
                return 4095;
            return 2048;
        }
        return 0;
    }

    uint32_t Runtime::nowMs() noexcept
    {
        return static_cast<uint32_t>(_simClockUs / 1000U);
    }

    uint64_t Runtime::nowMicros() noexcept
    {
        return _simClockUs;
    }

    void Runtime::delayMs(uint32_t ms) noexcept
    {
        const uint64_t endUs = _simClockUs + static_cast<uint64_t>(ms) * 1000U;
        while (!shouldQuit() && _simClockUs < endUs)
        {
            pumpEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void Runtime::serviceSimClock(uint64_t realNowUs) noexcept
    {
        if (_lastRealClockUs == 0)
        {
            _lastRealClockUs = realNowUs;
            return;
        }
        const uint64_t realDelta = realNowUs - _lastRealClockUs;
        _lastRealClockUs = realNowUs;
        if (_pendingFrameSteps != 0)
        {
            --_pendingFrameSteps;
            _simClockUs += 16'667U;
            return;
        }
        if (!_paused)
            _simClockUs += (realDelta * _timeScalePercent) / 100U;
    }

    void Runtime::markUserExit() noexcept
    {
        writeControlFile("_sim-user-exit");
    }

    void Runtime::advanceFrameStep() noexcept
    {
        _pendingFrameSteps += _frameStepCount;
    }

    void Runtime::stepFrame() noexcept
    {
        _paused = true;
        _historyBrowsing = false;
        advanceFrameStep();
        syncWindowControls();
    }

    void Runtime::setFpsLimit(uint32_t fps) noexcept
    {
        _fpsLimit = fps;
        saveSettings();
    }

    void Runtime::setPaused(bool paused) noexcept
    {
        _paused = paused;
        syncWindowControls();
    }

    void Runtime::stepBack() noexcept
    {
        if (_frameHistory.empty())
            return;
        _paused = true;
        if (!_historyBrowsing)
        {
            _frameHistoryCursor = _frameHistory.size() - 1U;
            _historyBrowsing = true;
        }
        const size_t step = std::min<size_t>(_frameStepCount, _frameHistoryCursor);
        _frameHistoryCursor -= step;
        if (_frameHistory[_frameHistoryCursor].size() == _framebuffer.size())
            _framebuffer = _frameHistory[_frameHistoryCursor];
        requestPresent();
        presentNow();
        syncWindowControls();
    }

    bool Runtime::ensureWindow() noexcept
    {
        if (_window)
            return true;
        auto *window = new SimWindow();
        if (!window->create(*this))
        {
            delete window;
            return false;
        }
        _window = window;
        if (const char *lastExit = std::getenv("PIPSIM_LAST_EXIT"))
        {
            if (*lastExit)
                pushConsoleLine(log::Level::Warning, lastExit, std::strlen(lastExit));
        }

        log::setSink(
            [](void *user, log::Level level, const char *line) noexcept
            {
                static_cast<Runtime *>(user)->pushConsoleLine(level, line, std::strlen(line));
            },
            this);
        return true;
    }

    void Runtime::resizeWindow() noexcept
    {
        if (_window)
            _window->syncNativeSize();
    }

    void Runtime::requestPresent() noexcept
    {
        _dirty = true;
    }

    void Runtime::presentNow() noexcept
    {
        _dirty = false;
        _lastPresentUs = monotonicMicros();

        ++_fpsFrameCount;
        if (_fpsWindowStartUs == 0)
        {
            _fpsWindowStartUs = _lastPresentUs;
        }
        else if (_lastPresentUs - _fpsWindowStartUs >= 1'000'000ULL)
        {
            _presentFps = static_cast<float>(_fpsFrameCount) * 1'000'000.0F /
                          static_cast<float>(_lastPresentUs - _fpsWindowStartUs);
            _fpsFrameCount = 0;
            _fpsWindowStartUs = _lastPresentUs;
        }
        _lastRenderCpuUs =
            (_frameCpuStartUs != 0 && _lastPresentUs >= _frameCpuStartUs) ? (_lastPresentUs - _frameCpuStartUs) : 0;
        _frameCpuStartUs = 0;

        if (!_historyBrowsing && !_framebuffer.empty() &&
            (_lastHistoryCaptureUs == 0 || (_lastPresentUs - _lastHistoryCaptureUs) >= 100'000U))
        {
            const uint64_t frameBytes = static_cast<uint64_t>(_framebuffer.size()) * 4ULL;
            const size_t maxFrames =
                std::max<size_t>(2U, static_cast<size_t>(kFrameHistoryMaxBytes / std::max<uint64_t>(frameBytes, 1ULL)));
            _frameHistory.push_back(_framebuffer);
            _lastHistoryCaptureUs = _lastPresentUs;
            const size_t cap = std::min<size_t>(maxFrames, kFrameHistoryMaxFrames);
            while (_frameHistory.size() > cap)
                _frameHistory.erase(_frameHistory.begin());
            _frameHistoryCursor = _frameHistory.size() - 1U;
        }

        if (_window)
            _window->invalidateCanvas();
    }

    void Runtime::simHeapTrackAdd(void *ptr, size_t bytes) noexcept
    {
        if (!ptr || bytes == 0)
            return;
        try
        {
            std::lock_guard<std::mutex> guard(_simHeapMutex);
            _simHeapBlocks.emplace_back(ptr, bytes);
            const uint64_t total = static_cast<uint64_t>(_simHeapBytes) + bytes;
            _simHeapBytes = static_cast<uint32_t>(std::min<uint64_t>(total, 0xFFFFFFFFULL));
            if (_simHeapBytes > _simHeapPeakBytes)
                _simHeapPeakBytes = _simHeapBytes;
        }
        catch (...)
        {
        }
    }

    size_t Runtime::simHeapTrackRemove(void *ptr) noexcept
    {
        if (!ptr)
            return 0;
        try
        {
            std::lock_guard<std::mutex> guard(_simHeapMutex);
            for (size_t i = 0; i < _simHeapBlocks.size(); ++i)
            {
                if (_simHeapBlocks[i].first == ptr)
                {
                    const size_t bytes = _simHeapBlocks[i].second;
                    _simHeapBlocks[i] = _simHeapBlocks.back();
                    _simHeapBlocks.pop_back();
                    _simHeapBytes = static_cast<uint32_t>(
                        std::max<int64_t>(0, static_cast<int64_t>(_simHeapBytes) - static_cast<int64_t>(bytes)));
                    return bytes;
                }
            }
        }
        catch (...)
        {
        }
        return 0;
    }

    void Runtime::syncWindowControls() noexcept
    {
        if (_window)
            _window->syncControls();
    }

    void Runtime::serviceRecording(uint64_t nowUs) noexcept
    {
        if (!_recording.active)
            return;
        if (nowUs >= _recording.nextFrameUs)
        {
            if (!encodeRecordingFrame())
            {
                const int32_t encodeHr = _recording.encodeHr;
                stopRecording();
                char message[128];
                std::snprintf(message, sizeof(message), simtext::tr(simtext::Id::MsgRecFailCode),
                              static_cast<unsigned long>(encodeHr));
                log::error("[sim] %s", message);
                _recordingPath.clear();
                return;
            }
            ++_recording.frameIndex;
            _recording.nextFrameUs += 1'000'000ULL / _recording.fps;
            if (nowUs > _recording.nextFrameUs + 1'000'000ULL / _recording.fps)
                _recording.nextFrameUs = nowUs + 1'000'000ULL / _recording.fps;
            if ((_recording.frameIndex % _recording.fps) == 0)
                syncWindowControls();
        }
    }

    uint64_t Runtime::recordingElapsedUs() const noexcept
    {
        return _recording.active ? (monotonicMicros() - _recording.startedUs) : 0ULL;
    }

    bool Runtime::restartProcess() noexcept
    {
        _restartRequested = true;
        writeControlFile("_sim-restart");
        _shouldQuit = true;
        if (_window)
            _window->requestClose();
        return true;
    }

    bool Runtime::isValidKernelDir(const char *dir) noexcept
    {
        if (!dir || !*dir)
            return false;
        try
        {
            namespace fs = std::filesystem;
            return fs::exists(fs::path(dir) / "CMakeLists.txt");
        }
        catch (...)
        {
            return false;
        }
    }

    bool Runtime::isValidAppDir(const char *dir) noexcept
    {
        if (!dir || !*dir)
            return false;
        try
        {
            namespace fs = std::filesystem;
            const fs::path root(dir);
            if (!fs::is_directory(root))
                return false;
            size_t visited = 0;
            std::error_code ec;
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
            {
                if (++visited > 8192U)
                    break;
                if (!it->is_regular_file(ec))
                    continue;
                const std::string ext = it->path().extension().string();
                if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c")
                    return true;
            }
        }
        catch (...)
        {
        }
        return false;
    }

    std::string Runtime::detectAppDir(const char *projectRoot) noexcept
    {
        if (!projectRoot || !*projectRoot)
            return {};

        for (const char *candidate : {"src", "main"})
        {
            const std::string dir = std::string(projectRoot) + "/" + candidate;
            if (isValidAppDir(dir.c_str()))
                return dir;
        }
        return {};
    }

    bool Runtime::pathsUsable() const noexcept
    {
        std::string kernel = _paths.kernelDir;
        std::string app = _paths.appDir;
        if (kernel.empty() && !_paths.projectRoot.empty())
            kernel = deriveKernelDir(_paths.projectRoot);
        if (app.empty())
            app = detectAppDir(_paths.projectRoot.c_str());
        return isValidKernelDir(kernel.c_str()) && isValidAppDir(app.c_str());
    }

    void Runtime::loadSettings() noexcept
    {
        const auto ini = simWorkDir() / "simulator.ini";
        _paths.projectRoot = normalizePath(readIniValue(ini, "project_root").c_str());
        _paths.kernelDir = normalizePath(readIniValue(ini, "kernel_dir").c_str());
        _paths.appDir = normalizePath(readIniValue(ini, "app_dir").c_str());
        if (_paths.kernelDir.empty() && !_paths.projectRoot.empty())
            _paths.kernelDir = deriveKernelDir(_paths.projectRoot);
        if (_paths.appDir.empty())
            _paths.appDir = detectAppDir(_paths.projectRoot.c_str());

        const std::string lang = readIniValue(ini, "language");
        simtext::Lang parsed = simtext::Lang::English;
        if (simtext::langFromCode(lang.c_str(), parsed))
        {
            simtext::setLang(parsed);
        }
        else if (lang.empty())
        {

#if defined(_WIN32)
            if ((GetUserDefaultUILanguage() & 0xFF) == 0x19)
                simtext::setLang(simtext::Lang::Russian);
#else
            const char *lc = std::getenv("LC_ALL");
            if (!lc || !*lc)
                lc = std::getenv("LANG");
            if (lc && (std::strstr(lc, "ru") != nullptr || std::strstr(lc, "RU") != nullptr))
                simtext::setLang(simtext::Lang::Russian);
#endif
        }

        const std::string fpsLimit = readIniValue(ini, "fps_limit");
        if (!fpsLimit.empty())
        {
            const int value = std::atoi(fpsLimit.c_str());
            _fpsLimit = (value > 0 && value <= 1000) ? static_cast<uint32_t>(value) : 0U;
        }

        _setupMode = !(isValidKernelDir(_paths.kernelDir.c_str()) && isValidAppDir(_paths.appDir.c_str()));
    }

    void Runtime::saveSettings() noexcept
    {
        try
        {
            const auto ini = simWorkDir() / "simulator.ini";
            std::string derivedKernel;
            if (!_paths.projectRoot.empty())
                derivedKernel = deriveKernelDir(_paths.projectRoot);
            const std::string derivedApp = detectAppDir(_paths.projectRoot.c_str());

            std::string content;
            content += "project_root=" + _paths.projectRoot + "\n";

            if (!_paths.kernelDir.empty() && _paths.kernelDir != derivedKernel)
                content += "kernel_dir=" + _paths.kernelDir + "\n";
            if (!_paths.appDir.empty() && _paths.appDir != derivedApp)
                content += "app_dir=" + _paths.appDir + "\n";
            content += "language=";
            content += simtext::langCode(simtext::lang());
            content += "\n";
            content += "fps_limit=";
            content += std::to_string(_fpsLimit);
            content += "\n";

            std::ofstream out(ini, std::ios::binary | std::ios::trunc);
            if (out)
                out << content;
        }
        catch (...)
        {
        }
    }

    void Runtime::refreshTitle() noexcept
    {
#ifdef PIPSIM_PROJECT_NAME
        _windowTitle = std::string(PIPSIM_PROJECT_NAME) + " - " + simtext::tr(simtext::Id::WindowTitleSuffix);
#else
        _windowTitle = simtext::tr(simtext::Id::WindowTitleDefault);
#endif
    }

    void Runtime::openSetupWindow() noexcept
    {
        if (_baseWidth == 0)
        {

            (void)configureDisplay(320, 240);
            (void)beginDisplay(0);
        }
        else
        {
            (void)ensureWindow();
        }
        if (!pathsUsable())
        {
            _setupMode = true;

            log::warning("[sim] %s", simtext::tr(simtext::Id::SetupConsoleHint));
        }
        syncWindowControls();
    }

    bool Runtime::applySetupPaths(const char *projectRoot, const char *kernelDir, const char *appDir) noexcept
    {
        if (projectRoot)
        {
            const std::string root = normalizePath(projectRoot);
            if (!root.empty())
                _paths.projectRoot = root;

            if (kernelDir == nullptr)
                _paths.kernelDir.clear();
            if (appDir == nullptr)
                _paths.appDir.clear();
        }
        if (kernelDir)
            _paths.kernelDir = normalizePath(kernelDir);
        if (appDir)
            _paths.appDir = normalizePath(appDir);

        if (_paths.kernelDir.empty() && !_paths.projectRoot.empty())
            _paths.kernelDir = deriveKernelDir(_paths.projectRoot);
        if (_paths.appDir.empty())
            _paths.appDir = detectAppDir(_paths.projectRoot.c_str());

        const bool ok = isValidKernelDir(_paths.kernelDir.c_str()) && isValidAppDir(_paths.appDir.c_str());
        _setupMode = !ok;
        saveSettings();
        syncWindowControls();
        return ok;
    }

    bool Runtime::saveAndRestartSim() noexcept
    {
        if (!pathsUsable())
        {
            setStatusText(simtext::tr(simtext::Id::SetupNotReady));
            return false;
        }
        saveSettings();

        writeControlFile("_sim-rebuild");
        log::info("[sim] %s", simtext::tr(simtext::Id::SetupSaved));
        return restartProcess();
    }

    void Runtime::setLanguage(simtext::Lang lang) noexcept
    {
        simtext::setLang(lang);
        refreshTitle();
        saveSettings();
        if (_window)
            _window->applyLanguage();
        syncWindowControls();
    }

    bool Runtime::saveScreenshot() noexcept
    {
        if (_framebuffer.empty())
            return false;
        const auto pngOut = timestampedPath("shots", "png");
        const bool ok = writePngRgba(pngOut, _width, _height, _framebuffer.data());
        char message[512];
        if (ok)
        {
            std::snprintf(message, sizeof(message), simtext::tr(simtext::Id::MsgScreenshot), pngOut.string().c_str());
            log::info("[sim] %s", message);
        }
        else
        {
            std::snprintf(message, sizeof(message), "%s", simtext::tr(simtext::Id::MsgScreenshotFail));
            log::error("[sim] %s", message);
        }
        return ok;
    }

    bool Runtime::toggleRecording() noexcept
    {
        if (_recording.active)
        {
            stopRecording();
            char message[512];
            std::snprintf(message, sizeof(message), simtext::tr(simtext::Id::MsgRecSaved), _recordingPath.c_str());
            log::info("[sim] %s", message);
            _recordingPath.clear();
            return true;
        }
        const bool ok = startRecording();
        if (ok)
        {
            char message[128];
            std::snprintf(message, sizeof(message), simtext::tr(simtext::Id::MsgRecStart),
                          static_cast<unsigned>(_recording.fps));
            log::info("[sim] %s", message);
        }
        else
        {
#if defined(_WIN32)
            log::error("[sim] %s", simtext::tr(simtext::Id::MsgRecNoEncoder));
#else
            log::error("[sim] %s", simtext::tr(simtext::Id::MsgRecNoFfmpeg));
#endif
        }
        return ok;
    }

    bool Runtime::startRecording() noexcept
    {
        if (_recording.active || _framebuffer.empty())
            return false;
        try
        {
            const auto out = timestampedPath("videos", "mp4");
            _recordingPath = out.string();
#if defined(_WIN32)

            const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (SUCCEEDED(comInit))
                _mfComOwned = true;
            else if (comInit != RPC_E_CHANGED_MODE)
                return false;

            auto *mf = new (std::nothrow) MfVideoWriter();
            if (!mf)
            {
                if (_mfComOwned)
                {
                    CoUninitialize();
                    _mfComOwned = false;
                }
                return false;
            }
            const HRESULT hr = mf->start(out.wstring().c_str(), _width, _height, _recording.fps);
            if (FAILED(hr))
            {
                delete mf;
                if (_mfComOwned)
                {
                    CoUninitialize();
                    _mfComOwned = false;
                }
                _recording.encodeHr = static_cast<int32_t>(hr);
                return false;
            }
            _mfWriter = mf;
#else
            std::signal(SIGPIPE, SIG_IGN);
            std::string command = "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt bgra -s ";
            command += std::to_string(_width) + "x" + std::to_string(_height);
            command +=
                " -r 30 -i - -an -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p -movflags +faststart ";
            command += shellQuote(out.string());
            FILE *pipe = popen(command.c_str(), "w");
            if (!pipe)
                return false;
            _recordPipe = pipe;
#endif
            _recording.frameIndex = 0;
            _recording.startedUs = monotonicMicros();
            _recording.nextFrameUs = _recording.startedUs;
            _recording.active = true;
            syncWindowControls();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void Runtime::stopRecording() noexcept
    {
        if (!_recording.active)
            return;
#if defined(_WIN32)
        if (_mfWriter)
        {
            static_cast<MfVideoWriter *>(_mfWriter)->stop();
            delete static_cast<MfVideoWriter *>(_mfWriter);
            _mfWriter = nullptr;
        }
        if (_mfComOwned)
        {
            CoUninitialize();
            _mfComOwned = false;
        }
#else
        if (_recordPipe)
        {
            FILE *pipe = static_cast<FILE *>(_recordPipe);
            std::fflush(pipe);
            pclose(pipe);
            _recordPipe = nullptr;
        }
#endif
        _recording = {};
        syncWindowControls();
    }

    bool Runtime::encodeRecordingFrame() noexcept
    {
        if (!_recording.active || _framebuffer.empty())
            return false;
#if defined(_WIN32)
        auto *mf = static_cast<MfVideoWriter *>(_mfWriter);
        if (!mf)
            return false;
        const HRESULT hr = mf->writeFrame(_framebuffer.data(), _width);
        if (FAILED(hr))
        {
            _recording.encodeHr = static_cast<int32_t>(hr);
            return false;
        }
        return true;
#else
        if (!_recordPipe)
            return false;
        FILE *pipe = static_cast<FILE *>(_recordPipe);

        return std::fwrite(_framebuffer.data(), sizeof(uint32_t), _framebuffer.size(), pipe) == _framebuffer.size();
#endif
    }

    void Runtime::handleKey(SimKey key, bool down) noexcept
    {
        switch (key)
        {
        case SimKey::Up:
            _upDown = down;
            break;
        case SimKey::Down:
            _downDown = down;
            break;
        case SimKey::Left:
            _prevDown = down;
            break;
        case SimKey::Right:
            _nextDown = down;
            break;
        case SimKey::Select:
            _selectDown = down;
            break;
        case SimKey::F1:
            if (down)
                setPaused(!_paused);
            break;
        case SimKey::F2:
            if (down)
                stepBack();
            break;
        case SimKey::F3:
            if (down)
                stepFrame();
            break;
        case SimKey::F5:
            if (down)
                (void)saveScreenshot();
            break;
        case SimKey::F9:
            if (down)
                (void)toggleRecording();
            break;
        }
    }

    void Runtime::injectTouch(bool down, int x, int y) noexcept
    {
#if PIPCORE_ENABLE_TOUCH
        if (pipcore::Platform *plat = pipcore::GetPlatform())
        {
            if (pipcore::Touch *touch = plat->touch())
            {
                auto *deskTouch = static_cast<pipcore::desktop::Touch *>(touch);
                int cx = std::max(0, std::min(x, static_cast<int>(_width - 1)));
                int cy = std::max(0, std::min(y, static_cast<int>(_height - 1)));
                deskTouch->injectPointer(down, static_cast<uint16_t>(cx), static_cast<uint16_t>(cy));
            }
        }
#else
        (void)down;
        (void)x;
        (void)y;
#endif
    }

    bool Runtime::pinPressed(uint8_t pin) const noexcept
    {
        if (pin == kPrevPin)
            return _prevDown;
        if (pin == kNextPin)
            return _nextDown;
        if (pin == kSelectPin)
            return _selectDown;
        return false;
    }

    uint32_t Runtime::color565ToArgb(uint16_t color565) noexcept
    {
        const uint32_t r5 = (color565 >> 11) & 0x1FU;
        const uint32_t g6 = (color565 >> 5) & 0x3FU;
        const uint32_t b5 = color565 & 0x1FU;
        return 0xFF000000u | (static_cast<uint32_t>(kExpand5[r5]) << 16) | (static_cast<uint32_t>(kExpand6[g6]) << 8) |
               kExpand5[b5];
    }

    void Runtime::setStatusText(const char *message) noexcept
    {
        if (_window && message)
            _window->setStatusText(message);
    }

    void Runtime::pushConsoleLine(log::Level level, const char *text, size_t len) noexcept
    {
        if (!text || len == 0)
            return;
        std::lock_guard<std::mutex> guard(_consoleMutex);
        pushConsoleLineLocked(level, text, len);
    }

    void Runtime::pushConsoleLineLocked(log::Level level, const char *text, size_t len) noexcept
    {
        if (_consoleFile)
        {
            std::fwrite(text, 1U, len, _consoleFile);
            std::fputc('\n', _consoleFile);
            std::fflush(_consoleFile);
        }

        const size_t textLen = std::min<size_t>(len, kConsoleLineCap);
        if (_consolePending.size() >= kConsoleMaxPending)
            _consolePending.erase(_consolePending.begin(),
                                  _consolePending.begin() + static_cast<long>(kConsoleMaxPending / 4U));
        _consolePending.emplace_back(ConsoleLine{level, std::string(text, textLen)});
    }

    void Runtime::drainConsoleLines(std::vector<ConsoleLine> &out) noexcept
    {
        std::lock_guard<std::mutex> guard(_consoleMutex);
        if (_consolePending.empty())
            return;
        out.insert(out.end(), std::make_move_iterator(_consolePending.begin()),
                   std::make_move_iterator(_consolePending.end()));
        _consolePending.clear();
    }

    bool Runtime::setLogToFile(bool enabled) noexcept
    {
        std::lock_guard<std::mutex> guard(_consoleMutex);
        if (enabled == _logToFile)
            return true;
        if (!enabled)
        {
            if (_consoleFile)
            {
                std::fclose(_consoleFile);
                _consoleFile = nullptr;
            }
            _logToFile = false;
            return true;
        }
        try
        {
            const auto path = simWorkDir() / "simulator.log";
            _consoleFile = std::fopen(path.string().c_str(), "ab");
            if (!_consoleFile)
                return false;
            std::fputs("=== session started ===\n", _consoleFile);
            _logToFile = true;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

#endif
