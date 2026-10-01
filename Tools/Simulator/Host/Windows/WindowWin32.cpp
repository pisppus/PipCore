#include "Config.hpp"
#if PIPCORE_TARGET_DESKTOP && defined(_WIN32)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <richedit.h>
#include <shobjidl.h>
#include <uxtheme.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Host/Windows/WindowWin32.hpp"
#include "Host/Runtime.hpp"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace pipcore::desktop
{
    namespace
    {
        constexpr COLORREF kCanvasBg = RGB(234, 234, 234);

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

        constexpr int kIdRestart = 2001;
        constexpr int kIdMenuExit = 2002;
        constexpr int kIdLangEn = 2003;
        constexpr int kIdLangRu = 2004;
        constexpr int kIdPause = 2101;
        constexpr int kIdBack = 2102;
        constexpr int kIdForward = 2103;
        constexpr int kIdShot = 2104;
        constexpr int kIdRecord = 2105;
        constexpr int kIdStepEdit = 2201;
        constexpr int kIdTimeSlider = 2202;
        constexpr int kIdFpsCombo = 2203;
        constexpr int kIdLevelCombo = 2301;
        constexpr int kIdLogFile = 2302;
        constexpr int kIdClear = 2303;
        constexpr int kIdCopy = 2304;
        constexpr int kIdSetupProject = 2401;
        constexpr int kIdSetupKernel = 2402;
        constexpr int kIdSetupApp = 2403;
        constexpr int kIdSetupSave = 2404;

        constexpr uint32_t kFpsChoices[] = {0U, 240U, 120U, 60U, 30U};

        [[nodiscard]] std::wstring toWide(const char *utf8) noexcept
        {
            if (!utf8 || !*utf8)
                return {};
            const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
            if (len <= 0)
                return {};
            std::wstring wide(static_cast<size_t>(len - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), len);
            return wide;
        }

        [[nodiscard]] std::string toUtf8(const wchar_t *wide) noexcept
        {
            if (!wide || !*wide)
                return {};
            const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
            if (len <= 0)
                return {};
            std::string utf8(static_cast<size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), len, nullptr, nullptr);
            return utf8;
        }

        void setWindowText(HWND control, const char *utf8) noexcept
        {
            if (control)
                SetWindowTextW(control, toWide(utf8).c_str());
        }

        [[nodiscard]] UINT windowDpi(HWND hwnd) noexcept
        {
            using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
            if (const HMODULE user32 = GetModuleHandleW(L"user32.dll"))
                if (const auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow")))
                    if (hwnd)
                        return fn(hwnd);
            HDC dc = GetDC(hwnd);
            const UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96U;
            if (dc)
                ReleaseDC(hwnd, dc);
            return dpi ? dpi : 96U;
        }

        [[nodiscard]] bool fontFaceInstalled(const wchar_t *face) noexcept
        {
            struct Ctx
            {
                const wchar_t *face;
                bool found;
            } ctx{face, false};

            LOGFONTW lf = {};
            lf.lfCharSet = DEFAULT_CHARSET;
            wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);

            HDC hdc = GetDC(nullptr);
            EnumFontFamiliesExW(
                hdc, &lf,
                [](const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM lp) -> int
                {
                    auto *ctx = reinterpret_cast<Ctx *>(lp);
                    ctx->found = true;
                    return 0;
                },
                reinterpret_cast<LPARAM>(&ctx), 0);
            ReleaseDC(nullptr, hdc);
            return ctx.found;
        }

        void setEditText(HWND edit, const std::string &text) noexcept
        {
            setWindowText(edit, text.c_str());
        }

        [[nodiscard]] std::string getEditText(HWND edit) noexcept
        {
            wchar_t buf[32] = {};
            GetWindowTextW(edit, buf, 32);
            return toUtf8(buf);
        }

        [[nodiscard]] char levelTag(log::Level level) noexcept
        {
            switch (level)
            {
            case log::Level::Verbose:
                return 'V';
            case log::Level::Debug:
                return 'D';
            case log::Level::Info:
                return 'I';
            case log::Level::Warning:
                return 'W';
            case log::Level::Error:
                return 'E';
            default:
                return 'I';
            }
        }

        [[nodiscard]] COLORREF levelColor(log::Level level) noexcept
        {
            switch (level)
            {
            case log::Level::Verbose:
                return RGB(128, 128, 128);
            case log::Level::Debug:
                return RGB(0, 100, 190);
            case log::Level::Info:
                return RGB(24, 24, 24);
            case log::Level::Warning:
                return RGB(176, 110, 0);
            case log::Level::Error:
                return RGB(200, 30, 30);
            default:
                return RGB(24, 24, 24);
            }
        }

        [[nodiscard]] std::string pickFolder(HWND owner, const char *titleUtf8) noexcept
        {
            std::string result;
            const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            const bool comInitialized = SUCCEEDED(hrInit);
            if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE)
                return {};

            IFileDialog *dialog = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) &&
                dialog)
            {
                DWORD options = 0;
                if (SUCCEEDED(dialog->GetOptions(&options)))
                    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                const std::wstring title = toWide(titleUtf8);
                dialog->SetTitle(title.c_str());
                if (SUCCEEDED(dialog->Show(owner)))
                {
                    IShellItem *item = nullptr;
                    if (SUCCEEDED(dialog->GetResult(&item)) && item)
                    {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                        {
                            result = toUtf8(path);
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                dialog->Release();
            }

            if (comInitialized)
                CoUninitialize();
            return result;
        }
    }

    SimWindow::~SimWindow() noexcept
    {
        close();
    }

    bool SimWindow::create(Runtime &runtime)
    {
        if (_hwnd)
            return true;
        _runtime = &runtime;

        INITCOMMONCONTROLSEX icc = {};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_UPDOWN_CLASS;
        InitCommonControlsEx(&icc);
        _logIsRich = LoadLibraryW(L"Msftedit.dll") != nullptr;

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT
        {
            if (msg == WM_NCCREATE)
            {
                auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            }
            auto *self = reinterpret_cast<SimWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            return self ? self->frameProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = L"PipCoreSimWindow";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);

        WNDCLASSEXW canvasWc = wc;
        canvasWc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT
        {
            if (msg == WM_NCCREATE)
            {
                auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            }
            auto *self = reinterpret_cast<SimWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            return self ? self->canvasProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
        };
        canvasWc.lpszClassName = L"PipCoreSimCanvas";
        canvasWc.hbrBackground = CreateSolidBrush(kCanvasBg);
        RegisterClassExW(&canvasWc);

        WNDCLASSEXW metricsWc = canvasWc;
        metricsWc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT
        {
            if (msg == WM_NCCREATE)
            {
                auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            }
            auto *self = reinterpret_cast<SimWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            return self ? self->metricsProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
        };
        metricsWc.lpszClassName = L"PipCoreSimMetrics";
        metricsWc.hbrBackground = nullptr;
        RegisterClassExW(&metricsWc);

        WNDCLASSEXW statusWc = metricsWc;
        statusWc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT
        {
            if (msg == WM_NCCREATE)
            {
                auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            }
            auto *self = reinterpret_cast<SimWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            return self ? self->statusProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
        };
        statusWc.lpszClassName = L"PipCoreSimStatus";
        statusWc.hbrBackground = nullptr;
        RegisterClassExW(&statusWc);

        _hwnd = CreateWindowExW(0, L"PipCoreSimWindow", toWide(_runtime->windowTitle()).c_str(),
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 940, 706, nullptr,
                                nullptr, GetModuleHandleW(nullptr), this);
        if (!_hwnd)
            return false;
        _dpi = windowDpi(_hwnd);

        _canvasBrush = CreateSolidBrush(kCanvasBg);
        reloadFonts();

        const HINSTANCE hi = GetModuleHandleW(nullptr);
        const DWORD childStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP;

        _canvas = CreateWindowExW(0, L"PipCoreSimCanvas", nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER, 0, 0, 10, 10,
                                  _hwnd, nullptr, hi, this);
        _metrics = CreateWindowExW(0, L"PipCoreSimMetrics", nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 10, s(40), _hwnd,
                                   nullptr, hi, this);
        _status = CreateWindowExW(0, L"PipCoreSimStatus", nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 10, s(26), _hwnd, nullptr,
                                  hi, this);

        auto makeBtn = [this, hi](int id, const char *label, int w)
        {
            return CreateWindowExW(0, L"BUTTON", toWide(label).c_str(), childStyle | BS_PUSHBUTTON, 0, 0, w, s(32),
                                   _hwnd, (HMENU)(intptr_t)id, hi, nullptr);
        };
        _tbPause = makeBtn(kIdPause, simtext::tr(simtext::Id::Pause), s(120));
        _tbBack = makeBtn(kIdBack, simtext::tr(simtext::Id::StepBack), s(56));
        _tbFwd = makeBtn(kIdForward, simtext::tr(simtext::Id::StepFwd), s(56));
        _tbShot = makeBtn(kIdShot, simtext::tr(simtext::Id::Screenshot), s(56));
        _tbRecord = makeBtn(kIdRecord, simtext::tr(simtext::Id::Record), s(56));

        _sectionRuntime = CreateWindowExW(0, L"STATIC", toWide(simtext::tr(simtext::Id::SectionEmulation)).c_str(),
                                          WS_CHILD | WS_VISIBLE, 0, 0, s(130), s(18), _hwnd, nullptr, hi, nullptr);
        _stepLabel = CreateWindowExW(0, L"STATIC", toWide(simtext::tr(simtext::Id::StepFrames)).c_str(),
                                     WS_CHILD | WS_VISIBLE, 0, 0, s(110), s(18), _hwnd, nullptr, hi, nullptr);
        _stepEdit = CreateWindowExW(0, L"EDIT", L"1", childStyle | ES_NUMBER, 0, 0, s(56), s(24), _hwnd,
                                    (HMENU)(intptr_t)kIdStepEdit, hi, nullptr);
        _stepSpin = CreateWindowExW(0, UPDOWN_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS, 0,
                                    0, 0, 0, _hwnd, nullptr, hi, nullptr);
        SendMessageW(_stepSpin, UDM_SETBUDDY, (WPARAM)_stepEdit, 0);
        SendMessageW(_stepSpin, UDM_SETRANGE32, 1, 120);
        SendMessageW(_stepSpin, UDM_SETPOS32, 0, 1);

        _timeLabel = CreateWindowExW(0, L"STATIC", toWide(simtext::tr(simtext::Id::TimeScale)).c_str(),
                                     WS_CHILD | WS_VISIBLE, 0, 0, s(100), s(18), _hwnd, nullptr, hi, nullptr);
        _timeValue = CreateWindowExW(0, L"STATIC", L"100%", WS_CHILD | WS_VISIBLE, 0, 0, s(48), s(18), _hwnd, nullptr, hi,
                                     nullptr);
        _timeSlider = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0, 0,
                                      s(240), s(28), _hwnd, (HMENU)(intptr_t)kIdTimeSlider, hi, nullptr);
        SendMessageW(_timeSlider, TBM_SETRANGE, TRUE, MAKELPARAM(5, 200));
        SendMessageW(_timeSlider, TBM_SETPOS, TRUE, 100);

        _fpsLabel = CreateWindowExW(0, L"STATIC", toWide(simtext::tr(simtext::Id::FpsLimit)).c_str(),
                                    WS_CHILD | WS_VISIBLE, 0, 0, s(100), s(18), _hwnd, nullptr, hi, nullptr);
        _fpsCombo = CreateWindowExW(0, L"COMBOBOX", nullptr, childStyle | CBS_DROPDOWNLIST, 0, 0, s(110), s(200), _hwnd,
                                    (HMENU)(intptr_t)kIdFpsCombo, hi, nullptr);
        fillFpsCombo();

        _setupSection = CreateWindowExW(0, L"STATIC", toWide(simtext::tr(simtext::Id::SetupSection)).c_str(), WS_CHILD, 0,
                                        0, s(240), s(18), _hwnd, nullptr, hi, nullptr);
        auto makeCaption = [hi, this](const char *text)
        {
            return CreateWindowExW(0, L"STATIC", toWide(text).c_str(), WS_CHILD, 0, 0, 10, s(16), _hwnd, nullptr, hi,
                                   nullptr);
        };
        _setupProjectName = makeCaption(simtext::tr(simtext::Id::SetupProject));
        _setupKernelName = makeCaption(simtext::tr(simtext::Id::SetupKernel));
        _setupAppName = makeCaption(simtext::tr(simtext::Id::SetupApp));
        auto makePathLabel = [hi, this](int id)
        {
            return CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_PATHELLIPSIS | SS_NOTIFY, 0, 0, 10, s(20), _hwnd,
                                   (HMENU)(intptr_t)id, hi, nullptr);
        };
        _setupProjectLabel = makePathLabel(kIdSetupProject);
        _setupKernelLabel = makePathLabel(kIdSetupKernel);
        _setupAppLabel = makePathLabel(kIdSetupApp);
        _setupProjectBtn = makeBtn(kIdSetupProject, simtext::tr(simtext::Id::SetupBrowse), s(88));
        _setupKernelBtn = makeBtn(kIdSetupKernel, simtext::tr(simtext::Id::SetupBrowse), s(88));
        _setupAppBtn = makeBtn(kIdSetupApp, simtext::tr(simtext::Id::SetupBrowse), s(88));
        _setupSaveBtn = makeBtn(kIdSetupSave, simtext::tr(simtext::Id::SetupSave), s(240));

        _levelCombo = CreateWindowExW(0, L"COMBOBOX", nullptr, childStyle | CBS_DROPDOWNLIST, 0, 0, s(150), s(200),
                                      _hwnd, (HMENU)(intptr_t)kIdLevelCombo, hi, nullptr);
        fillLevelCombo();

        _logFileCheck = CreateWindowExW(0, L"BUTTON", toWide(simtext::tr(simtext::Id::LogToFile)).c_str(),
                                        childStyle | BS_AUTOCHECKBOX, 0, 0, s(110), s(22), _hwnd,
                                        (HMENU)(intptr_t)kIdLogFile, hi, nullptr);
        _clearBtn = makeBtn(kIdClear, simtext::tr(simtext::Id::Clear), s(76));
        _copyBtn = makeBtn(kIdCopy, simtext::tr(simtext::Id::Copy), s(76));

        const DWORD logStyle =
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL;
        _log = CreateWindowExW(0, _logIsRich ? MSFTEDIT_CLASS : L"EDIT", nullptr, logStyle, 0, 0, 10, s(150), _hwnd,
                               nullptr, hi, nullptr);
        if (_logIsRich)
            SendMessageW(_log, EM_EXLIMITTEXT, 0, 0x4000000);

        _tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX, 0, 0, 0, 0, _hwnd,
                                   nullptr, hi, nullptr);
        makeTooltip(_tbPause, toWide((std::string(simtext::tr(simtext::Id::Pause)) + " | F6").c_str()).c_str());
        makeTooltip(_tbShot, toWide((std::string(simtext::tr(simtext::Id::Screenshot)) + " | F12").c_str()).c_str());
        makeTooltip(_tbRecord, toWide((std::string(simtext::tr(simtext::Id::Record)) + " | F9").c_str()).c_str());

        applyFonts();
        buildMenu();

        const UINT preference = DWMWCP_ROUND;
        (void)DwmSetWindowAttribute(_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
        syncControls();
        layoutChildren();
        ShowWindow(_hwnd, SW_SHOW);
        UpdateWindow(_hwnd);
        SetFocus(_canvas);
        return true;
    }

    void SimWindow::buildMenu() noexcept
    {
        if (!_hwnd || !_runtime)
            return;
        if (_menuBar)
        {
            SetMenu(_hwnd, nullptr);
            DestroyMenu(_menuBar);
            _menuBar = _menuSim = _menuLang = nullptr;
        }

        _menuSim = CreatePopupMenu();
        _menuLang = CreatePopupMenu();
        if (!_menuSim || !_menuLang)
            return;
        AppendMenuW(_menuSim, MF_STRING, kIdRestart, toWide(simtext::tr(simtext::Id::Restart)).c_str());
        AppendMenuW(_menuSim, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(_menuSim, MF_STRING, kIdMenuExit, toWide(simtext::tr(simtext::Id::MenuExit)).c_str());
        AppendMenuW(_menuLang, MF_STRING | (simtext::lang() == simtext::Lang::English ? MF_CHECKED : MF_UNCHECKED),
                    kIdLangEn, toWide(simtext::tr(simtext::Id::LangEn)).c_str());
        AppendMenuW(_menuLang, MF_STRING | (simtext::lang() == simtext::Lang::Russian ? MF_CHECKED : MF_UNCHECKED),
                    kIdLangRu, toWide(simtext::tr(simtext::Id::LangRu)).c_str());

        _menuBar = CreateMenu();
        AppendMenuW(_menuBar, MF_POPUP, (UINT_PTR)_menuSim, toWide(simtext::tr(simtext::Id::MenuSim)).c_str());
        AppendMenuW(_menuBar, MF_POPUP, (UINT_PTR)_menuLang, toWide(simtext::tr(simtext::Id::MenuLanguage)).c_str());
        SetMenu(_hwnd, _menuBar);
    }

    void SimWindow::close() noexcept
    {
        if (_hwnd)
        {
            DestroyWindow(_hwnd);
            _hwnd = nullptr;
        }
        _tooltip = nullptr;
        if (_uiFont)
            DeleteObject(_uiFont);
        if (_uiBoldFont)
            DeleteObject(_uiBoldFont);
        if (_monoFont)
            DeleteObject(_monoFont);
        if (_setupTitleFont)
            DeleteObject(_setupTitleFont);
        if (_canvasBrush)
            DeleteObject(_canvasBrush);
        _uiFont = _uiBoldFont = _monoFont = _setupTitleFont = nullptr;
        _canvasBrush = nullptr;
    }

    void SimWindow::requestClose() noexcept
    {
        if (_hwnd)
            PostMessageW(_hwnd, WM_CLOSE, 0, 0);
    }

    void SimWindow::pumpEvents() noexcept
    {
        drainConsole();

        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                if (_runtime)
                    _runtime->requestQuit();
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    void SimWindow::fillLevelCombo() noexcept
    {
        if (!_levelCombo)
            return;
        SendMessageW(_levelCombo, CB_RESETCONTENT, 0, 0);
        const char *items[] = {
            simtext::tr(simtext::Id::LevelTrace),
            simtext::tr(simtext::Id::LevelDebug),
            simtext::tr(simtext::Id::LevelInfo),
            simtext::tr(simtext::Id::LevelWarnings),
            simtext::tr(simtext::Id::LevelErrors),
        };
        for (const char *item : items)
            SendMessageW(_levelCombo, CB_ADDSTRING, 0, (LPARAM)toWide(item).c_str());
        SendMessageW(_levelCombo, CB_SETCURSEL, _viewLevel, 0);
    }

    void SimWindow::fillFpsCombo() noexcept
    {
        if (!_fpsCombo)
            return;
        SendMessageW(_fpsCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(_fpsCombo, CB_ADDSTRING, 0, (LPARAM)toWide(simtext::tr(simtext::Id::Unlimited)).c_str());
        for (const uint32_t fps : kFpsChoices)
        {
            if (fps == 0U)
                continue;
            wchar_t item[16];
            swprintf(item, 16, L"%u", fps);
            SendMessageW(_fpsCombo, CB_ADDSTRING, 0, (LPARAM)item);
        }
    }

    void SimWindow::drainConsole() noexcept
    {
        if (!_runtime)
            return;
        static std::vector<ConsoleLine> batch;
        batch.clear();
        _runtime->drainConsoleLines(batch);
        for (const ConsoleLine &line : batch)
            appendConsoleLine(line);
    }

    void SimWindow::appendConsoleLine(const ConsoleLine &line) noexcept
    {
        _console.push_back(line);
        if (_console.size() > 4000U)
        {
            _console.erase(_console.begin(), _console.begin() + 1000);
            rebuildConsoleView();
            return;
        }

        if (static_cast<int>(line.level) < _viewLevel)
            return;
        char prefix[8];
        std::snprintf(prefix, sizeof(prefix), "[%c] ", levelTag(line.level));
        const std::wstring text = toWide(prefix) + toWide(line.text.c_str()) + L"\r";
        appendLogChunk(text.c_str(), levelColor(line.level));
    }

    void SimWindow::rebuildConsoleView() noexcept
    {
        SetWindowTextW(_log, L"");
        for (const ConsoleLine &line : _console)
        {
            if (static_cast<int>(line.level) < _viewLevel)
                continue;
            char prefix[8];
            std::snprintf(prefix, sizeof(prefix), "[%c] ", levelTag(line.level));
            const std::wstring text = toWide(prefix) + toWide(line.text.c_str()) + L"\r";
            appendLogChunk(text.c_str(), levelColor(line.level));
        }
    }

    void SimWindow::appendLogChunk(const wchar_t *text, unsigned color) noexcept
    {
        if (!_log || !text || !*text)
            return;

        const int start = GetWindowTextLengthW(_log);
        if (_logIsRich)
        {
            CHARRANGE cr{};
            cr.cpMin = cr.cpMax = start;
            SendMessageW(_log, EM_EXSETSEL, 0, (LPARAM)&cr);
        }
        else
        {
            SendMessageW(_log, EM_SETSEL, start, start);
        }
        SendMessageW(_log, EM_REPLACESEL, FALSE, (LPARAM)text);

        if (_logIsRich)
        {
            const int end = GetWindowTextLengthW(_log);
            CHARRANGE cr{};
            cr.cpMin = start;
            cr.cpMax = end;
            SendMessageW(_log, EM_EXSETSEL, 0, (LPARAM)&cr);
            CHARFORMAT2W cf{};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_COLOR;
            cf.crTextColor = static_cast<COLORREF>(color);
            SendMessageW(_log, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            cr.cpMin = cr.cpMax = end;
            SendMessageW(_log, EM_EXSETSEL, 0, (LPARAM)&cr);
        }
    }

    void SimWindow::syncControls() noexcept
    {
        if (!_runtime || !_hwnd)
            return;
        Runtime &rt = *_runtime;

        if (_timeSlider)
            SendMessageW(_timeSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(rt.timeScalePercent()));
        if (_timeValue)
        {
            wchar_t buf[32];
            swprintf(buf, 32, L"%u%%", rt.timeScalePercent());
            SetWindowTextW(_timeValue, buf);
        }
        if (_fpsCombo)
        {
            uint32_t selection = 0;
            for (uint32_t i = 0; i < std::size(kFpsChoices); ++i)
                if (kFpsChoices[i] == rt.fpsLimit())
                    selection = i;
            SendMessageW(_fpsCombo, CB_SETCURSEL, selection, 0);
        }
        if (_stepEdit)
            setEditText(_stepEdit, std::to_string(rt.frameStepCount()));

        const SimPaths &paths = rt.paths();
        setWindowText(_setupProjectLabel, !paths.projectRoot.empty() ? paths.projectRoot.c_str()
                                                                     : simtext::tr(simtext::Id::SetupNotSet));
        setWindowText(_setupKernelLabel, !paths.kernelDir.empty() ? paths.kernelDir.c_str()
                                                                  : simtext::tr(simtext::Id::SetupNotSet));
        setWindowText(_setupAppLabel, !paths.appDir.empty() ? paths.appDir.c_str()
                                                            : simtext::tr(simtext::Id::SetupNotSet));

        setWindowText(_tbPause, rt.isPaused() ? simtext::tr(simtext::Id::Resume) : simtext::tr(simtext::Id::Pause));
        setWindowText(_tbRecord, rt.isRecording() ? simtext::tr(simtext::Id::Stop) : simtext::tr(simtext::Id::Record));

        if (rt.isRecording())
        {
            const uint64_t elapsed = rt.recordingElapsedUs();
            const uint64_t totalSeconds = elapsed / 1'000'000ULL;
            wchar_t rec[32];
            swprintf(rec, 32, L"REC %02llu:%02llu:%02llu",
                     static_cast<unsigned long long>(totalSeconds / 3600ULL),
                     static_cast<unsigned long long>((totalSeconds / 60ULL) % 60ULL),
                     static_cast<unsigned long long>(totalSeconds % 60ULL));
            _statusRec = rec;
        }
        else
        {
            _statusRec.clear();
        }
        if (_status)
            InvalidateRect(_status, nullptr, FALSE);

        applySetupVisibility();

        if (_metrics)
            InvalidateRect(_metrics, nullptr, FALSE);
    }

    void SimWindow::applyLanguage() noexcept
    {
        if (!_hwnd || !_runtime)
            return;

        setWindowText(_stepLabel, simtext::tr(simtext::Id::StepFrames));
        setWindowText(_timeLabel, simtext::tr(simtext::Id::TimeScale));
        setWindowText(_fpsLabel, simtext::tr(simtext::Id::FpsLimit));
        setWindowText(_logFileCheck, simtext::tr(simtext::Id::LogToFile));
        setWindowText(_clearBtn, simtext::tr(simtext::Id::Clear));
        setWindowText(_copyBtn, simtext::tr(simtext::Id::Copy));
        setWindowText(_setupProjectBtn, simtext::tr(simtext::Id::SetupBrowse));
        setWindowText(_setupKernelBtn, simtext::tr(simtext::Id::SetupBrowse));
        setWindowText(_setupAppBtn, simtext::tr(simtext::Id::SetupBrowse));
        setWindowText(_setupSaveBtn, simtext::tr(simtext::Id::SetupSave));
        setWindowText(_sectionRuntime, simtext::tr(simtext::Id::SectionEmulation));
        setWindowText(_setupSection, simtext::tr(simtext::Id::SetupSection));
        setWindowText(_setupProjectName, simtext::tr(simtext::Id::SetupProject));
        setWindowText(_setupKernelName, simtext::tr(simtext::Id::SetupKernel));
        setWindowText(_setupAppName, simtext::tr(simtext::Id::SetupApp));
        setWindowText(_tbPause, _runtime->isPaused() ? simtext::tr(simtext::Id::Resume) : simtext::tr(simtext::Id::Pause));
        setWindowText(_tbBack, simtext::tr(simtext::Id::StepBack));
        setWindowText(_tbFwd, simtext::tr(simtext::Id::StepFwd));
        setWindowText(_tbShot, simtext::tr(simtext::Id::Screenshot));
        setWindowText(_tbRecord,
                      _runtime->isRecording() ? simtext::tr(simtext::Id::Stop) : simtext::tr(simtext::Id::Record));
        fillLevelCombo();
        fillFpsCombo();
        buildMenu();
        SetWindowTextW(_hwnd, toWide(_runtime->windowTitle()).c_str());

        InvalidateRect(_hwnd, nullptr, TRUE);
        syncControls();
        layoutChildren();
    }

    void SimWindow::applySetupVisibility() noexcept
    {
        if (!_hwnd || !_runtime)
            return;
        const BOOL setup = _runtime->setupMode() ? TRUE : FALSE;
        for (HWND h : {_tbPause, _tbBack, _tbFwd, _tbShot, _tbRecord, _sectionRuntime, _stepLabel, _stepEdit, _stepSpin,
                       _timeLabel, _timeSlider, _timeValue, _fpsLabel, _fpsCombo})
            if (h)
                ShowWindow(h, setup ? SW_HIDE : SW_SHOW);
        for (HWND h : {_setupSection, _setupProjectName, _setupKernelName, _setupAppName, _setupProjectLabel,
                       _setupKernelLabel, _setupAppLabel, _setupProjectBtn, _setupKernelBtn, _setupAppBtn,
                       _setupSaveBtn})
            if (h)
                ShowWindow(h, setup ? SW_SHOW : SW_HIDE);
    }

    void SimWindow::setStatusText(const char *text) noexcept
    {
        _statusMsg = text ? text : "";
        if (_status)
            InvalidateRect(_status, nullptr, FALSE);
    }

    void SimWindow::invalidateCanvas() noexcept
    {
        if (_canvas)
            InvalidateRect(_canvas, nullptr, FALSE);
        if (_metrics)
            InvalidateRect(_metrics, nullptr, FALSE);
        if (_status)
            InvalidateRect(_status, nullptr, FALSE);
    }

    void SimWindow::syncNativeSize() noexcept
    {
        if (_hwnd)
        {
            layoutChildren();
            InvalidateRect(_canvas, nullptr, TRUE);
        }
    }

    void SimWindow::reloadFonts() noexcept
    {
        if (_uiFont)
            DeleteObject(_uiFont);
        if (_uiBoldFont)
            DeleteObject(_uiBoldFont);
        if (_monoFont)
            DeleteObject(_monoFont);
        if (_setupTitleFont)
            DeleteObject(_setupTitleFont);
        _uiFont = loadUiFont(s(15), false, FW_NORMAL);
        _uiBoldFont = loadUiFont(s(12), false, FW_SEMIBOLD);
        _monoFont = loadUiFont(s(14), true, FW_NORMAL);
        _setupTitleFont = loadUiFont(s(26), false, FW_SEMIBOLD);
    }

    void SimWindow::applyFonts() noexcept
    {
        if (!_hwnd)
            return;
        for (HWND child :
             {_tbPause, _tbBack, _tbFwd, _tbShot, _tbRecord, _sectionRuntime, _stepLabel, _timeLabel, _timeValue,
              _fpsLabel, _stepEdit, _timeSlider, _fpsCombo, _levelCombo, _logFileCheck, _clearBtn, _copyBtn, _log,
              _setupSection, _setupProjectName, _setupKernelName, _setupAppName, _setupProjectLabel,
              _setupKernelLabel, _setupAppLabel, _setupProjectBtn, _setupKernelBtn, _setupAppBtn, _setupSaveBtn})
            SendMessageW(child, WM_SETFONT, (WPARAM)_uiFont, TRUE);
        SendMessageW(_sectionRuntime, WM_SETFONT, (WPARAM)_uiBoldFont, TRUE);
        SendMessageW(_setupSection, WM_SETFONT, (WPARAM)_uiBoldFont, TRUE);
        if (_tooltip)
            SendMessageW(_tooltip, WM_SETFONT, (WPARAM)_uiFont, TRUE);
    }

    void SimWindow::makeTooltip(HWND target, const wchar_t *text) noexcept
    {
        if (!_tooltip || !target)
            return;
        TOOLINFOW ti = {};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
        ti.hwnd = _hwnd;
        ti.uId = (UINT_PTR)target;
        ti.hinst = nullptr;
        ti.lpszText = const_cast<LPWSTR>(text);
        SendMessageW(_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }

    HFONT SimWindow::loadUiFont(int pixelHeight, bool mono, int weight) noexcept
    {
        if (mono)
        {
            const wchar_t *face = fontFaceInstalled(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
            return CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                               face);
        }
        const wchar_t *face =
            fontFaceInstalled(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
        return CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    }

    void SimWindow::layoutChildren() noexcept
    {
        if (!_hwnd || !_runtime)
            return;
        Runtime &rt = *_runtime;

        RECT rc = {};
        GetClientRect(_hwnd, &rc);
        const int clientW = rc.right - rc.left;
        const int clientH = rc.bottom - rc.top;
        if (clientW <= 0 || clientH <= 0)
            return;

        auto moveWnd = [](HWND h, int x, int y, int w, int hgt)
        {
            RECT r = {};
            GetWindowRect(h, &r);
            MapWindowPoints(nullptr, GetParent(h), (LPPOINT)&r, 2);
            if (r.left != x || r.top != y || (r.right - r.left) != w || (r.bottom - r.top) != hgt)
                MoveWindow(h, x, y, w, hgt, FALSE);
        };

        const int margin = s(12);
        const int statusH = s(26);
        const int logH = s(150);
        const int barH = s(32);
        const int metricsH = s(44);
        const int panelW = s(240);

        const int logTop = clientH - statusH - s(8) - logH;
        const int barTop = logTop - barH;
        const int workBottom = barTop - s(10);
        const int workTop = margin;

        const int panelX = clientW - margin - panelW;

        HDC measure = GetDC(_hwnd);
        HFONT oldMeasureFont = (HFONT)SelectObject(measure, _uiFont);
        auto textWidth = [&](const char *utf8)
        {
            const std::wstring wide = toWide(utf8);
            SIZE sz = {};
            GetTextExtentPoint32W(measure, wide.c_str(), (int)wide.size(), &sz);
            return sz.cx;
        };

        const int pauseW = std::max(textWidth(simtext::tr(simtext::Id::Pause)),
                                    textWidth(simtext::tr(simtext::Id::Resume))) +
                           s(28);
        const int backW = textWidth(simtext::tr(simtext::Id::StepBack)) + s(28);
        const int fwdW = textWidth(simtext::tr(simtext::Id::StepFwd)) + s(28);
        const int shotW = textWidth(simtext::tr(simtext::Id::Screenshot)) + s(28);
        const int recW = std::max(textWidth(simtext::tr(simtext::Id::Record)),
                                  textWidth(simtext::tr(simtext::Id::Stop))) +
                         s(28);
        const int clearW = textWidth(simtext::tr(simtext::Id::Clear)) + s(24);
        const int copyW = textWidth(simtext::tr(simtext::Id::Copy)) + s(24);

        SelectObject(measure, oldMeasureFont);
        ReleaseDC(_hwnd, measure);

        moveWnd(_levelCombo, margin, barTop + (barH - s(24)) / 2, s(150), s(200));
        moveWnd(_logFileCheck, margin + s(160), barTop + (barH - s(18)) / 2, s(120), s(18));
        moveWnd(_clearBtn, clientW - margin - clearW - copyW - s(6), barTop + (barH - s(28)) / 2, clearW, s(28));
        moveWnd(_copyBtn, clientW - margin - copyW, barTop + (barH - s(28)) / 2, copyW, s(28));
        moveWnd(_log, margin, logTop, clientW - 2 * margin, logH);

        const int canvasW = std::max(1, static_cast<int>(rt.width()) * static_cast<int>(rt.scale()));
        const int canvasH = std::max(1, static_cast<int>(rt.height()) * static_cast<int>(rt.scale()));
        const int leftW = std::max(1, panelX - s(16) - margin);
        const int availH = std::max(1, workBottom - workTop - metricsH - s(6));
        const int drawW = std::min(canvasW, leftW);
        const int drawH = std::min(canvasH, availH);
        const int canvasX = margin + std::max(0, (leftW - drawW) / 2);
        moveWnd(_canvas, canvasX, workTop, drawW, drawH);
        moveWnd(_metrics, canvasX, workTop + drawH + s(6), drawW, metricsH);
        moveWnd(_status, 0, clientH - statusH, clientW, statusH);

        const int pw = panelW;
        int y = workTop;
        if (_runtime->setupMode())
        {
            moveWnd(_setupSection, panelX, y, pw, s(18));
            y += s(30);
            auto placeRow = [&](HWND caption, HWND label, HWND btn)
            {
                moveWnd(caption, panelX, y, pw, s(16));
                moveWnd(label, panelX, y + s(20), pw - s(94), s(20));
                moveWnd(btn, panelX + pw - s(88), y + s(16), s(88), s(28));
                y += s(52);
            };
            placeRow(_setupProjectName, _setupProjectLabel, _setupProjectBtn);
            placeRow(_setupKernelName, _setupKernelLabel, _setupKernelBtn);
            placeRow(_setupAppName, _setupAppLabel, _setupAppBtn);
            y += s(8);
            moveWnd(_setupSaveBtn, panelX, y, pw, s(32));
        }
        else
        {
            moveWnd(_tbPause, panelX, y, pw, s(32));
            y += s(40);
            const int halfW = (pw - s(8)) / 2;
            moveWnd(_tbBack, panelX, y, std::max(halfW, backW), s(32));
            moveWnd(_tbFwd, panelX + pw - std::max(halfW, fwdW), y, std::max(halfW, fwdW), s(32));
            y += s(40);
            moveWnd(_tbShot, panelX, y, std::max(halfW, shotW), s(32));
            moveWnd(_tbRecord, panelX + pw - std::max(halfW, recW), y, std::max(halfW, recW), s(32));
            y += s(48);
            moveWnd(_sectionRuntime, panelX, y, pw, s(18));
            y += s(30);
            moveWnd(_stepLabel, panelX, y, s(110), s(18));
            moveWnd(_stepEdit, panelX + pw - s(56), y - s(4), s(56), s(24));
            moveWnd(_stepSpin, panelX + pw - s(18), y - s(4), s(18), s(24));
            y += s(34);
            moveWnd(_timeLabel, panelX, y, s(100), s(18));
            moveWnd(_timeValue, panelX + pw - s(48), y, s(48), s(18));
            y += s(24);
            moveWnd(_timeSlider, panelX, y, pw, s(28));
            y += s(36);
            moveWnd(_fpsLabel, panelX, y, s(100), s(18));
            moveWnd(_fpsCombo, panelX + pw - s(110), y - s(4), s(110), s(200));
        }
    }

    void SimWindow::browseSetup(int which) noexcept
    {
        if (!_runtime)
            return;
        const char *title = (which == 0)   ? simtext::tr(simtext::Id::SetupProject)
                            : (which == 1) ? simtext::tr(simtext::Id::SetupKernel)
                                           : simtext::tr(simtext::Id::SetupApp);
        const std::string picked = pickFolder(_hwnd, title);
        if (picked.empty())
        {
            setStatusText(simtext::tr(simtext::Id::SetupPickCancel));
            return;
        }

        bool ok = false;
        switch (which)
        {
        case 0:
            ok = _runtime->applySetupPaths(picked.c_str(), nullptr, nullptr);
            break;
        case 1:
            ok = _runtime->applySetupPaths(nullptr, picked.c_str(), nullptr);
            break;
        default:
            ok = _runtime->applySetupPaths(nullptr, nullptr, picked.c_str());
            break;
        }

        if (ok)
            (void)_runtime->saveAndRestartSim();
        else
            syncControls();
    }

    LRESULT SimWindow::frameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept
    {
        switch (msg)
        {
        case WM_SIZE:
            layoutChildren();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED:
        {
            _dpi = windowDpi(hwnd);
            reloadFonts();
            applyFonts();
            auto *suggested = reinterpret_cast<RECT *>(lp);
            MoveWindow(hwnd, suggested->left, suggested->top, suggested->right - suggested->left,
                       suggested->bottom - suggested->top, TRUE);
            layoutChildren();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto *info = reinterpret_cast<MINMAXINFO *>(lp);
            info->ptMinTrackSize.x = s(800);
            info->ptMinTrackSize.y = s(620);
            return 0;
        }
        case WM_CLOSE:
            if (_runtime && !_runtime->isRestartRequested())
                _runtime->markUserExit();
            if (_runtime)
                _runtime->requestQuit();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            _hwnd = nullptr;
            PostQuitMessage(0);
            return 0;
        case WM_COMMAND:
        {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (!_runtime)
                break;
            Runtime &rt = *_runtime;
            switch (id)
            {
            case kIdMenuExit:
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            case kIdLangEn:
            case kIdLangRu:
                if (code == 0)
                    rt.setLanguage(id == kIdLangRu ? simtext::Lang::Russian : simtext::Lang::English);
                return 0;
            case kIdPause:
                if (code == BN_CLICKED || code == 0)
                {
                    rt.setPaused(!rt.isPaused());
                    setStatusText(rt.isPaused() ? simtext::tr(simtext::Id::StatusPaused)
                                                : simtext::tr(simtext::Id::StatusRunning));
                    syncControls();
                }
                SetFocus(_canvas);
                return 0;
            case kIdBack:
                if (code == BN_CLICKED || code == 0)
                    rt.stepBack();
                SetFocus(_canvas);
                return 0;
            case kIdForward:
                if (code == BN_CLICKED || code == 0)
                    rt.stepFrame();
                SetFocus(_canvas);
                return 0;
            case kIdShot:
                if (code == BN_CLICKED || code == 0)
                    (void)rt.saveScreenshot();
                SetFocus(_canvas);
                return 0;
            case kIdRecord:
                if (code == BN_CLICKED || code == 0)
                    (void)rt.toggleRecording();
                SetFocus(_canvas);
                return 0;
            case kIdRestart:
                if (code == BN_CLICKED || code == 0)
                    (void)rt.restartProcess();
                SetFocus(_canvas);
                return 0;
            case kIdStepEdit:
                if (code == EN_CHANGE || code == EN_KILLFOCUS)
                    rt.setFrameStepCount(static_cast<uint32_t>(std::max(1, std::atoi(getEditText(_stepEdit).c_str()))));
                return 0;
            case kIdFpsCombo:
                if (code == CBN_SELCHANGE)
                {
                    const int sel = static_cast<int>(SendMessageW(_fpsCombo, CB_GETCURSEL, 0, 0));
                    if (sel >= 0 && sel < static_cast<int>(std::size(kFpsChoices)))
                        rt.setFpsLimit(kFpsChoices[sel]);
                    syncControls();
                }
                return 0;
            case kIdLogFile:
                if (code == BN_CLICKED)
                {
                    const bool want = SendMessageW(_logFileCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    if (!rt.setLogToFile(want))
                    {
                        SendMessageW(_logFileCheck, BM_SETCHECK, BST_UNCHECKED, 0);
                        setStatusText(simtext::tr(simtext::Id::CannotOpenLog));
                    }
                    else
                    {
                        setStatusText(want ? simtext::tr(simtext::Id::LoggingToFile)
                                           : simtext::tr(simtext::Id::FileLoggingOff));
                    }
                }
                return 0;
            case kIdLevelCombo:
                if (code == CBN_SELCHANGE)
                {
                    const int sel = static_cast<int>(SendMessageW(_levelCombo, CB_GETCURSEL, 0, 0));
                    if (sel >= 0)
                    {
                        _viewLevel = sel;
                        rebuildConsoleView();
                    }
                }
                return 0;
            case kIdClear:
                _console.clear();
                SetWindowTextW(_log, L"");
                return 0;
            case kIdCopy:
            {
                const LONG len = std::min<LONG>(GetWindowTextLengthW(_log), 4'000'000L);
                if (len > 0 && OpenClipboard(hwnd))
                {
                    std::wstring wide(static_cast<size_t>(len) + 1U, L'\0');
                    GetWindowTextW(_log, wide.data(), len + 1);
                    const std::string text = toUtf8(wide.c_str());
                    const SIZE_T bytes = text.size() + 1U;
                    EmptyClipboard();
                    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes))
                    {
                        if (void *data = GlobalLock(mem))
                        {
                            std::memcpy(data, text.c_str(), bytes);
                            GlobalUnlock(mem);
                            SetClipboardData(CF_TEXT, mem);
                        }
                    }
                    CloseClipboard();
                }
                return 0;
            }
            case kIdSetupProject:
                if (code == BN_CLICKED || code == 0)
                    browseSetup(0);
                return 0;
            case kIdSetupKernel:
                if (code == BN_CLICKED || code == 0)
                    browseSetup(1);
                return 0;
            case kIdSetupApp:
                if (code == BN_CLICKED || code == 0)
                    browseSetup(2);
                return 0;
            case kIdSetupSave:
                if (code == BN_CLICKED)
                    (void)rt.saveAndRestartSim();
                return 0;
            default:
                break;
            }
            break;
        }
        case WM_CTLCOLORBTN:
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        case WM_HSCROLL:
        {
            if (!_runtime)
                break;
            if ((HWND)lp == _timeSlider)
            {
                _runtime->setTimeScalePercent(static_cast<uint32_t>(SendMessageW(_timeSlider, TBM_GETPOS, 0, 0)));
                syncControls();
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            if (!_runtime)
                break;
            const bool down = (msg == WM_KEYDOWN);
            if (down)
            {
                if (wp == VK_F6)
                {
                    _runtime->setPaused(!_runtime->isPaused());
                    syncControls();
                    return 0;
                }
                if (wp == VK_F12)
                {
                    (void)_runtime->saveScreenshot();
                    return 0;
                }
            }
            switch (static_cast<int>(wp))
            {
            case VK_UP:
                _runtime->handleKey(SimKey::Up, down);
                return 0;
            case VK_DOWN:
                _runtime->handleKey(SimKey::Down, down);
                return 0;
            case VK_LEFT:
                _runtime->handleKey(SimKey::Left, down);
                return 0;
            case VK_RIGHT:
                _runtime->handleKey(SimKey::Right, down);
                return 0;
            case VK_RETURN:
            case VK_SPACE:
                _runtime->handleKey(SimKey::Select, down);
                return 0;
            case VK_F1:
                _runtime->handleKey(SimKey::F1, down);
                return 0;
            case VK_F2:
                _runtime->handleKey(SimKey::F2, down);
                return 0;
            case VK_F3:
                _runtime->handleKey(SimKey::F3, down);
                return 0;
            case VK_F5:
                _runtime->handleKey(SimKey::F5, down);
                return 0;
            case VK_F9:
                _runtime->handleKey(SimKey::F9, down);
                return 0;
            default:
                break;
            }
            break;
        }
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT SimWindow::canvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            presentCanvas(hdc);
            drawSetupOverlay(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            if (_runtime)
            {
                int x = 0, y = 0;
                mapTouch(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), x, y);
                _runtime->injectTouch(true, x, y);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (GetCapture() == hwnd && _runtime)
            {
                int x = 0, y = 0;
                mapTouch(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), x, y);
                _runtime->injectTouch(true, x, y);
            }
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == hwnd)
                ReleaseCapture();
            if (_runtime)
            {
                int x = 0, y = 0;
                mapTouch(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), x, y);
                _runtime->injectTouch(false, x, y);
            }
            return 0;
        default:
            return frameProc(hwnd, msg, wp, lp);
        }
    }

    void SimWindow::mapTouch(int clientX, int clientY, int &outX, int &outY) const noexcept
    {
        if (!_runtime)
        {
            outX = outY = 0;
            return;
        }
        Runtime &rt = *_runtime;
        RECT rc = {};
        GetClientRect(_canvas, &rc);
        const int cw = rc.right - rc.left;
        const int ch = rc.bottom - rc.top;

        const double sx = static_cast<double>(cw) / static_cast<double>(rt.width());
        const double sy = static_cast<double>(ch) / static_cast<double>(rt.height());
        const double scale = std::max(1.0, std::min(static_cast<double>(rt.scale()), std::min(sx, sy)));
        const int outW = std::max(1, static_cast<int>(static_cast<double>(rt.width()) * scale));
        const int outH = std::max(1, static_cast<int>(static_cast<double>(rt.height()) * scale));
        const int startX = (cw - outW) / 2;
        const int startY = (ch - outH) / 2;

        outX = static_cast<int>((clientX - startX) / scale);
        outY = static_cast<int>((clientY - startY) / scale);
    }

    void SimWindow::presentCanvas(HDC hdc) noexcept
    {
        if (!_runtime || !_runtime->framebuffer() || _runtime->width() == 0 || _runtime->height() == 0)
            return;
        Runtime &rt = *_runtime;

        RECT rc = {};
        GetClientRect(_canvas, &rc);
        const int cw = rc.right - rc.left;
        const int ch = rc.bottom - rc.top;

        static HDC memDC = nullptr;
        static HBITMAP memBmp = nullptr;
        static HBITMAP memOld = nullptr;
        static int memW = 0;
        static int memH = 0;
        if (!memDC)
            memDC = CreateCompatibleDC(nullptr);
        if (memW != cw || memH != ch)
        {
            if (memOld)
                SelectObject(memDC, memOld);
            memOld = nullptr;
            if (memBmp)
                DeleteObject(memBmp);
            memBmp = CreateCompatibleBitmap(hdc, cw, ch);
            memOld = static_cast<HBITMAP>(SelectObject(memDC, memBmp));
            memW = cw;
            memH = ch;
        }
        HDC &target = memDC;

        static std::vector<uint32_t> bgra;
        const size_t pixels = static_cast<size_t>(rt.width()) * static_cast<size_t>(rt.height());
        bgra.resize(pixels);
        const uint32_t *src = rt.framebuffer();
        for (size_t i = 0; i < pixels; ++i)
        {
            const uint32_t argb = src[i];
            bgra[i] = 0xFF000000u | ((argb & 0xFFu) << 16) | (argb & 0xFF00u) | ((argb >> 16) & 0xFFu);
        }

        const double sx = static_cast<double>(cw) / static_cast<double>(rt.width());
        const double sy = static_cast<double>(ch) / static_cast<double>(rt.height());
        const double scale = std::max(1.0, std::min(static_cast<double>(rt.scale()), std::min(sx, sy)));
        const int outW = std::max(1, static_cast<int>(static_cast<double>(rt.width()) * scale));
        const int outH = std::max(1, static_cast<int>(static_cast<double>(rt.height()) * scale));
        const int outX = (cw - outW) / 2;
        const int outY = (ch - outH) / 2;

        FillRect(target, &rc, _canvasBrush);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(rt.width());
        bmi.bmiHeader.biHeight = -static_cast<LONG>(rt.height());
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(target, COLORONCOLOR);
        StretchDIBits(target, outX, outY, outW, outH, 0, 0, static_cast<int>(rt.width()), static_cast<int>(rt.height()),
                      bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

        BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);
    }

    void SimWindow::drawSetupOverlay(HDC hdc) noexcept
    {
        if (!_runtime || !_runtime->setupMode())
            return;
        RECT rc = {};
        GetClientRect(_canvas, &rc);
        const int cw = rc.right - rc.left;
        const int ch = rc.bottom - rc.top;
        FillRect(hdc, &rc, _canvasBrush);
        const int innerW = std::max(80, cw - 80);
        const std::wstring line1 = toWide(simtext::tr(simtext::Id::SetupTitle));
        const std::wstring line2 = toWide(simtext::tr(simtext::Id::SetupHint));

        SetBkMode(hdc, TRANSPARENT);
        HFONT old = (HFONT)SelectObject(hdc, _setupTitleFont);

        RECT r1 = {40, 0, 40 + innerW, 0};
        SetTextColor(hdc, RGB(24, 24, 24));
        DrawTextW(hdc, line1.c_str(), -1, &r1, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
        RECT r2 = {40, 0, 40 + innerW, 0};
        SelectObject(hdc, _uiFont);
        SetTextColor(hdc, RGB(96, 96, 96));
        DrawTextW(hdc, line2.c_str(), -1, &r2, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);

        const int h1 = r1.bottom - r1.top;
        const int h2 = r2.bottom - r2.top;
        const int top = std::max(8, (ch - h1 - 14 - h2) / 2);

        r1 = {40, top, 40 + innerW, top + h1};
        r2 = {40, top + h1 + 14, 40 + innerW, top + h1 + 14 + h2};

        SelectObject(hdc, _setupTitleFont);
        SetTextColor(hdc, RGB(24, 24, 24));
        DrawTextW(hdc, line1.c_str(), -1, &r1, DT_CENTER | DT_WORDBREAK);
        SelectObject(hdc, _uiFont);
        SetTextColor(hdc, RGB(96, 96, 96));
        DrawTextW(hdc, line2.c_str(), -1, &r2, DT_CENTER | DT_WORDBREAK);
        SelectObject(hdc, old);
    }

    LRESULT SimWindow::metricsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            if (!_runtime)
                break;
            Runtime &rt = *_runtime;
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            const int width = rc.right - rc.left;
            const int height = rc.bottom - rc.top;
            FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));

            const uint32_t heapKb = (rt.simHeapBytes() + 1023U) / 1024U;
            const uint32_t heapPeakKb = (rt.simHeapPeakBytes() + 1023U) / 1024U;
            const uint32_t heapBudgetKb = Runtime::simHeapBudgetBytes() / 1024U;

            wchar_t values[3][48];
            if (rt.presentFps() > 0.05F)
                swprintf(values[0], 48, L"%.1f", static_cast<double>(rt.presentFps()));
            else
                wcscpy(values[0], L"-");
            swprintf(values[1], 48, L"%.2f ms", static_cast<double>(rt.lastRenderCpuUs()) / 1000.0);
            swprintf(values[2], 48, L"%u / %u KB", heapKb, heapBudgetKb);

            const char *captions[3] = {
                simtext::tr(simtext::Id::MetricFps),
                simtext::tr(simtext::Id::MetricCpu),
                simtext::tr(simtext::Id::MetricHeap),
            };

            const int cellCount = 3;
            const int cellW = std::max(1, width / cellCount);

            SetBkMode(hdc, TRANSPARENT);
            for (int i = 0; i < cellCount; ++i)
            {
                const int x = i * cellW + s(12);

                HFONT oldFont = (HFONT)SelectObject(hdc, _uiBoldFont);
                SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
                const std::wstring caption = toWide(captions[i]);
                TextOutW(hdc, x, s(5), caption.c_str(), static_cast<int>(caption.size()));

                SelectObject(hdc, _monoFont);
                SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                TextOutW(hdc, x, s(20), values[i], (int)wcslen(values[i]));

                if (i == 2)
                {
                    SIZE valueSize = {};
                    GetTextExtentPoint32W(hdc, values[i], (int)wcslen(values[i]), &valueSize);
                    const int barX = x + valueSize.cx + s(10);
                    const int barW = std::max(s(20), i * cellW + cellW - s(16) - barX);
                    const int barY = s(24);
                    const double budgetKbF = static_cast<double>(std::max<uint32_t>(1U, heapBudgetKb));
                    const double frac = std::min(1.0, static_cast<double>(heapKb) / budgetKbF);
                    const double peakFrac = std::min(1.0, static_cast<double>(heapPeakKb) / budgetKbF);
                    RECT track{barX, barY, barX + barW, barY + s(6)};
                    FillRect(hdc, &track, (HBRUSH)(COLOR_SCROLLBAR + 1));
                    if (frac > 0.0)
                    {
                        RECT fill{barX, barY, barX + static_cast<int>(barW * frac), barY + s(6)};
                        HBRUSH b = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
                        FillRect(hdc, &fill, b);
                        DeleteObject(b);
                    }
                    if (peakFrac > frac)
                    {
                        RECT tick{barX + static_cast<int>(barW * peakFrac), barY - s(1),
                                  barX + static_cast<int>(barW * peakFrac) + s(2), barY + s(7)};
                        FillRect(hdc, &tick, (HBRUSH)(COLOR_WINDOWTEXT + 1));
                    }
                }
                SelectObject(hdc, oldFont);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT SimWindow::statusProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept
    {
        switch (msg)
        {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            const int width = rc.right - rc.left;
            const int height = rc.bottom - rc.top;
            FillRect(hdc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));

            HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_SCROLLBAR));
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, 0, 0, nullptr);
            LineTo(hdc, width, 0);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);

            HFONT old = (HFONT)SelectObject(hdc, _uiFont);
            SetBkMode(hdc, TRANSPARENT);
            const int pad = s(12);
            const int mid = (height + 1) / 2;

            std::wstring left = toWide(_statusMsg.c_str());
            if (left.empty())
                left = toWide((_runtime && _runtime->isPaused()) ? simtext::tr(simtext::Id::StatusPaused)
                                                                 : simtext::tr(simtext::Id::StatusRunning));
            SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
            TextOutW(hdc, pad, mid - s(8), left.c_str(), (int)left.size());

            int x = width - pad;
            auto drawRight = [&](const std::wstring &text, COLORREF color, int dotRadius = 0, COLORREF dot = 0)
            {
                if (text.empty())
                    return;
                SIZE sz = {};
                GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
                x -= sz.cx;
                SetTextColor(hdc, color);
                TextOutW(hdc, x, mid - s(8), text.c_str(), (int)text.size());
                if (dotRadius > 0)
                {
                    x -= dotRadius * 2 + s(5);
                    HBRUSH b = CreateSolidBrush(dot);
                    HBRUSH ob = (HBRUSH)SelectObject(hdc, b);
                    Ellipse(hdc, x, mid - dotRadius, x + dotRadius * 2, mid + dotRadius);
                    SelectObject(hdc, ob);
                    DeleteObject(b);
                }
                x -= s(18);
            };
            drawRight(_statusRec, RGB(200, 30, 30), s(4), RGB(220, 50, 50));
            if (_runtime)
            {
                wchar_t timePart[32];
                swprintf(timePart, 32, toWide(simtext::tr(simtext::Id::StatusTimeFmt)).c_str(),
                         _runtime->timeScalePercent());
                wchar_t right[128];
                swprintf(right, 128, L"%s    %.0f FPS", timePart, static_cast<double>(_runtime->presentFps()));
                drawRight(right, GetSysColor(COLOR_GRAYTEXT));
            }
            SelectObject(hdc, old);
            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

#endif
