#pragma once

#include "Config.hpp"

#if PIPCORE_TARGET_DESKTOP

#include <cstdint>

namespace pipcore::desktop::simtext
{
    enum class Lang : uint8_t
    {
        English = 0,
        Russian = 1
    };

    enum class Id : uint8_t
    {

        WindowTitleDefault,
        WindowTitleSuffix,
        Pause,
        Resume,
        StepBack,
        StepFwd,
        Screenshot,
        Record,
        Stop,
        Restart,

        SectionEmulation,
        StepFrames,
        TimeScale,
        Unlimited,

        LogToFile,
        Clear,
        Copy,
        LevelTrace,
        LevelDebug,
        LevelInfo,
        LevelWarnings,
        LevelErrors,

        StatusPaused,
        StatusRunning,
        CannotOpenLog,
        LoggingToFile,
        FileLoggingOff,

        MsgScreenshot,
        MsgScreenshotFail,
        MsgRecStart,
        MsgRecNoFfmpeg,

        SetupTitle,
        SetupHint,
        SetupSection,
        SetupProject,
        SetupKernel,
        SetupApp,
        SetupBrowse,
        SetupSave,
        SetupNotSet,
        SetupNotReady,
        SetupConsoleHint,
        SetupSaved,
        SetupPickCancel,
        DialogCancel,
        DialogOpen,

        MenuSim,
        MenuLanguage,
        MenuExit,
        LangEn,
        LangRu,
        FpsLimit,
        MetricFps,
        MetricCpu,
        MetricHeap,
        StatusTimeFmt,
        MsgRecFailCode,
        MsgRecSaved,
        MsgRecNoEncoder
    };

    void setLang(Lang lang) noexcept;
    [[nodiscard]] Lang lang() noexcept;

    [[nodiscard]] const char *langCode(Lang lang) noexcept;

    [[nodiscard]] bool langFromCode(const char *code, Lang &out) noexcept;
    [[nodiscard]] const char *tr(Id id) noexcept;
}

#endif
