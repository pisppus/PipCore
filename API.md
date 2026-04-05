# PipCore API

## Что это

`PipCore` это низкоуровневое ядро для прошивок и библиотек поверх него.

Слои:

- внешний API: то, что допустимо использовать в прошивке
- внутренний API: то, что предназначено для платформ, дисплеев, transport-слоя и backend-сервисов

## Основное подключение

Предпочтительная точка входа:

```cpp
#include <PipCore/PipCore.hpp>
```

Этот umbrella-header подключает:

- `Config/Features.hpp`
- `Platform.hpp`
- `Display.hpp`
- `Platforms/Select.hpp`
- `Graphics/Sprite.hpp`
- `Input/Button.hpp`
- `Network/Wifi.hpp`
- `Update/Ota.hpp`

Если нужен только отдельный модуль, можно подключать его напрямую.

## Типичный сценарий

```cpp
#include <PipCore/PipCore.hpp>

using namespace pipcore;

void setup()
{
    Platform *plat = GetPlatform();

    DisplayConfig cfg;
    cfg.mosi = 11;
    cfg.sclk = 12;
    cfg.cs = 10;
    cfg.dc = 9;
    cfg.rst = 8;
    cfg.width = 320;
    cfg.height = 170;
    cfg.hz = 40000000;

    plat->configDisplay(cfg);
    plat->beginDisplay(0);
}
```

## Внешний API

### Платформа

Файлы:

- `PipCore/Platform.hpp`
- `PipCore/Platforms/Select.hpp`

Основные сущности:

- `pipcore::Platform`
- `pipcore::GetPlatform()`
- `pipcore::SelectedPlatform`
- `pipcore::DisplayConfig`
- `pipcore::PlatformError`
- `pipcore::InputMode`
- `pipcore::AllocCaps`

Основные методы `Platform`:

- `nowMs()`
- `configDisplay(const DisplayConfig&)`
- `beginDisplay(uint8_t rotation)`
- `setDisplayRotation(uint8_t rotation)`
- `display()`
- `configureBacklightPin(...)`
- `setBacklightPercent(...)`
- `loadMaxBrightnessPercent()`
- `storeMaxBrightnessPercent(...)`
- `freeHeapTotal()`
- `freeHeapInternal()`
- `largestFreeBlock()`
- `minFreeHeap()`
- `lastError()`
- `lastErrorText()`
- `network()`
- `update()`

Замечания:

- `Platform::network()` и `Platform::update()` это низкоуровневый путь
- для прикладного кода предпочтительнее использовать wrapper API из `pipcore::net` и `pipcore::ota`
- при выключенных optional-модулях часть platform-level API может возвращать `nullptr` или fallback-значения

### Дисплей

Файл:

- `PipCore/Display.hpp`

Интерфейс:

- `begin(rotation)`
- `setRotation(rotation)`
- `width()`
- `height()`
- `fillScreen565(color565)`
- `writeRect565(x, y, w, h, pixels, stridePixels)`

Обычно используется через:

```cpp
if (auto *display = pipcore::GetPlatform()->display())
{
    display->fillScreen565(0x0000);
}
```

### Sprite

Файл:

- `PipCore/Graphics/Sprite.hpp`

Назначение:

- RAM-backed 16-bit sprite buffer
- промежуточный рендер
- локальный framebuffer
- вывод в `Display`

Основные методы:

- `createSprite(w, h)`
- `deleteSprite()`
- `fillScreen(color565)`
- `drawPixel(...)`
- `pushImage(...)`
- `fillRect(...)`
- `setClipRect(...)`
- `getClipRect(...)`
- `pushSprite(...)`
- `writeToDisplay(display, x, y, w, h)`

Полезные helper-методы:

- `Sprite::color565(r, g, b)`
- `Sprite::swap16(v)`
- `Sprite::u8clamp(v)`
- `Sprite::blend565(bg, fg, alpha)`

### Button

Файл:

- `PipCore/Input/Button.hpp`

Назначение:

