<p align="center">
  <img src="./Hero.png" alt="PipCore Library" width="100%">
</p>

<p align="center">
  <a href="../README.md">English</a> &nbsp;&nbsp; <a href="UA.md">Українська</a> &nbsp;&nbsp; <strong>Русский</strong><br>
  <sup>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;▔▔▔▔</sup>
</p>

PipCore - это легковесный высокопроизводительный слой аппаратных абстракций (HAL) и системное ядро, разработанное специально для микроконтроллеров.

<p align="center">
  <img src="./Architecture.png" alt="Архитектура PipCore" width="100%">
</p>

Ядро обеспечивает эффективный доступ к платформе, работу с GPIO и АЦП, а также асинхронный двухбуферный DMA-драйвер дисплеев для панелей ST7789, ST7796 и ILI9488. Движок спрайтов работает прямо в RGB565: отсечение по маске, быстрые 32-битные операции с аппаратным байт-свопом и программное альфа-смешивание. Ввод - ёмкостный тач и аналоговые джойстики, звук - программный микшер на 16 голосов со встроенным форматом PAC поверх I2S.

Для хранения есть файловые системы LittleFS и настройки на базе NVS. Из сетевого - событийный сервис Wi-Fi и неблокирующий OTA-апдейтер с проверкой подписи манифеста по Ed25519 и контролем целостности по SHA256. Встроенный симулятор для Windows исполняет тот же код ядра на десктопе (отрисовка кадров, эмуляция ввода), также создание PNG-скриншотов и запись MP4-видео.

> **Предупреждение**
>
> Драйвер ILI9488 в настоящее время находится в экспериментальной стадии. Стабильная работа не гарантируется, и некоторые модули дисплея могут демонстрировать непредсказуемое поведение в зависимости от аппаратного обеспечения и настроек.

<p align="center">
  <strong>Ресурсы</strong>&emsp;&emsp;&emsp;<strong>Нужен в</strong><br>
  <a href="https://pisppus.is-a.dev/docs/pipcore">Доки</a>&emsp;&nbsp;&nbsp;&emsp;&emsp;&emsp;&nbsp;<a href="https://github.com/pisppus/PipKit">PipKit</a><br>
  <a href="https://components.espressif.com/components/pisppus/pipcore">Registry</a>&emsp;&emsp;&emsp;&emsp;<a href="https://github.com/pisppus/Pip3D">Pip3D</a>
</p>

<p align="center">
 Распространяется под лицензией MIT. 
</p>