<p align="center">
  <img src="./README/Hero.png" alt="PipCore Library" width="100%">
</p>

<p align="center">
  <strong>English</strong> &nbsp;&nbsp; <a href="README/UA.md">Українська</a> &nbsp;&nbsp; <a href="README/RU.md">Русский</a><br>
  <sup>▔▔▔▔&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</sup>
</p>

PipCore is a lightweight, high-performance hardware abstraction layer (HAL) and system kernel designed specifically for microcontrollers.

<p align="center">
  <img src="./README/Architecture.png" alt="PipCore architecture" width="100%">
</p>

The kernel provides efficient platform access with GPIO and ADC support and an asynchronous dual-buffer DMA display driver for ST7789, ST7796 and ILI9488 panels. The sprite engine works directly in RGB565 space: mask clipping, fast 32-bit operations with hardware byte swapping, and software alpha blending. Input is handled by capacitive touch and analog joysticks, audio by a 16-voice software mixer playing the built-in PAC format over I2S.

For storage there are LittleFS file systems and NVS-backed preferences. On the network side there is an event-driven Wi-Fi service and a non-blocking OTA updater with Ed25519 manifest signature verification and SHA256 integrity checks. The built-in Windows simulator runs the same kernel code on the desktop (frame rendering, input emulation), as well as PNG screenshots and MP4 video recording.

> **Warning**
>
> The ILI9488 driver is currently experimental. Stable operation is not guaranteed and some display modules may exhibit unexpected behavior depending on hardware and configuration.

<p align="center">
  <strong>Resources</strong>&emsp;&emsp;&emsp;<strong>Used in</strong><br>
  &ensp;&ensp;<a href="https://pisppus.is-a.dev/docs/pipcore">Docs</a>&emsp;&emsp;&emsp;&emsp;&ensp;<a href="https://github.com/pisppus/PipKit">PipKit</a><br>
  &emsp;&emsp;&emsp;&emsp;&emsp;&emsp;&emsp;&emsp;<a href="https://github.com/pisppus/Pip3D">Pip3D</a>
</p>

<p align="center">
  Distributed under the MIT License.
</p>
