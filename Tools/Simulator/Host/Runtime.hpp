#pragma once

#include "Config.hpp"
#if PIPCORE_TARGET_DESKTOP

#include <array>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include <Log.hpp>
#include <Platform.hpp>

#include "Host/SimText.hpp"

namespace pipcore::desktop
{
    class SimWindow;

    struct SimPaths
    {
        std::string projectRoot;
        std::string kernelDir;
        std::string appDir;
    };

    enum class SimKey : int
    {
        Up = 1,
        Down,
        Left,
        Right,
        Select,
        F1,
        F2,
        F3,
        F5,
        F9
    };

    struct ConsoleLine
    {
        log::Level level = log::Level::Info;
        std::string text;
    };

    class Runtime final
    {
    public:
        static Runtime &instance() noexcept;

        [[nodiscard]] bool configureDisplay(uint16_t width, uint16_t height) noexcept;
        [[nodiscard]] bool beginDisplay(uint8_t rotation) noexcept;
        [[nodiscard]] bool setDisplayRotation(uint8_t rotation) noexcept;

        [[nodiscard]] uint16_t width() const noexcept { return _width; }
        [[nodiscard]] uint16_t height() const noexcept { return _height; }
        [[nodiscard]] uint8_t scale() const noexcept { return _scale; }
        [[nodiscard]] const uint32_t *framebuffer() const noexcept { return _framebuffer.data(); }

        void fillScreen565(uint16_t color565) noexcept;
        void writeRect565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels,
                          int32_t stridePixels) noexcept;

        void pumpEvents() noexcept;
        [[nodiscard]] bool shouldQuit() const noexcept { return _shouldQuit; }
        void requestQuit() noexcept { _shouldQuit = true; }
        [[nodiscard]] bool isRestartRequested() const noexcept { return _restartRequested; }
        void markUserExit() noexcept;

        void setPaused(bool paused) noexcept;
        [[nodiscard]] bool isPaused() const noexcept { return _paused; }
        void stepFrame() noexcept;
        void stepBack() noexcept;
        void setFrameStepCount(uint32_t frames) noexcept { _frameStepCount = frames ? frames : 1U; }
        [[nodiscard]] uint32_t frameStepCount() const noexcept { return _frameStepCount; }

        void setTimeScalePercent(uint32_t percent) noexcept { _timeScalePercent = percent; }
        [[nodiscard]] uint32_t timeScalePercent() const noexcept { return _timeScalePercent; }

        void setFpsLimit(uint32_t fps) noexcept;
        [[nodiscard]] uint32_t fpsLimit() const noexcept { return _fpsLimit; }

        [[nodiscard]] bool saveScreenshot() noexcept;
        [[nodiscard]] bool toggleRecording() noexcept;
        [[nodiscard]] bool isRecording() const noexcept { return _recording.active; }
        [[nodiscard]] uint64_t recordingElapsedUs() const noexcept;

        [[nodiscard]] bool restartProcess() noexcept;

        [[nodiscard]] const SimPaths &paths() const noexcept { return _paths; }
        [[nodiscard]] bool setupMode() const noexcept { return _setupMode; }

        void openSetupWindow() noexcept;

        [[nodiscard]] bool applySetupPaths(const char *projectRoot, const char *kernelDir, const char *appDir) noexcept;

        bool saveAndRestartSim() noexcept;
        [[nodiscard]] static bool isValidKernelDir(const char *dir) noexcept;
        [[nodiscard]] static bool isValidAppDir(const char *dir) noexcept;

        [[nodiscard]] static std::string detectAppDir(const char *projectRoot) noexcept;

        void setLanguage(simtext::Lang lang) noexcept;

        [[nodiscard]] uint64_t lastRenderCpuUs() const noexcept { return _lastRenderCpuUs; }

        [[nodiscard]] float presentFps() const noexcept { return _presentFps; }

        void simHeapTrackAdd(void *ptr, size_t bytes) noexcept;

        [[nodiscard]] size_t simHeapTrackRemove(void *ptr) noexcept;
        [[nodiscard]] uint32_t simHeapBytes() const noexcept { return _simHeapBytes; }
        [[nodiscard]] uint32_t simHeapPeakBytes() const noexcept { return _simHeapPeakBytes; }
        [[nodiscard]] static constexpr uint32_t simHeapBudgetBytes() noexcept { return 240U * 1024U; }

        void setStatusText(const char *message) noexcept;

        void pushConsoleLine(log::Level level, const char *text, size_t len) noexcept;

