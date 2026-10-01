#pragma once

#include "Config.hpp"

#if PIPCORE_TARGET_DESKTOP && defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pipcore::desktop
{
    class Runtime;
    struct ConsoleLine;

    class SimWindow final
    {
    public:
        SimWindow() noexcept = default;
        ~SimWindow() noexcept;

        SimWindow(const SimWindow &) = delete;
        SimWindow &operator=(const SimWindow &) = delete;

        [[nodiscard]] bool create(Runtime &runtime);
        void close() noexcept;

        void pumpEvents() noexcept;

        void syncControls() noexcept;
        void applyLanguage() noexcept;
        void setStatusText(const char *text) noexcept;
        void invalidateCanvas() noexcept;
        void syncNativeSize() noexcept;
        void requestClose() noexcept;

    private:
        [[nodiscard]] int s(int logical) const noexcept { return MulDiv(logical, static_cast<int>(_dpi), 96); }
        void reloadFonts() noexcept;
        void applyFonts() noexcept;
        void buildMenu() noexcept;
        void layoutChildren() noexcept;
        void presentCanvas(HDC hdc) noexcept;
        void drawSetupOverlay(HDC hdc) noexcept;
        void browseSetup(int which) noexcept;
        void applySetupVisibility() noexcept;
        void fillLevelCombo() noexcept;
        void fillFpsCombo() noexcept;
        void drainConsole() noexcept;
        void appendConsoleLine(const ConsoleLine &line) noexcept;
        void rebuildConsoleView() noexcept;
        void appendLogChunk(const wchar_t *text, unsigned color) noexcept;
        [[nodiscard]] HFONT loadUiFont(int pixelHeight, bool mono, int weight) noexcept;
        void makeTooltip(HWND target, const wchar_t *text) noexcept;
        [[nodiscard]] LRESULT frameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
        [[nodiscard]] LRESULT canvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
        [[nodiscard]] LRESULT metricsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
        [[nodiscard]] LRESULT statusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
        void mapTouch(int clientX, int clientY, int &outX, int &outY) const noexcept;

        Runtime *_runtime = nullptr;
        HWND _hwnd = nullptr;
        HWND _canvas = nullptr;
        HWND _metrics = nullptr;
        HWND _log = nullptr;
        HWND _status = nullptr;
        HWND _tooltip = nullptr;
        UINT _dpi = 96;
        bool _logIsRich = false;

        HMENU _menuBar = nullptr;
        HMENU _menuSim = nullptr;
        HMENU _menuLang = nullptr;

        HWND _tbPause = nullptr;
        HWND _tbBack = nullptr;
        HWND _tbFwd = nullptr;
        HWND _tbShot = nullptr;
        HWND _tbRecord = nullptr;

        HWND _sectionRuntime = nullptr;
        HWND _stepLabel = nullptr;
        HWND _stepEdit = nullptr;
        HWND _stepSpin = nullptr;
        HWND _timeLabel = nullptr;
        HWND _timeSlider = nullptr;
        HWND _timeValue = nullptr;
        HWND _fpsLabel = nullptr;
        HWND _fpsCombo = nullptr;

        HWND _setupSection = nullptr;
        HWND _setupProjectName = nullptr;
        HWND _setupKernelName = nullptr;
        HWND _setupAppName = nullptr;
        HWND _setupProjectLabel = nullptr;
        HWND _setupKernelLabel = nullptr;
        HWND _setupAppLabel = nullptr;
        HWND _setupProjectBtn = nullptr;
        HWND _setupKernelBtn = nullptr;
        HWND _setupAppBtn = nullptr;
        HWND _setupSaveBtn = nullptr;

        HWND _levelCombo = nullptr;
        HWND _logFileCheck = nullptr;
        HWND _clearBtn = nullptr;
        HWND _copyBtn = nullptr;

        HFONT _uiFont = nullptr;
        HFONT _uiBoldFont = nullptr;
        HFONT _monoFont = nullptr;
        HFONT _setupTitleFont = nullptr;
        HBRUSH _canvasBrush = nullptr;

        std::vector<ConsoleLine> _console;
        int _viewLevel = 2;

        std::wstring _statusRec;
        std::string _statusMsg;
    };
}

#endif
