#include "Host/SimText.hpp"
#if PIPCORE_TARGET_DESKTOP

#include <atomic>
#include <cstring>

namespace pipcore::desktop::simtext
{
    namespace
    {
        struct Entry
        {
            const char *en;
            const char *ru;
        };

        constexpr Entry kTable[] = {

            {"PipCore Simulator", "Симулятор PipCore"},
            {"Simulator", "Симулятор"},
            {"Pause", "Пауза"},
            {"Resume", "Продолжить"},
            {"< Back", "< Назад"},
            {"Step >", "Шаг >"},
            {"Screenshot", "Скриншот"},
            {"Record", "Запись"},
            {"Stop", "Стоп"},
            {"Restart", "Перезапуск"},

            {"EMULATION", "ЭМУЛЯЦИЯ"},
            {"Step frames", "Кадров за шаг"},
            {"Time scale", "Скорость времени"},
            {"Unlimited", "Без лимита"},

            {"Log to file", "Лог в файл"},
            {"Clear", "Очистить"},
            {"Copy", "Копировать"},
            {"Trace", "Подробно"},
            {"Debug", "Отладка"},
            {"Info", "Инфо"},
            {"Warnings", "Предупреждения"},
            {"Errors", "Ошибки"},

            {"Paused", "Пауза"},
            {"Running", "Работает"},
            {"cannot open simulator.log", "не удалось открыть simulator.log"},
            {"logging to Build/simulator.log", "лог пишется в Build/simulator.log"},
            {"file logging off", "запись лога выключена"},

            {"screenshot: %s", "скриншот: %s"},
            {"screenshot failed", "скриншот не удался"},
            {"recording started (%u fps)", "запись начата (%u к/с)"},
            {"recording failed: ffmpeg not found or encoder rejected input",
             "запись не удалась: ffmpeg не найден или кодировщик отклонил поток"},

            {"Firmware app is not configured",
             "Приложение прошивки не настроено"},
            {"Pick the folders in the panel on the right, then press Save & restart",
             "Выберите папки в панели справа и нажмите «Сохранить и перезапуск»"},
            {"PROJECT SETUP", "НАСТРОЙКА ПРОЕКТА"},
            {"Project folder", "Папка проекта"},
            {"Kernel component folder", "Папка компонента ядра"},
            {"Firmware app folder", "Папка кода прошивки"},
            {"Browse...", "Обзор..."},
            {"Save & restart", "Сохранить и перезапуск"},
            {"(not set)", "(не задано)"},
            {"Kernel and app folders must be valid to restart",
             "Папки ядра и прошивки должны быть заданы верно"},
            {"app sources not found - pick folders in the simulator window and press Save & restart",
             "исходники приложения не найдены - выберите папки в окне симулятора и нажмите «Сохранить и перезапуск»"},
            {"paths saved, rebuilding...", "пути сохранены, идёт пересборка..."},
            {"folder selection cancelled", "выбор папки отменён"},
            {"Cancel", "Отмена"},
            {"Open", "Выбрать"}};

        constexpr Entry kTableAppended[] = {
            {"Simulator", "Симулятор"},
            {"Language", "Язык"},
            {"Exit", "Выход"},
            {"English", "Английский"},
            {"Russian", "Русский"},
            {"FPS limit", "Лимит FPS"},
            {"FPS", "FPS"},
            {"CPU", "CPU"},
            {"HEAP", "HEAP"},
            {"time %u%%", "время %u%%"},
            {"recording failed (error 0x%08lX)", "запись не удалась (ошибка 0x%08lX)"},
            {"recording saved: %s", "запись сохранена: %s"},
            {"video encoder is unavailable on this system", "видеокодировщик недоступен в этой системе"}};

        static_assert(sizeof(kTable) / sizeof(kTable[0]) == 46, "kTable rows must match simtext::Id");
        static_assert(sizeof(kTableAppended) / sizeof(kTableAppended[0]) == 13,
                      "kTableAppended rows must match the trailing simtext::Id values");

        std::atomic<uint8_t> g_lang{static_cast<uint8_t>(Lang::English)};
    }

    void setLang(Lang lang) noexcept
    {
        g_lang.store(static_cast<uint8_t>(lang), std::memory_order_relaxed);
    }

    Lang lang() noexcept
    {
        return static_cast<Lang>(g_lang.load(std::memory_order_relaxed));
    }

    const char *langCode(Lang lang) noexcept
    {
        return (lang == Lang::Russian) ? "ru" : "en";
    }

    bool langFromCode(const char *code, Lang &out) noexcept
    {
        if (!code)
            return false;
        if (std::strncmp(code, "ru", 2) == 0)
        {
            out = Lang::Russian;
            return true;
        }
        if (std::strncmp(code, "en", 2) == 0)
        {
            out = Lang::English;
            return true;
        }
        return false;
    }

    const char *tr(Id id) noexcept
    {
        const auto index = static_cast<uint8_t>(id);
        constexpr uint8_t kBaseCount = static_cast<uint8_t>(sizeof(kTable) / sizeof(kTable[0]));
        if (index < kBaseCount)
        {
            const Entry &row = kTable[index];
            return (lang() == Lang::Russian) ? row.ru : row.en;
        }
        constexpr uint8_t kAppendedCount =
            static_cast<uint8_t>(sizeof(kTableAppended) / sizeof(kTableAppended[0]));
        if (index < kBaseCount + kAppendedCount)
        {
            const Entry &row = kTableAppended[index - kBaseCount];
            return (lang() == Lang::Russian) ? row.ru : row.en;
        }
        return "";
    }
}

#endif