        void drainConsoleLines(std::vector<ConsoleLine> &out) noexcept;
        [[nodiscard]] bool logToFile() const noexcept { return _logToFile; }
        [[nodiscard]] bool setLogToFile(bool enabled) noexcept;

        void pinModeInput(uint8_t pin, pipcore::InputMode mode) noexcept;
        [[nodiscard]] bool digitalRead(uint8_t pin) const noexcept;
        [[nodiscard]] int16_t analogRead(uint8_t pin) const noexcept;
        void injectTouch(bool down, int x, int y) noexcept;
        void handleKey(SimKey key, bool down) noexcept;

        [[nodiscard]] uint32_t nowMs() noexcept;
        [[nodiscard]] uint64_t nowMicros() noexcept;
        void delayMs(uint32_t ms) noexcept;

        [[nodiscard]] const char *windowTitle() const noexcept { return _windowTitle.c_str(); }

    private:
        Runtime() noexcept;
        Runtime(const Runtime &) = delete;
        Runtime &operator=(const Runtime &) = delete;

        friend class SimWindow;

        [[nodiscard]] bool ensureWindow() noexcept;
        [[nodiscard]] bool pathsUsable() const noexcept;
        void loadSettings() noexcept;
        void saveSettings() noexcept;
        void refreshTitle() noexcept;
        void resizeWindow() noexcept;
        void requestPresent() noexcept;
        void presentNow() noexcept;
        void syncWindowControls() noexcept;
        void serviceRecording(uint64_t nowUs) noexcept;
        void serviceSimClock(uint64_t realNowUs) noexcept;
        void advanceFrameStep() noexcept;
        [[nodiscard]] bool startRecording() noexcept;
        void stopRecording() noexcept;
        [[nodiscard]] bool encodeRecordingFrame() noexcept;
        void pushConsoleLineLocked(log::Level level, const char *text, size_t len) noexcept;
        [[nodiscard]] bool pinPressed(uint8_t pin) const noexcept;
        [[nodiscard]] static uint32_t color565ToArgb(uint16_t color565) noexcept;

        SimWindow *_window = nullptr;
        void *_recordPipe = nullptr;
#if defined(_WIN32)
        void *_mfWriter = nullptr;
        bool _mfComOwned = false;
#endif
        std::string _recordingPath;
        std::string _windowTitle;

        uint16_t _baseWidth = 0;
        uint16_t _baseHeight = 0;
        uint16_t _width = 0;
        uint16_t _height = 0;
        uint8_t _rotation = 0;
        uint8_t _scale = 1;

        bool _shouldQuit = false;
        bool _dirty = false;
        bool _paused = false;
        bool _restartRequested = false;
        bool _historyBrowsing = false;
        bool _logToFile = false;

        uint64_t _lastPresentUs = 0;
        uint64_t _lastRealClockUs = 0;
        uint64_t _simClockUs = 0;
        uint64_t _lastHistoryCaptureUs = 0;
        uint32_t _timeScalePercent = 100;
        uint32_t _pendingFrameSteps = 0;
        uint32_t _frameStepCount = 1;
        uint32_t _fpsLimit = 60;
        float _presentFps = 0.0F;
        uint32_t _fpsFrameCount = 0;
        uint64_t _fpsWindowStartUs = 0;
        uint64_t _frameCpuStartUs = 0;
        uint64_t _lastRenderCpuUs = 0;
        uint32_t _simHeapBytes = 0;
        uint32_t _simHeapPeakBytes = 0;

        struct RecordingState
        {
            uint64_t startedUs = 0;
            uint64_t nextFrameUs = 0;
            uint64_t frameIndex = 0;
            uint32_t fps = 30;
            int32_t encodeHr = 0;
            bool active = false;
        } _recording = {};

        SimPaths _paths;
        bool _setupMode = false;

        std::vector<uint32_t> _framebuffer;
        std::vector<std::vector<uint32_t>> _frameHistory;
        size_t _frameHistoryCursor = 0;

        std::mutex _simHeapMutex;
        std::vector<std::pair<void *, size_t>> _simHeapBlocks;

        pipcore::InputMode _pinModes[256] = {};
        bool _prevDown = false;
        bool _nextDown = false;
        bool _selectDown = false;
        bool _upDown = false;
        bool _downDown = false;

        std::mutex _consoleMutex;
        std::vector<ConsoleLine> _consolePending;
        std::FILE *_consoleFile = nullptr;
    };
}

#endif