- debounce кнопки
- polling-based input

Основные методы:

- `begin()`
- `update()`
- `wasPressed()`
- `isDown()`

Можно создавать:

- `Button(pin, pull)`
- `Button(platform, pin, pull)`

### Wi-Fi API

Файл:

- `PipCore/Network/Wifi.hpp`

Типы:

- `pipcore::net::WifiState`
- `pipcore::net::WifiConfig`
- `pipcore::net::Backend`

Wrapper-функции:

- `wifiConfigure(cfg)`
- `wifiRequest(enabled)`
- `wifiService()`
- `wifiState()`
- `wifiConnected()`
- `wifiLocalIpV4()`

Важно:

- этот API доступен только при `PIPCORE_ENABLE_WIFI=1`
- если `PIPCORE_ENABLE_WIFI=0`, вызов этих wrapper-функций даёт compile-time ошибку
- это намеренное поведение: выключенный модуль нельзя использовать "тихо"

### OTA API

Файл:

- `PipCore/Update/Ota.hpp`

Типы:

- `pipcore::ota::Options`
- `pipcore::ota::Channel`
- `pipcore::ota::CheckMode`
- `pipcore::ota::State`
- `pipcore::ota::Error`
- `pipcore::ota::Manifest`
- `pipcore::ota::Status`
- `pipcore::ota::Backend`

Wrapper-функции:

- `markAppValid()`
- `configure(opt, cb, user)`
- `requestCheck()`
- `requestCheck(mode)`
- `requestInstall()`
- `requestStableList()`
- `stableListReady()`
- `stableListCount()`
- `stableListVersion(idx)`
- `requestInstallStableVersion(version)`
- `cancel()`
- `service()`
- `status()`

Важно:

- OTA доступен только при `PIPCORE_ENABLE_OTA=1`
- OTA требует `PIPCORE_ENABLE_WIFI=1`
- OTA project URL задаётся через `PIPCORE_OTA_PROJECT_URL`
- если `PIPCORE_ENABLE_OTA=0`, вызов внешнего OTA API даёт compile-time ошибку

## Внутренний API

Этот слой не предназначен для обычного кода прошивки.

Его используют:

- platform backends
- display drivers
- transport implementations
- библиотеки более высокого уровня

### Platform implementation

Файлы:

- `PipCore/Platforms/ESP32/Platform.hpp`
- `PipCore/Platforms/ESP32/Services/Core.hpp`

Это реализация `pipcore::Platform` для ESP32.

Обычно из прошивки напрямую не используется. Предпочтительный вход:

```cpp
pipcore::GetPlatform()
```

### Display selection

Файл:

- `PipCore/Displays/Select.hpp`

Назначение:

- compile-time выбор конкретного display-класса
- alias `pipcore::SelectedDisplay`

### Display implementations

Файлы:

- `PipCore/Displays/ST7789/*`
- `PipCore/Displays/ILI9488/*`

Назначение:

- реализация протокола конкретной матрицы
- ротация, адресные окна, write path, fill path

### ESP32 transports

Файлы:

- `PipCore/Platforms/ESP32/Transports/St7789Spi.hpp`
- `PipCore/Platforms/ESP32/Transports/Ili9488Spi.hpp`

Назначение:

- SPI/GPIO/DMA transport-слой
- используется display driver-ами через transport interface

### ESP32 services

Файлы:

- `PipCore/Platforms/ESP32/Services/Wifi.hpp`
- `PipCore/Platforms/ESP32/Services/Ota.hpp`
- `PipCore/Platforms/ESP32/Services/Prefs.hpp`

Назначение:

- concrete backend implementations для `net::Backend` и `ota::Backend`
- platform-specific persistence

## Compile-time флаги

Файл:

- `PipCore/Config/Features.hpp`

Поддерживаемые макросы:

- `PIPCORE_PLATFORM`
- `PIPCORE_DISPLAY`
- `PIPCORE_ENABLE_PREFS`
- `PIPCORE_ENABLE_WIFI`
- `PIPCORE_ENABLE_OTA`
- `PIPCORE_OTA_PROJECT_URL`

