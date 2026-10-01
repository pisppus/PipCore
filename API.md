# PipCore 2.0.0 - API Reference

Модульное HAL-ядро для ESP-IDF: дисплей, графика, ввод, звук, WiFi, OTA, настройки, файловое хранилище и отладка. Каждый модуль включается отдельно - то, что не нужно, не попадает в прошивку.

## Содержание

- [1. Установка](#1-установка)
- [2. Конфигурация (Kconfig)](#2-конфигурация-kconfig)
- [3. Быстрый старт](#3-быстрый-старт)
- [4. Platform - точка входа](#4-platform---точка-входа)
- [5. Display - вывод на панель](#5-display---вывод-на-панель)
- [6. Sprite - offscreen-графика](#6-sprite---offscreen-графика)
- [7. Button - кнопка с дебаунсом](#7-button---кнопка-с-дебаунсом)
- [8. Joystick - аналоговые оси](#8-joystick---аналоговые-оси)
- [9. Touch - ёмкостный тач](#9-touch---ёмкостный-тач)
- [10. Audio - микшер и формат PAC](#10-audio---микшер-и-формат-pac)
- [11. WiFi - STA-подключение](#11-wifi---sta-подключение)
- [12. OTA - обновления прошивки](#12-ota---обновления-прошивки)
- [13. Prefs - постоянные настройки](#13-prefs---постоянные-настройки)
- [14. Storage - файловое хранилище](#14-storage---файловое-хранилище)
- [15. Log - логирование](#15-log---логирование)
- [16. Debug - профайлер и трекер аллокаций](#16-debug---профайлер-и-трекер-аллокаций)
- [17. Симулятор](#17-симулятор)

---

## 1. Установка

**Вариант 1 - из реестра компонентов.** В папке приложения:

```bash
idf.py add-dependency "pisppus/PipCore^2.0.0"
```

Компонент-менеджер сам скачает ядро и его зависимости (например, `joltwallet/littlefs`) при первой сборке - вручную ничего ставить не нужно.

**Вариант 2 - как локальный компонент.** Скопируйте (или подключите сабмодулем) папку `PipCore/` в проект и добавьте её в поиск компонентов в корневом `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/PipCore")
```

Затем подключите компонент в приложении как обычно:

```cmake
idf_component_register(SRCS "main.cpp" REQUIRES PipCore)
```

---

## 2. Конфигурация (Kconfig)

Все опции находятся в меню `PipCore`:

```bash
idf.py menuconfig   →   PipCore
```

По умолчанию **все модули выключены**: ядро собирается «headless» - только `Platform` (время, GPIO, память) и `Log`.

| Опция | Описание | По умолчанию |
|---|---|---|
| `PIPCORE_DISPLAY` | Драйвер панели: `None` / `ST7789` / `ST7796` / `ILI9488` | `None` |
| `PIPCORE_ENABLE_GRAPHICS` | Sprite-графика (RGB565) | on, если выбран драйвер дисплея |
| `PIPCORE_ENABLE_TOUCH` | Ёмкостный тач семейства FT6x36 | off |
| `PIPCORE_ENABLE_AUDIO` | 16-голосный PAC-микшер + I²S-выход | off |
| `PIPCORE_AUDIO_TASK_STACK` | Стек таска микшера, байт (1024…32768) | 4096 |
| `PIPCORE_AUDIO_TASK_PRIO` | Приоритет таска микшера (1…25) | 5 |
| `PIPCORE_AUDIO_TASK_CORE` | Ядро CPU для микшера (0/1). На одноядерных чипах игнорируется | 0 |
| `PIPCORE_ENABLE_WIFI` | WiFi STA (событийная модель) | off |
| `PIPCORE_ENABLE_OTA` | OTA-сервис (манифест + Ed25519 + SHA-256). | off (требует WiFi) |
| `PIPCORE_OTA_PROJECT_URL` | Базовый URL сервера обновлений | `""` |
| `PIPCORE_OTA_HTTP_TIMEOUT_MS` | Таймаут HTTP-запросов OTA (500…30000). | 2000 |
| `PIPCORE_ENABLE_PREFS` | Настройки в NVS | off |
| `PIPCORE_ENABLE_STORAGE` | Файловое хранилище LittleFS | off |
| `PIPCORE_STORAGE_PARTITION_LABEL` | Метка раздела LittleFS | `"storage"` |
| `PIPCORE_LOG_LEVEL` | Стартовый уровень логов (0…5, см. [Log](#15-log---логирование)) | 2 (Info) |
| `PIPCORE_ENABLE_DEBUG` | Трекер аллокаций + профайлер | off |
| `PIPCORE_DEBUG_CONSOLE` | Отладочная консоль для PipCore Inspector. | off (требует Debug)|
| `PIPCORE_DEBUG_CONSOLE_ACCEPT_RISK` | Подтверждение риска неаутентифицированного доступа | off |
| `PIPCORE_DEBUG_CONSOLE_STACK` | Стек таска консоли, байт (1024…32768) | 4096 |
| `PIPCORE_DEBUG_TRANSPORT` | Транспорт консоли: `USB-Serial/JTAG` / `UART0` | USB-Serial/JTAG |

> **Пины и параметры шин Kconfig не задаёт.** Они передаются в рантайме структурами `DisplayConfig`, `TouchConfig`, `SoundConfig`, `net::WifiConfig`.

> **Транспорт консоли.** USB-Serial/JTAG доступен на ESP32 S и C серий; оригинальный ESP32 и ESP32-S2 требуют `UART0`.

### Макросы для условной компиляции

Все опции доступны в коде как макросы `0/1`, поэтому их можно использовать в `#if`:

```cpp
#if PIPCORE_ENABLE_TOUCH
    // код, который нужен только со включённым тачем
#endif
```

| Макрос | Значение |
|---|---|
| `PIPCORE_VERSION` | Версия ядра (`"2.0.0"`) |
| `PIPCORE_TARGET_ESP32` / `PIPCORE_TARGET_DESKTOP` | Целевая платформа: прошивка или [симулятор](#17-симулятор) (ровно один из двух равен 1) |
| `PIPCORE_ENABLE_GRAPHICS`, `_TOUCH`, `_AUDIO`, `_WIFI`, `_OTA`, `_PREFS`, `_STORAGE`, `_DEBUG` | Включён ли соответствующий модуль |
| `PIPCORE_DEBUG_CONSOLE` | Включена ли отладочная консоль |
| `PIPCORE_OTA_PROJECT_URL` | Базовый URL OTA (строковый литерал) |

---

## 3. Быстрый старт

Минимальная программа: экран + заливка цветом. Нужен `PIPCORE_DISPLAY` в `menuconfig`.

```cpp
#include <PipCore.hpp>

using namespace pipcore;

extern "C" void app_main()
{
    Platform *plat = GetPlatform();

    // 1. Описываем дисплей: пины, разрешение, частоту SPI.
    DisplayConfig cfg = {};
    cfg.mosi   = 6;
    cfg.sclk   = 5;
    cfg.cs     = 7;
    cfg.dc     = 8;
    cfg.rst    = -1;          // -1: пин reset не подключён
    cfg.width  = 320;
    cfg.height = 480;
    cfg.hz     = 80'000'000;  // 80 МГц
    cfg.order  = 1;           // BGR-порядок каналов (типично для ST7796)
    cfg.invert = true;

    // 2. Конфигурируем и запускаем. Обе функции возвращают true при успехе.
    if (!plat->configDisplay(cfg) || !plat->beginDisplay(0))
    {
        log::error("display: %s", plat->lastErrorText());
        return;
    }

    // 3. Рисуем.
    plat->display()->fillScreen565(Sprite::color565(0, 8, 16));
}
```

---

## 4. Platform - точка входа

`Platform` - единственный объект, через который прошивка получает доступ к «железу» и ко всем включённым сервисам. Это абстрактный интерфейс: конкретную реализацию (ESP32 или симулятор) возвращает `GetPlatform()`.

```cpp
Platform *plat = GetPlatform();
```

`GetPlatform()` - синглтон, вызывать можно откуда угодно и сколько угодно раз.

### Время и задержки

| Метод | Описание |
|---|---|
| `uint32_t nowMs()` | Монотонное время с запуска, мс. |
| `uint64_t nowUs()` | Монотонное время, мкс |
| `void delayMs(uint32_t ms)` | Пауза без нагрузки на CPU (уступает планировщику FreeRTOS) |
| `bool shouldQuit() const` | Запрос на выход: в симуляторе - закрытие окна, на ESP32 всегда `false` |

### GPIO

| Метод | Описание |
|---|---|
| `void pinModeInput(uint8_t pin, InputMode mode)` | Настроить пин как вход: `InputMode::Floating` / `Pullup` / `Pulldown` |
| `bool digitalRead(uint8_t pin)` | Цифровое чтение входа |
| `int16_t analogRead(uint8_t pin)` | Чтение АЦП (сырой код, 0…4095). ADC-юнит и канал выбираются автоматически по номеру пина (при неверном пине вернёт 0) |

### Память

| Метод | Описание |
|---|---|
| `void *alloc(size_t bytes, AllocCaps caps)` | Аллокация. Вернёт `nullptr` при нехватке памяти. `AllocCaps::PreferInternal` - сначала внутренняя RAM (нужно для DMA-буферов), при нехватке - внешняя |
| `void free(void *ptr)` | Освобождение памяти из `alloc`. `nullptr` безопасен |
| `void *allocAligned(size_t bytes, size_t align, AllocCaps caps)` | Аллокация с выравниванием (`align` меньше `sizeof(void*)` поднимается до него) |
| `void freeAligned(void *ptr)` | Освобождение памяти из `allocAligned` |
| `uint32_t freeHeapTotal()` | Свободно всего, байт |
| `uint32_t freeHeapInternal()` | Свободно во внутренней RAM, байт |
| `uint32_t largestFreeBlock()` | Крупнейший непрерывный блок, байт |
| `uint32_t minFreeHeap()` | Исторический минимум свободной кучи, байт |

Память, выделенную `alloc`, освобождайте только `free`, а из `allocAligned` - только `freeAligned`. Смешивать их нельзя.

```cpp
void *buf = plat->alloc(4096, AllocCaps::PreferInternal); // DMA-буфер
if (!buf)
    return;
// ...
plat->free(buf);
```

### Дисплей

| Метод | Описание |
|---|---|
| `bool configDisplay(const DisplayConfig &cfg)` | Сконфигурировать SPI-транспорт и драйвер панели. Можно вызывать повторно (смена параметров). `false` - если `width`/`height` нулевые или конфигурация не принята драйвером |
| `bool beginDisplay(uint8_t rotation)` | Инициализировать панель, поворот `0..3`. Вызывается после успешного `configDisplay` |
| `bool setDisplayRotation(uint8_t rotation)` | Сменить поворот на лету, без повторной инициализации |
| `Display *display()` | Интерфейс дисплея; `nullptr`, если дисплей не сконфигурирован или не запущен |

### Ошибки

```cpp
if (!plat->configDisplay(cfg))
{
    PlatformError code = plat->lastError();     // enum
    const char *text   = plat->lastErrorText(); // "invalid display config", ...
}
```

| `PlatformError` | Текст | Когда возникает |
|---|---|---|
| `None` | `ok` | Ошибок нет |
| `InvalidDisplayConfig` | `invalid display config` | Нулевое разрешение; вызов `beginDisplay`/`setDisplayRotation` до успешной конфигурации |
| `DisplayConfigureFailed` | `display configure failed` | Драйвер не принял параметры |
| `DisplayBeginFailed` | `display begin failed` | Не удалось выполнить инициализацию панели |
| `DisplayIoFailed` | `display io failed` | Ошибка SPI-передачи (в т. ч. при смене поворота) |

`lastError()` также возвращает `DisplayIoFailed`, если драйвер панели зафиксировал сбой ввода-вывода, даже когда последняя операция платформы прошла успешно. `platformErrorText(PlatformError)` даёт текст для любого значения.

### Сервисы

| Метод | Возвращает |
|---|---|
| `net::Backend *network()` | WiFi-бэкенд или `nullptr`, если модуль выключен |
| `ota::Backend *update()` | OTA-бэкенд или `nullptr` |
| `Touch *touch()` | Тач или `nullptr` |
| `Audio *audio()` | Микшер или `nullptr` |
| `prefs::Backend *prefs()` | Хранилище настроек или `nullptr` |

Для WiFi, OTA, Prefs и Storage обычно удобнее свободные функции (`net::wifiService()`, `prefs::setU8(...)`, `storage::open(...)`) - они сами достают бэкенд из платформы. Прямые указатели нужны, когда хочется хранить интерфейс у себя.

### DisplayConfig

```cpp
struct DisplayConfig
{
    int8_t   mosi = -1;       // пины SPI; -1 = не используется
    int8_t   sclk = -1;
    int8_t   cs   = -1;       // chip select
    int8_t   dc   = -1;       // data/command
    int8_t   rst  = -1;       // аппаратный reset; -1 - не подключён
    uint16_t width  = 0;      // разрешение панели в пикселях (обязательно)
    uint16_t height = 0;
    uint32_t hz = 0;          // частота SPI, Гц
    uint8_t  order = 0;       // порядок каналов в MADCTL: 0 = RGB, 1 = BGR
    bool     invert = true;   // инверсия цвета (нужна большинству IPS-панелей)
    bool     swap = false;    // байтовый своп RGB565 перед отправкой
    int16_t  xOffset = 0;     // сдвиг окна для панелей, у которых контроллер
    int16_t  yOffset = 0;     // больше стекла (например, 240×320 внутри)
};
```

---

## 5. Display - вывод на панель

Низкоуровневый RGB565-интерфейс. В приложении используется через `plat->display()`.

| Метод | Описание |
|---|---|
| `bool begin(uint8_t rotation)` | Инициализация панели (обычно вызывается через `Platform::beginDisplay`) |
| `bool setRotation(uint8_t rotation)` | Поворот `0..3` на лету |
| `uint16_t width() const` / `uint16_t height() const` | Текущее разрешение с учётом поворота |
| `void fillScreen565(uint16_t color565)` | Заливка всего экрана |
| `void writeRect565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels, int32_t stridePixels)` | Вывод прямоугольника. **Синхронно**: возвращается, когда данные уже ушли в панель |
| `void writeRect565Async(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels, int32_t stridePixels)` | То же через DMA: ставит передачу в очередь и возвращается **сразу** |
| `void waitDMA()` | Дождаться завершения асинхронных передач |

### Ключевые понятия

- **`stridePixels`** - шаг строки источника в пикселях. Позволяет выводить произвольное окно из большого буфера кадра: `writeRect565(100, 50, 60, 40, frame, frameWidth)`. Для плотно упакованного источника передайте `w`.
- **Синхронный путь** просто и предсказуемо: вернулись - можно менять буфер.
- **Асинхронный путь** освобождает CPU для отрисовки следующего кадра. Два правила:
  1. буфер-источник должен оставаться валидным до `waitDMA()` (DMA читает его прямо из памяти);
  2. перед повторным использованием буфера вызовите `waitDMA()`.
- При `swap = true` асинхронный путь прозрачно прогоняет данные через двойной DMA-буфер (копирование со свопом): цвета верны, но копирование ложится на CPU.

### Пример

Поставить кадр N в очередь → рисовать кадр N+1 в другой буфер → `waitDMA()` → поменять буферы:

```cpp
Sprite bufA(plat), bufB(plat);
if (!bufA.createSprite(w, h) || !bufB.createSprite(w, h))
    return;

Sprite *front = &bufA;   // его сейчас передаёт DMA
Sprite *back  = &bufB;   // в него рисуем

while (!plat->shouldQuit())
{
    renderScene(*back);                       // CPU рисует, пока DMA занят
    display->waitDMA();                       // предыдущий кадр ушёл
    display->writeRect565Async(0, 0, w, h,
        static_cast<const uint16_t *>(back->getBuffer()), w);
    std::swap(front, back);                   // теперь рисуем в другой буфер
}
```

### Драйверы

| Драйвер | Формат | Макс. частота | Особенности |
|---|---|---|---|
| ST7789 / ST7796 | RGB565 | 80 МГц | Общий шаблонный движок, очередь DMA-транзакций с двойной буферизацией |
| ILI9488 | RGB666 | 60 МГц | Конвейер с конвертацией 565 → 666 |

---

## 6. Sprite - offscreen-графика

Модуль включается опцией `PIPCORE_ENABLE_GRAPHICS` (по умолчанию включён вместе с драйвером дисплея).

`Sprite` - изображение в RAM с клиппингом и быстрым блиттингом. Память выделяется через `Platform::alloc` (внутренняя RAM в приоритете) и освобождается в деструкторе или `deleteSprite()`. Копировать спрайт нельзя, для обмена буферами есть `swap()`.

```cpp
Sprite sp(plat);
if (!sp.createSprite(128, 64))
{
    // не хватило памяти
    return;
}
sp.fillScreen(Sprite::color565(0, 0, 0));
sp.fillRect(10, 10, 40, 20, Sprite::color565(255, 128, 0));
sp.writeToDisplay(*plat->display(), 0, 0, 128, 64);
```

### Создание и служебные методы

| Метод | Описание |
|---|---|
| `Sprite()` / `explicit Sprite(Platform *platform)` | Конструктор. Платформа нужна для выделения памяти - передайте её здесь или через `setPlatform()` |
| `bool createSprite(int16_t w, int16_t h)` | Выделить буфер `w × h`. `false` - не хватило памяти |
| `void deleteSprite()` | Освободить буфер |
| `int16_t width() const` / `int16_t height() const` | Размер |
| `void setPlatform(Platform *platform)` | Сменить платформу-аллокатор |
| `void *getBuffer()` / `const void *getBuffer() const` | Прямой доступ к пикселям (для своих ядер отрисовки). Буфер хранит значения в байтовом порядке, готовом к DMA (после `swap16`) |
| `void swap(Sprite &other)` | Обменять буферы без копирования (удобно для двойной буферизации) |

### Рисование

| Метод | Описание |
|---|---|
| `void fillScreen(uint16_t color565)` | Залить весь спрайт |
| `void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color565)` | Залитый прямоугольник |
| `void drawPixel(int16_t x, int16_t y, uint16_t color565)` | Пиксель. Вне клип-зоны молча игнорируется |
| `void pushImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels565)` | Вставить плотно упакованное изображение |

### Вывод

| Метод | Описание |
|---|---|
| `void writeToDisplay(Display &display, int16_t x, int16_t y, int16_t w, int16_t h) const` | Вывести область на дисплей: `x`, `y` - позиция на дисплее, `w`, `h` - размер области |
| `void pushSprite(Sprite *dst, int16_t x, int16_t y) const` | Спрайт на спрайт (композиция offscreen-сцен) |

### Клиппинг

| Метод | Описание |
|---|---|
| `void setClipRect(int16_t x, int16_t y, int16_t w, int16_t h)` | Зона отсечения: всё вне неё не рисуется. Удобно для окон и скролла |
| `void getClipRect(int32_t *x, int32_t *y, int32_t *w, int32_t *h) const` | Текущая зона |
| `SpriteClip clipRegion(int16_t x, int16_t y, int16_t w, int16_t h) const` | Пересечение прямоугольника с клип-зоной - для собственных циклов отрисовки |

```cpp
struct SpriteClip
{
    int16_t rx1, ry1;   // левый верхний угол видимой части
    int16_t cw, ch;     // её размер
    bool visible;       // false - пересечение пусто
};
```

### Цветовые утилиты

`static constexpr` - вызываются без объекта: `Sprite::color565(...)`.

| Метод | Описание |
|---|---|
| `uint16_t color565(uint8_t r, uint8_t g, uint8_t b)` | RGB888 → RGB565 |
| `uint16_t swap16(uint16_t v)` | Байтовый своп |
| `uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t alpha)` | Альфа-смешение, `alpha` 0…255 (0 - фон, 255 - передний план) |
| `uint8_t u8clamp(int v)` | Ограничение значения диапазоном 0…255 |

---

## 7. Button - кнопка с дебаунсом

Программный дебаунс 12 мс. Активный уровень определяется по `InputMode`: `Pullup` → кнопка замыкает пин на GND (нажатие = низкий уровень). Если платформа не передана, берётся `GetPlatform()`.

```cpp
class Button
{
    explicit Button(uint8_t pin, InputMode pull = InputMode::Pullup);
    Button(Platform *platform, uint8_t pin, InputMode pull = InputMode::Pullup);
};
```

| Метод | Описание |
|---|---|
| `void begin()` | Настроить пин, прочитать начальное состояние, сбросить флаги |
| `void update()` | Опрос + дебаунс. Вызывайте каждый цикл |
| `bool wasPressed()` | `true` один раз на каждое нажатие; флаг сбрасывается чтением |
| `bool isDown() const` | Текущее стабильное состояние (нажата ли кнопка сейчас) |
| `uint8_t pin() const` | Номер пина |
| `InputMode pullMode() const` | Режим подтяжки |

### Пример

```cpp
Button btn(21, InputMode::Pullup);
btn.begin();

while (true)
{
    btn.update();
    if (btn.wasPressed())
        onButton();          // сработает один раз на физическое нажатие
    plat->delayMs(5);
}
```

`Button` + `wasPressed()` - базовый примитив игровых меню. Для удержаний используйте `isDown()` и собственную логику автоповтора. `wasPressed()` срабатывает по нажатию (переход в «нажата»), а не по отпусканию.

---

## 8. Joystick - аналоговые оси

Аналоговая ось с нормализацией, мёртвой зоной, экспоненциальным сглаживанием и опциональной кнопкой.

### Конфигурация

```cpp
struct AnalogAxisConfig
{
    uint8_t pin = 0xFF;        // пин АЦП; 0xFF = ось не используется
    int16_t minValue = 0;      // сырые границы АЦП (калибровка «краёв»)
    int16_t maxValue = 4095;
    float   deadZone = 0.12f;  // мёртвая зона, доля от 1.0
    bool    inverted = false;  // инверсия оси
};

struct JoystickConfig
{
    AnalogAxisConfig axisX;
    AnalogAxisConfig axisY;
    float     deadZone   = 0.12f;               // радиальная мёртвая зона стика
    uint8_t   buttonPin  = 0xFF;                // 0xFF = без кнопки
    InputMode buttonPull = InputMode::Pullup;
};
```

### AnalogAxis

Одна ось. Используйте, если нужна одна ось или нестандартная комбинация.

| Метод | Описание |
|---|---|
| `AnalogAxis(Platform *plat = nullptr, const AnalogAxisConfig &c = {})` | Конструктор |
| `void begin()` | Подготовить ось (для пина `0xFF` ничего не делает) |
| `float update(float deltaTime)` | Прочитать АЦП, обработать, сгладить. Возвращает значение -1.0 … 1.0 |
| `float value() const` | Последнее сглаженное значение |

### Joystick

Две оси и опциональная кнопка.

| Метод | Описание |
|---|---|
| `Joystick(Platform *plat = nullptr, const JoystickConfig &cfg = {})` | Конструктор |
| `void begin()` | Запуск осей и кнопки |
| `void update(float deltaTime)` | Опрос. Вызывайте каждый цикл |
| `float x() const` / `float y() const` | Положение, -1.0 … 1.0, уже со сглаживанием |
| `bool isPressed() const` | Кнопка зажата (без кнопки - всегда `false`) |
| `bool wasPressed()` | Было нажатие с прошлого вызова |

### Пример

```cpp
JoystickConfig jc;
jc.axisX.pin = 1;
jc.axisY.pin = 2;
jc.buttonPin = 3;

Joystick joy(nullptr, jc);
joy.begin();

uint64_t last = plat->nowUs();
while (true)
{
    const uint64_t now = plat->nowUs();
    const float dt = static_cast<float>(now - last) / 1e6f;   // секунды
    last = now;

    joy.update(dt);
    if (joy.wasPressed())
        pauseGame();
    movePlayer(joy.x(), joy.y());
}
```

---

## 9. Touch - ёмкостный тач

Интерфейс ёмкостного тач-контроллера (семейство FT6x36, I²C), до 2 точек. Включается `PIPCORE_ENABLE_TOUCH`. Доступ - `plat->touch()`.

```cpp
struct TouchConfig
{
    int8_t   sda = -1;          // пины I²C
    int8_t   scl = -1;
    int8_t   intr = -1;         // пин прерывания INT - обязателен
                                // configure() отвергает intr < 0

    uint8_t  i2cAddr = 0x38;    // адрес контроллера
    uint32_t freqHz = 400000;   // частота I²C
    uint16_t width = 0;         // разрешение панели - для маппинга координат
    uint16_t height = 0;
    uint8_t  rotation = 0;      // поворот системы координат 0..3
};

struct TouchPoint
{
    uint16_t x = 0;             // в координатах экрана (с учётом поворота)
    uint16_t y = 0;
    uint8_t  id = 0;            // идентификатор касания (для мультитач-трекинга)
    TouchState state = TouchState::Released;

    bool active() const;        // state != Released
};
```

| Метод класса `Touch` | Описание |
|---|---|
| `bool configure(const TouchConfig &cfg)` | Задать параметры. Вызывается до `begin()` |
| `bool begin()` | Запуск: I²C-шина, прерывание |
| `void end()` | Остановка |
| `void update()` | Опрос контроллера. Вызывайте в цикле |
| `bool ready() const` | Контроллер запущен |
| `uint8_t count() const` | Число активных точек |
| `TouchPoint point(uint8_t index) const` | Точка `0..MaxPoints-1` |
| `bool touched() const` | Есть ли хотя бы одно активное касание |
| `static constexpr uint8_t MaxPoints` | 2 |

### Пример

```cpp
Touch *touch = plat->touch();
if (!touch)
    return;                                // модуль выключен в menuconfig

TouchConfig tcfg = {};
tcfg.sda = 4;
tcfg.scl = 5;
tcfg.intr = 6;
tcfg.width = 320;
tcfg.height = 480;

if (!touch->configure(tcfg) || !touch->begin())
{
    log::error("touch init failed");
    return;
}

// в цикле:
touch->update();
for (uint8_t i = 0; i < touch->count(); ++i)
{
    const TouchPoint p = touch->point(i);
    if (p.state == TouchState::Pressed)
        onPress(p.x, p.y);
}
```

| `TouchState` | Значение |
|---|---|
| `Pressed` | Палец только что коснулся |
| `Held` | Палец на месте |
| `Moved` | Палец сместился |
| `Released` | Точка не активна |

---

## 10. Audio - микшер и формат PAC

16-голосный программный микшер со встроенным ресемплером, вывод через I²S standard-mode (16 бит, стерео). Включается `PIPCORE_ENABLE_AUDIO`. Доступ - `plat->audio()`.

**Модель потоков.** Микшер работает в собственном FreeRTOS-таске (стек, приоритет и ядро - в Kconfig; по умолчанию ядро 0, противоположное рендеру). Методы класса `Audio` защищены мьютексом - `play()` можно вызывать из любого таска. Пользовательский `MixHook` выполняется в контексте таска микшера, держите его быстрым и не блокирующим.

### Типы

```cpp
struct SoundConfig
{
    uint32_t sampleRate = 44100;   // частота I²S-выхода, Гц
    int8_t   bck = 13;             // пины I²S
    int8_t   ws = 15;
    int8_t   dataOut = 17;
    uint8_t  i2sPort = 0;
};

enum class Bus : uint8_t { MUSIC = 0, SFX = 1, UI = 2, AMBIENT = 3 };

struct SoundHandle { uint16_t id = 0; };   // id == 0 - «нет звука»
```

Шины позволяют регулировать громкость и лимиты голосов по группам звуков.

### Методы

| Метод | Описание |
|---|---|
| `bool configure(const SoundConfig &cfg)` | Пины и частота I²S. Вызывайте до `begin()`. Если не вызвать, `begin()` возьмёт `SoundConfig{}` |
| `bool begin()` | Запуск вывода и таска микшера. `false` - не удалось |
| `void end()` | Остановка |
| `bool ready() const` | Микшер запущен |
| `SoundHandle play(const uint8_t *data, size_t dataSize, float volume = 1.0f, uint16_t gainL_q15 = 32767, uint16_t gainR_q15 = 32767, Bus bus = Bus::SFX, uint8_t priority = 5)` | Запустить звук из PAC-данных в памяти. Возвращает `{0}`, если микшер не запущен, данные битые или не нашлось свободного голоса. `data` должна быть выровнена на 4 байта |
| `void stop(SoundHandle h)` | Остановить голос |
| `void pause(SoundHandle h)` / `void resume(SoundHandle h)` | Пауза / продолжение |
| `void setVoiceGains(SoundHandle h, uint16_t gainL_q15, uint16_t gainR_q15)` | Панорама голоса. Формат q15: 32767 = 1.0 |
| `void setMasterVolume(float v01)` / `float masterVolume() const` | Общая громкость 0…1 (по умолчанию 0,8) |
| `void setBusVolume(Bus bus, float v01)` / `float busVolume(Bus bus) const` | Громкость шины 0…1 |
| `void setBusQuota(const BusQuota &q)` | Лимит одновременных голосов на шину. `BusQuota{ maxVoices[4] }`, по умолчанию `{2, 10, 2, 2}` (MUSIC, SFX, UI, AMBIENT) |
| `void setMixHook(MixHook hook, void *user)` | Пользовательская пост-обработка микса до мастер-громкости (эквалайзер, реверб) |
| `uint8_t activeVoices() const` | Число активных (не на паузе) голосов |
| `uint8_t countActiveOnBus(Bus bus) const` | Активных голосов на шине |
| `static constexpr uint8_t MaxVoices` | 16 |

Тип хука:

```cpp
using MixHook = void (*)(int32_t *mixL, int32_t *mixR, size_t frames, void *user);
```

### Пример

```cpp
Audio *audio = plat->audio();
if (!audio)
    return;

audio->configure(SoundConfig{});          // пины по умолчанию
if (!audio->begin())
{
    log::error("audio init failed");
    return;
}
audio->setBusVolume(Bus::MUSIC, 0.8f);
audio->setBusVolume(Bus::SFX, 1.0f);

SoundHandle music = audio->play(musicData, musicSize, 0.9f, 32767, 32767, Bus::MUSIC);
audio->play(laserData, laserSize, 1.0f, 32767, 22000, Bus::SFX);   // смещение вправо
// ...
audio->stop(music);
```

Выровненные данные в прошивке:

```cpp
alignas(4) static const uint8_t laserData[] = { /* содержимое .pac */ };
```

### Приоритеты и вытеснение

Если квота шины исчерпана, `play()` вытесняет голос с **самым низким приоритетом** - при условии, что приоритет нового звука не ниже вытесняемого. Не удалось вытеснить - вернётся `{0}`. Голос, доигравший до конца, освобождается сам.

### Формат PAC

PAC - компактный контейнер озвучки для микшера (генерируется внешним инструментом). Данные лежат во флеш-памяти как есть, без копирования: `play()` получает указатель и проверяет заголовок.

```cpp
struct alignas(4) PACHeader          // ровно 48 байт
{
    char     magic[4];               // "PAC!"
    uint16_t version;
    uint16_t flags;                  // PAC_FLAG_LOOP
    uint32_t sourceRate;             // частота исходника, Гц (1000…192000)
    uint32_t nativeRate;             // частота «как задумано»
    uint32_t frameCount;             // кадров всего
    uint32_t loopStart, loopEnd;     // точки цикла, в кадрах
    uint32_t blockCount;
    uint32_t dataOffset;             // смещение полезных данных (>= 48)
    uint32_t dataSize;
    uint32_t reserved1, reserved2;

    bool isValid() const;            // magic == "PAC!"
    bool isLoop() const;             // flags & PAC_FLAG_LOOP
};
```

Данные разбиты на блоки по 256 кадров (`PAC_BLOCK_FRAMES`); каждый блок кодируется одним из режимов:

| Режим | Содержимое | Полезная нагрузка блока |
|---|---|---|
| `PAC_MODE_SILENCE` | Тишина | - |
| `PAC_MODE_ADPCM2` | ADPCM, 2 бита на сэмпл | `PacPayload2` = 64 байта |
| `PAC_MODE_ADPCM4` | ADPCM, 4 бита на сэмпл | `PacPayload4` = 128 байт |
| `PAC_MODE_ADPCM6` | ADPCM, 6 бит на сэмпл | `PacPayload6` = 192 байта |
| `PAC_MODE_HOLD` | Удержание предыдущего сэмпла | - |

Декодеры сэмплов (`decodeAdpcm4` и др. в `pipcore::audio`) публичны - их можно использовать в собственных инструментах.

**Ресемплер** линейный: `sourceRate` автоматически приводится к частоте I²S-выхода. При совпадении частот включается passthrough (пересчёт не тратит CPU).

---

## 11. WiFi - STA-подключение

Событийная state-machine поверх (режим STA). Включается `PIPCORE_ENABLE_WIFI`. Радио работает, пока запрошено `wifiRequest(true)`; при `wifiRequest(false)` железо полностью деинициализируется.

**Важно:** таймауты подключения и повторные попытки продвигаются только внутри `wifiService()` - вызывайте её регулярно (например, каждый проход главного цикла).

### Конфигурация

```cpp
struct WifiConfig
{
    char ssid[kWifiSsidCap] = {};          // kWifiSsidCap = 33
    char password[kWifiPasswordCap] = {};  // kWifiPasswordCap = 65

    bool disableSleep = false;             // true → WIFI_PS_NONE (максимальная
                                           // пропускная способность)

    bool fullScan = false;                 // true → сканировать все каналы
                                           // (медленнее, надёжнее)

    bool autoReconnect = true;             // автоматический реконнект с
                                           // нарастающей паузой

    uint32_t connectTimeoutMs = 15'000;    // таймаут одной попытки
    uint32_t retryDelayMs = 2'500;         // базовая пауза между попытками

    uint32_t staticIp = 0;                 // статический IPv4, формат - см. ниже
    
    uint32_t gateway = 0;                  // если staticIp, gateway и subnet
                                           // все != 0 - DHCP отключается
    uint32_t subnet = 0;
    uint32_t dns1 = 0;                     // необязательные DNS
    uint32_t dns2 = 0;

    void setCredentials(const char *ssidIn, const char *passwordIn);  // безопасное копирование
};

enum class WifiState : uint8_t
{
    Off = 0, Connecting = 1, Connected = 2, Failed = 3, Unsupported = 4
};
```

### Функции

| Функция | Описание |
|---|---|
| `void wifiConfigure(const WifiConfig &cfg)` | Задать креды и параметры. Вызывайте до включения радио |
| `void wifiRequest(bool enabled)` | Включить или выключить радио |
| `void wifiService()` | Прокачка state-machine. Вызывайте в цикле |
| `WifiState wifiState()` | Текущее состояние |
| `bool wifiConnected()` | `state == Connected` |
| `uint32_t wifiLocalIpV4()` | Локальный IPv4 (0 - адреса нет) |

| `WifiState` | Значение |
|---|---|
| `Off` | Радио выключено |
| `Connecting` | Идёт подключение или пауза перед повторной попыткой |
| `Connected` | Подключено, IP получен |
| `Failed` | Ошибка (в том числе пустой SSID); повторная попытка - по `retryDelayMs` |
| `Unsupported` | WiFi-бэкенд недоступен |

### Пример

```cpp
net::WifiConfig cfg = {};
cfg.setCredentials("MyWiFi", "password");
cfg.disableSleep = true;
net::wifiConfigure(cfg);
net::wifiRequest(true);

const uint32_t start = plat->nowMs();
while (!net::wifiConnected() && plat->nowMs() - start < 20'000)
{
    net::wifiService();
    plat->delayMs(10);
}

if (net::wifiConnected())
{
    const uint32_t ip = net::wifiLocalIpV4();
    log::info("IP: %u.%u.%u.%u",
              unsigned(ip >> 24), unsigned((ip >> 16) & 0xFF),
              unsigned((ip >> 8) & 0xFF), unsigned(ip & 0xFF));
}
```

### Формат IP-адресов

Адреса хранятся как `uint32_t` в порядке «старший октет - в старших битах»: `192.168.1.10` → `0xC0A8010A`; `ip >> 24` - первый октет. Это относится и к `staticIp`, `gateway`, `subnet`, `dns1`, `dns2`.

### Повторные попытки

Если попытка подключения не удалась (таймаут или обрыв), состояние сменяется на `Failed` / `Connecting`, и через `retryDelayMs` начинается новая попытка. При автореконнекте пауза удваивается с каждой неудачей до 16× от базовой - сеть не «заваливается» запросами при лежащем роутере. Ручной реконнект - снова `wifiRequest(true)`.

---

## 12. OTA - обновления прошивки

Обновление по манифесту с криптографической проверкой. Включается `PIPCORE_ENABLE_OTA` (требует WiFi). Сервис сам включает радио на время работы, поэтому WiFi-креды достаточно задать через `net::wifiConfigure()` - вызывать `wifiRequest(true)` не нужно.

### Поток обновления

```
GET {URL}/index.json
   → выбор канала (stable / beta) и более нового build
GET {URL}/<релиз>/manifest.json
   → проверка Ed25519-подписи
   → состояние UpdateAvailable
requestInstall()
   → потоковая загрузка с подсчётом SHA-256
   → запись во второй OTA-слот
   → смена загрузочного раздела
   → перезагрузка
```

**Anti-replay:** build не может быть меньше сохранённого для канала (хранится в NVS).

### Типы

```cpp
struct Options
{
    uint16_t currentVerMajor = 0, currentVerMinor = 0, currentVerPatch = 0;
    uint64_t currentBuild = 0;               // монотонный счётчик сборки
    const char *ed25519PubkeyHex = nullptr;  // открытый ключ, 64 hex-символа
};

enum class Channel   : uint8_t { Stable = 0, Beta = 1 };
enum class CheckMode : uint8_t { NewerOnly = 0, AllowDowngrade = 1 };

enum class State : uint8_t
{
    Idle = 0, WifiStarting = 1, FetchingManifest = 2, UpdateAvailable = 3,
    Downloading = 4, Installing = 5, Success = 6, Error = 7, UpToDate = 8
};

struct Manifest
{
    char     title[64];
    uint16_t verMajor, verMinor, verPatch;
    char     version[32];
    uint64_t build;
    uint32_t size;               // размер прошивки, байт
    char     url[512];
    char     desc[256];
    uint8_t  sha256[32];         // контрольная сумма образа
    uint8_t  sigEd25519[64];     // подпись манифеста
    bool     hasSig;
};

struct Status
{
    State state;  Error error;
    int httpCode;                // HTTP-код последнего запроса
    int platformCode;            // код для диагностики
    uint32_t downloaded, total;  // прогресс загрузки, байт
    uint32_t lastChangeMs;       // время последней смены состояния
    int8_t versionCmp, buildCmp; // сравнение с текущей прошивкой
    bool pendingVerify;          // прошивка ждёт подтверждения
    Channel channel;
    Manifest manifest;
};

using StatusCallback = void (*)(const Status &st, void *user);
```

### Коды ошибок (`ota::Error`)

| Код | Значение | Причина |
|---|---|---|
| `None` (0) | - | Ошибки нет |
| `WifiNotEnabled` (1) | WiFi недоступен | OTA-сервису не удалось получить WiFi-бэкенд |
| `WifiNotConnected` (2) | Нет подключения | WiFi перешёл в `Failed` (проверьте креды через `net::wifiConfigure`) |
| `HttpBeginFailed` (3) | Не удалось начать HTTP-запрос | Проверьте URL |
| `HttpStatusNotOk` (4) | Сервер вернул не 200 | См. `Status::httpCode` |
| `ManifestTooLarge` (5) | Манифест слишком большой | - |
| `ManifestParseFailed` (6) | Манифест не разобран | Неверный JSON/поля |
| `ManifestReplay` (7) | Build меньше сохранённого | Защита от отката на старую версию |
| `SignatureMissing` (8) | Нет подписи | - |
| `SignatureInvalid` (9) | Подпись неверна | Не тот ключ или манифест изменён |
| `FlashLayoutInvalid` (10) | Неподходящая разметка флеш | Проверьте таблицу разделов: нужен OTA-слот под образ |
| `RollbackUnavailable` (11) | Откат невозможен | Зарезервирован |
| `UpdateBeginFailed` (12) | Не удалось начать запись | - |
| `UpdateWriteFailed` (13) | Ошибка записи | - |
| `HashPipelineFailed` (14) | Ошибка подсчёта SHA-256 | - |
| `DownloadTruncated` (15) | Загрузка оборвана | - |
| `PayloadSizeMismatch` (16) | Размер не совпал с манифестом | - |
| `HashMismatch` (17) | Контрольная сумма не совпала | - |
| `UpdateEndFailed` (18) | Не удалось завершить обновление | - |
| `UrlTooLong` (19) | URL длиннее буфера | - |

### Функции

| Функция | Описание |
|---|---|
| `void configure(const Options &opt, StatusCallback cb = nullptr, void *user = nullptr)` | Инициализация. Базовый URL берётся из `PIPCORE_OTA_PROJECT_URL`. `cb` вызывается на каждое изменение статуса |
| `void requestCheck()` / `void requestCheck(CheckMode mode)` | Проверить обновления. `NewerOnly` - только более новые build; `AllowDowngrade` - разрешить и более старые |
| `void requestInstall()` | Установить найденное обновление (после `UpdateAvailable`) |
| `void requestStableList()` | Запросить список стабильных версий сервера |
| `bool stableListReady()` | Список получен |
| `uint8_t stableListCount()` | Число версий в списке |
| `const char *stableListVersion(uint8_t idx)` | Версия по индексу |
| `void requestInstallStableVersion(const char *version)` | Установить конкретную версию (откат) |
| `void cancel()` | Прервать текущую операцию |
| `void service()` | Прокачка state-machine. Вызывайте в цикле. Блокируется не дольше `PIPCORE_OTA_HTTP_TIMEOUT_MS` за вызов |
| `const Status &status()` | Полный статус |
| `void markAppValid()` | Подтвердить работоспособность прошивки после OTA-перезагрузки |

### Пример

```cpp
ota::Options opt = {};
opt.currentVerMajor = 2;  opt.currentVerMinor = 0;  opt.currentVerPatch = 0;
opt.currentBuild = 2000000;
opt.ed25519PubkeyHex = PIPCORE_PUBKEY;    // ваш публичный ключ, 64 hex-символа

ota::configure(opt, onStatus, nullptr);
ota::requestCheck();

// в главном цикле:
ota::service();

const ota::Status &st = ota::status();
if (st.state == ota::State::UpdateAvailable)
    ota::requestInstall();                // например, после подтверждения пользователя
else if (st.state == ota::State::Error)
    log::error("ota: error %d (http %d)", int(st.error), st.httpCode);
```

### Подтверждение после обновления

Новая прошивка стартует в состоянии *pending-verify*. Если приложение не вызовет `markAppValid()`, загрузчик IDF откатится на предыдущую версию. Вызывайте `markAppValid()`, когда убедились, что всё работает (сеть поднялась, железо ответило):

```cpp
extern "C" void app_main()
{
    // ... инициализация железа ...
    if (initOk)
        ota::markAppValid();
}
```

### Безопасность

- **TLS:** сертификат сервера проверяется системным bundle ESP-IDF.
- **Подпись:** манифест подписывается Ed25519 по каноническому представлению полей (префикс `pipcore-ota-manifest`, `title`, `version`, `build`, `size`, `sha256`, `url`, `desc`). Меняйте эти поля только инструментом публикации - иначе подпись станет недействительной.
- **Целостность:** образ проверяется по SHA-256 из подписанного манифеста.

---

## 13. Prefs - постоянные настройки

Key-value хранилище поверх NVS (пространство имён `pipcore`). Включается `PIPCORE_ENABLE_PREFS`. NVS инициализируется при первом обращении. В штатной работе ядро NVS не стирает, единственное исключение - стандартное поведение ESP-IDF при несовместимости формата раздела (`ESP_ERR_NVS_NO_FREE_PAGES` или `ESP_ERR_NVS_NEW_VERSION_FOUND`, например после обновления IDF или смены разметки), тогда раздел стирается и инициализируется заново, все сохранённые значения при этом теряются. Функции защищены от гонок между тасками.

Свободные функции в `pipcore::prefs`:

**Чтение** - возвращают `true`, если значение прочитано и записано в `out`; `false` - ключа нет или произошла ошибка.

| Функция | Описание |
|---|---|
| `bool getU8(const char *key, uint8_t &out)` | 8 бит без знака |
| `bool getU16(const char *key, uint16_t &out)` | 16 бит без знака |
| `bool getU32(const char *key, uint32_t &out)` | 32 бита без знака |
| `bool getU64(const char *key, uint64_t &out)` | 64 бита без знака |
| `bool getI32(const char *key, int32_t &out)` | 32 бита со знаком |
| `bool getStr(const char *key, char *out, size_t cap)` | Строка в буфер `out` ёмкостью `cap` байт (с учётом завершающего нуля) |
| `bool getBlob(const char *key, void *out, size_t &len)` | Бинарные данные. `len`: на входе - ёмкость `out`, на выходе - реальный размер |

**Запись** - `true` означает, что значение сохранено (коммит выполняется автоматически).

| Функция | Описание |
|---|---|
| `bool setU8/U16/U32/U64/I32(const char *key, T v)` | Записать число |
| `bool setStr(const char *key, const char *v)` | Записать строку |
| `bool setBlob(const char *key, const void *data, size_t len)` | Записать бинарные данные |
| `bool eraseKey(const char *key)` | Удалить ключ |
| `bool eraseAll()` | Стереть всё пространство имён |

### Примеры

Сохранить и прочитать:

```cpp
prefs::setU8("maxBright", 100);

uint8_t bright = 100;                       // значение по умолчанию
if (prefs::getU8("maxBright", bright))
    applyBrightness(bright);
```

Паттерн «прочитать с дефолтом»:

```cpp
if (!prefs::getU32("bestScore", best))
    best = 0;                               // первый запуск: ключа ещё нет
```

Если результат чтения не важен (нужно просто сохранить значение по умолчанию), его можно явно проигнорировать:

```cpp
uint32_t best = 0;
(void)prefs::getU32("bestScore", best);     // при неудаче best останется 0
```

Блоб с определением размера:

```cpp
size_t len = 0;
if (prefs::getBlob("calib", nullptr, len) && len > 0)   // сначала узнаём размер
{
    auto *data = static_cast<uint8_t *>(plat->alloc(len));
    if (data && prefs::getBlob("calib", data, len))
        useCalibration(data, len);
    plat->free(data);
}
```

### Ограничения NVS

- Ключ - не длиннее 15 символов.
- Строки - до ≈ 4000 байт.
- Блобы читаются в два захода: сначала размер (`getBlob(key, nullptr, len)`), затем данные.
- Каждая запись - это запись во флеш, не пишите в цикле каждый кадр, чтобы не износить память.

Прямой доступ к бэкенду - `prefs::backend()` (или `plat->prefs()`), если нужен собственный слой поверх.

---

## 14. Storage - файловое хранилище

LittleFS через VFS. Включается `PIPCORE_ENABLE_STORAGE`. Раздел задаётся `PIPCORE_STORAGE_PARTITION_LABEL`. Пути относительны точки монтирования: можно писать `"/cfg.json"` или `"cfg.json"` - ядро само сопоставит с разделом.

```cpp
enum class OpenMode : uint8_t { Read = 0, Write = 1, Append = 2 };
```

| Функция | Описание |
|---|---|
| `bool begin(bool formatOnFail = false)` | Смонтировать раздел. `formatOnFail` - отформатировать, если файловая система пуста или повреждена |
| `File open(const char *path)` | Открыть файл на чтение либо каталог |
| `File open(const char *path, OpenMode mode)` | Открыть файл в указанном режиме |
| `bool remove(const char *path)` | Удалить файл |
| `bool rename(const char *from, const char *to)` | Переименовать или переместить |
| `bool mkdir(const char *path)` | Создать каталог |
| `bool exists(const char *path)` | Существует ли путь |

`OpenMode::Write` открывает файл с нуля (содержимое стирается), `Append` дописывает в конец. Вызывайте `begin()` один раз перед остальными функциями.

**Безопасность путей.** Компонент `..` в пути отвергается - ни приложение, ни отладочная консоль не могут выйти за пределы раздела. Пустой путь тоже отвергается.

### File

`File` - владеющий handle: перемещаемый (move-only), закрывается в деструкторе.

| Метод | Описание |
|---|---|
| `explicit operator bool() const` | Открыт ли файл/каталог |
| `bool isDirectory() const` | Это каталог |
| `void close()` | Закрыть явно |
| `const char *name() const` | Короткое имя (без пути). Указатель действителен до следующего вызова `name()` в том же таске - скопируйте строку, если она нужна дольше |
| `File openNextFile()` | Следующий элемент каталога (пустой `File`, когда закончились) |
| `int read(uint8_t *buf, size_t size)` | Прочитать до `size` байт; возвращает число прочитанных |
| `size_t write(const uint8_t *buf, size_t size)` | Записать; возвращает число записанных байт |
| `size_t write(const char *buf, size_t size)` | То же для `const char*` |
| `size_t size() const` | Размер файла, байт |
| `bool seek(size_t pos)` | Перейти к позиции |
| `void flush()` | Сбросить буфер на носитель |

### Примеры

Запись файла:

```cpp
if (!storage::begin(false))
{
    log::error("storage mount failed");
    return;
}

storage::File out = storage::open("/cfg.json", storage::OpenMode::Write);
if (out)
{
    const size_t written = out.write(json, jsonLen);
    out.close();
    if (written != jsonLen)
        log::warning("cfg.json: short write");
}
```

Чтение файла:

```cpp
storage::File in = storage::open("/cfg.json");
if (in)
{
    uint8_t buf[256];
    int n;
    while ((n = in.read(buf, sizeof(buf))) > 0)
        parse(buf, size_t(n));
}
```

Листинг каталога:

```cpp
storage::File dir = storage::open("/");
for (storage::File it = dir.openNextFile(); it; it = dir.openNextFile())
{
    if (!it.isDirectory())
        log::info("%s %u", it.name(), unsigned(it.size()));
}
```

---

## 15. Log - логирование

Лёгкий логгер ядра. Уровень проверяется до форматирования (отключённый уровень не тратит такты на разбор строки формата). Вывод идёт в платформенный бэкенд плюс опциональный приёмник строк.

```cpp
enum class log::Level : uint8_t
{
    Verbose = 0, Debug = 1, Info = 2, Warning = 3, Error = 4, Off = 5
};

using log::Sink = void (*)(void *user, Level level, const char *line) noexcept;
```

| Функция | Описание |
|---|---|
| `void log::setLevel(Level level)` | Установить порог уровня в рантайме |
| `Level log::level()` | Текущий порог |
| `bool log::enabled(Level level)` | Пройдёт ли сообщение такого уровня через фильтр - полезно, чтобы не готовить дорогие аргументы |
| `void log::setSink(Sink sink, void *user)` | Дополнительный приёмник отформатированных строк (`nullptr` - отключить) |
| `void log::print(Level level, const char *fmt, ...)` | Вывод с `printf`-форматированием |
| `void log::vprint(Level level, const char *fmt, std::va_list args)` | То же с `va_list` |
| `log::verbose/debug/info/warning/error(const char *fmt, ...)` | Сокращения для соответствующего уровня |

- Стартовый уровень - `PIPCORE_LOG_LEVEL` (по умолчанию Info). Сообщения ниже порога не форматируются вообще.
- На ESP32 вывод идёт через логгер IDF (метка `pipcore`, с меткой времени и уровнем) - виден в `idf.py monitor`; в симуляторе - в консоль.
- `setSink()` передаёт каждую *уже отформатированную* строку вашему колбэку. Так симулятор показывает логи в своей консоли; так же логи можно пересылать куда угодно (по сети, в файл).

```cpp
log::info("heap: internal %u KB", unsigned(plat->freeHeapInternal() / 1024));
log::error("display: %s", plat->lastErrorText());

if (log::enabled(log::Level::Debug))
    log::debug("state dump: %s", buildExpensiveDump());
```

---

## 16. Debug - профайлер и трекер аллокаций

Включается `PIPCORE_ENABLE_DEBUG`. В production-сборке (выключено) макросы профайлера превращаются в пустышки - нулевая цена.

### Профайлер

```cpp
void hotPath()
{
    PIP_PROFILE_FUNCTION();        // зона на всю функцию (имя из __PRETTY_FUNCTION__)
    PIP_PROFILE_ZONE("parse");     // именованная зона в любом scope
    // ...
}
```

Макросы строят дерево узлов: суммарное и собственное время (в тактах CPU), число вызовов, максимальная длительность зоны. Точность измерения - до такта (`debug::profileCycles()`). Каждая зона - одна строка кода: не ставьте две `PIP_PROFILE_ZONE` на одной строке.

Данные читает PipCore Inspector (`GET_PROFILE`) или вы вручную:

```cpp
auto &prof = debug::Profiler::instance();
prof.calculateSelfCycles();                 // пересчёт собственного времени
for (debug::ProfileNode *n = prof._head; n; n = n->next)
    log::info("%s: %u cycles x%u", n->name, unsigned(n->totalCycles), unsigned(n->callCount));
prof.clear();                               // обнулить счётчики
```

### Трекер аллокаций

Все глобальные `operator new/delete` идут через трекер: он ведёт текущие и пиковые байты, теги точек выделения и полный список живых аллокаций (виден в Inspector).

```cpp
debug::AllocStats st = debug::allocStats();
log::info("heap now %u, peak %u", unsigned(st.currentBytes), unsigned(st.peakBytes));
```

`debug::allocStats()` и `AllocStats` доступны только при `PIPCORE_ENABLE_DEBUG`, поэтому при использовании оберните вызовы в `#if PIPCORE_ENABLE_DEBUG`.

### Отладочная консоль (PipCore Inspector)

`PIPCORE_DEBUG_CONSOLE` поднимает ASCII-консоль (таск `PipCoreConsole`, транспорт - USB-Serial/JTAG или UART0) для десктопного инспектора.

| Команда | Действие |
|---|---|
| `GET_ALLOCS` | Список живых аллокаций |
| `GET_FLASH` | Информация о флеш-памяти |
| `GET_NVS` | Содержимое NVS |
| `GET_CPU` | Загрузка CPU |
| `GET_PROFILE` / `RESET_PROFILE` | Данные профайлера / сброс |
| `GET_FS` | Содержимое файловой системы |
| `GET_FILE:<path>` | Скачать файл |
| `WRITE_START:<path>` → `WRITE_CHUNK:<hex>` → `WRITE_END` | Загрузить файл по частям |
| `DELETE_FILE:<path>` | Удалить файл |
| `GET_PARTITIONS` | Таблица разделов |
| `READ_FLASH:<offset>,<size>` | Прочитать флеш (до 1 КБ за запрос) |
| `GET_BOOT_SECURITY` | Состояние защиты загрузки |

> **Консоль не имеет аутентификации.** Любой, у кого есть доступ к порту, может прочитать всю флеш-память и NVS, а также скачать, перезаписать и удалить файлы в разделе хранилища. Используйте только при разработке. Ядро не соберётся, пока не включён `PIPCORE_DEBUG_CONSOLE_ACCEPT_RISK`. **Никогда не включайте консоль в релизной прошивке.**

---

## 17. Симулятор

Тот же код ядра, собранный под Windows (нативный Win32), логика приложения отлаживается на ПК и затем прошивается без изменений.

```powershell
Tools/Simulator/Sim.ps1      # сборка + запуск (флаги: -Debug, -Clean, -NoRun)
```

**Возможности:**

- окно устройства 480×320 в масштабе 1:1, мышь эмулирует тач;
- боковая панель: пауза, шаг по истории кадров, скриншот, запись, кадров за шаг, масштаб времени 5-200 %, лимит FPS (настройки сохраняются в ini);
- метрики под экраном: FPS, загрузка CPU рендера, куча прошивки с пиком против бюджета 240 КБ;
- скриншоты - PNG формат; запись MP4 - нативно через Media Foundation.

Артефакты сборки лежат в `Tools/Simulator/Build/`.

**Различия платформ.** Макросы `PIPCORE_TARGET_ESP32` / `PIPCORE_TARGET_DESKTOP` (из `<Config.hpp>`) позволяют скрыть платформенно-специфичный код; в остальном API идентичен:

```cpp
#if PIPCORE_TARGET_ESP32
    // только для железа
#else
    // только для симулятора
#endif
```