### `PIPCORE_PLATFORM`

Сейчас поддерживается:

- `ESP32`

Пример:

```ini
-D PIPCORE_PLATFORM=ESP32
```

### `PIPCORE_DISPLAY`

Сейчас поддерживается:

- `ST7789`
- `ILI9488`

Пример:

```ini
-D PIPCORE_DISPLAY=ILI9488
```

### `PIPCORE_ENABLE_PREFS`

Включает platform-level prefs backend.

Значения:

- `0`
- `1`

По умолчанию:

- `0`

Поведение:

- при `0` platform prefs API работает как fallback/no-op
- compile-time ошибки для prefs сейчас не вводятся, потому что это platform-level сервис, а не отдельный внешний wrapper-модуль

### `PIPCORE_ENABLE_WIFI`

Включает Wi-Fi backend платформы.

Значения:

- `0`
- `1`

По умолчанию:

- `0`

Поведение:

- backend платформы не создаётся
- внешний `pipcore::net::*` API становится compile-time недоступен

### `PIPCORE_ENABLE_OTA`

Включает OTA backend платформы.

Значения:

- `0`
- `1`

По умолчанию:

- `0`

Ограничение:

- `PIPCORE_ENABLE_OTA=1` требует `PIPCORE_ENABLE_WIFI=1`

Поведение:

- backend платформы не создаётся
- внешний `pipcore::ota::*` API становится compile-time недоступен

### `PIPCORE_OTA_PROJECT_URL`

URL OTA project index / release endpoint.

Используется только если:

- `PIPCORE_ENABLE_OTA=1`

Пример:

```ini
-D PIPCORE_OTA_PROJECT_URL=\"https://example.com/fw/my-device\"
```

## Рекомендуемые конфигурации

### Минимальная

```ini
build_flags =
    -std=gnu++17
    -Iinclude
    -D PIPCORE_DISPLAY=ST7789
    -D PIPCORE_ENABLE_PREFS=0
    -D PIPCORE_ENABLE_WIFI=0
    -D PIPCORE_ENABLE_OTA=0
    -ffunction-sections
    -fdata-sections
    -Wl,--gc-sections
```

### ILI9488 без сети

```ini
build_flags =
    -std=gnu++17
    -Iinclude
    -D PIPCORE_DISPLAY=ILI9488
    -D PIPCORE_ENABLE_PREFS=0
    -D PIPCORE_ENABLE_WIFI=0
    -D PIPCORE_ENABLE_OTA=0
    -ffunction-sections
    -fdata-sections
    -Wl,--gc-sections
```

### Полная конфигурация

```ini
build_flags =
    -std=gnu++17
    -Iinclude
    -D PIPCORE_DISPLAY=ILI9488
    -D PIPCORE_ENABLE_PREFS=1
    -D PIPCORE_ENABLE_WIFI=1
    -D PIPCORE_ENABLE_OTA=1
    -D PIPCORE_OTA_PROJECT_URL=\"https://example.com/fw/my-device\"
    -ffunction-sections
    -fdata-sections
    -Wl,--gc-sections
```

## Что считать публичным

Можно использовать в прошивках напрямую:

- `PipCore/PipCore.hpp`
- `PipCore/Platform.hpp`
- `PipCore/Platforms/Select.hpp`
- `PipCore/Display.hpp`
- `PipCore/Graphics/Sprite.hpp`
- `PipCore/Input/Button.hpp`
- `PipCore/Network/Wifi.hpp`
- `PipCore/Update/Ota.hpp`

Не рекомендуется использовать напрямую из прикладного кода:

- `PipCore/Displays/ST7789/*`
- `PipCore/Displays/ILI9488/*`
- `PipCore/Platforms/ESP32/*`
- `PipCore/Platforms/ESP32/Services/*`
- `PipCore/Platforms/ESP32/Transports/*`

Это внутренняя часть реализации.