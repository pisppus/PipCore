set(srcs
    "Core/Memory.cpp"
    "Core/Log.cpp"
    "Platforms/ESP32/Core/Log.cpp"
)

if(CONFIG_PIPCORE_ENABLE_GRAPHICS)
    list(APPEND srcs
        "Graphics/Sprite.cpp"
        "Graphics/Raster.cpp"
    )
endif()

if(CONFIG_PIPCORE_ENABLE_AUDIO)
    list(APPEND srcs
        "Audio/Audio.cpp"
        "Audio/Pac.cpp"
        "Platforms/ESP32/Audio/I2s.cpp"
    )
endif()

if(CONFIG_PIPCORE_DISPLAY_ST7789 OR CONFIG_PIPCORE_DISPLAY_ST7796 OR CONFIG_PIPCORE_DISPLAY_ILI9488)
    list(APPEND srcs "Platforms/ESP32/Display/LcdTransport.cpp")
endif()

if(CONFIG_PIPCORE_DISPLAY_ILI9488)
    list(APPEND srcs
        "Display/ILI9488/Display.cpp"
        "Display/ILI9488/Driver.cpp"
    )
endif()

if(CONFIG_PIPCORE_ENABLE_TOUCH)
    list(APPEND srcs "Platforms/ESP32/Input/Touch.cpp")
endif()

if(CONFIG_PIPCORE_ENABLE_PREFS)
    list(APPEND srcs "Platforms/ESP32/Storage/Nvs.cpp")
endif()

if(CONFIG_PIPCORE_ENABLE_STORAGE)
    list(APPEND srcs "Platforms/ESP32/Storage/LittleFs.cpp")
endif()

if(CONFIG_PIPCORE_ENABLE_WIFI)
    list(APPEND srcs
        "Network/Wifi.cpp"
        "Platforms/ESP32/Network/Wifi.cpp"
    )
endif()

if(CONFIG_PIPCORE_ENABLE_OTA)
    list(APPEND srcs
        "Network/Ota.cpp"
        "Platforms/ESP32/Network/Common.cpp"
        "Platforms/ESP32/Network/Http.cpp"
        "Platforms/ESP32/Network/Manifest.cpp"
        "Platforms/ESP32/Network/Service.cpp"
        "Platforms/ESP32/Network/Update.cpp"
        "Network/Crypto/Ed25519.cpp"
        "Network/Crypto/ed25519/fe.c"
        "Network/Crypto/ed25519/ge.c"
        "Network/Crypto/ed25519/sc.c"
        "Network/Crypto/ed25519/sha512.c"
        "Network/Crypto/ed25519/verify.c"
    )
endif()

if(CONFIG_PIPCORE_ENABLE_DEBUG AND CONFIG_PIPCORE_DEBUG_CONSOLE)
    list(APPEND srcs "Platforms/ESP32/Debug/Console.cpp")
endif()

list(APPEND srcs
    "Platforms/ESP32/Platform.cpp"
    "Platforms/ESP32/Core/Core.cpp"
)

if(NOT DEFINED CONFIG_PIPCORE_ENABLE_PREFS)
    idf_build_get_property(_pipcore_sdkconfig SDKCONFIG)
    if(_pipcore_sdkconfig AND EXISTS "${_pipcore_sdkconfig}")
        file(STRINGS "${_pipcore_sdkconfig}" _pipcore_sdkconfig_lines)
        foreach(_line IN LISTS _pipcore_sdkconfig_lines)
            if(_line MATCHES "^CONFIG_PIPCORE_ENABLE_PREFS=y")
                set(CONFIG_PIPCORE_ENABLE_PREFS y)
            elseif(_line MATCHES "^CONFIG_PIPCORE_ENABLE_WIFI=y")
                set(CONFIG_PIPCORE_ENABLE_WIFI y)
            elseif(_line MATCHES "^CONFIG_PIPCORE_ENABLE_OTA=y")
                set(CONFIG_PIPCORE_ENABLE_OTA y)
            elseif(_line MATCHES "^CONFIG_PIPCORE_ENABLE_DEBUG=y")
                set(CONFIG_PIPCORE_ENABLE_DEBUG y)
            elseif(_line MATCHES "^CONFIG_PIPCORE_DEBUG_CONSOLE=y")
                set(CONFIG_PIPCORE_DEBUG_CONSOLE y)
            elseif(_line MATCHES "^CONFIG_PIPCORE_ENABLE_AUDIO=y")
                set(CONFIG_PIPCORE_ENABLE_AUDIO y)
            elseif(_line MATCHES "^CONFIG_PIPCORE_ENABLE_TOUCH=y")
                set(CONFIG_PIPCORE_ENABLE_TOUCH y)
            elseif(_line MATCHES "^CONFIG_PIPCORE_DISPLAY_ST7789=y")
                set(CONFIG_PIPCORE_DISPLAY_ST7789 y)
            elseif(_line MATCHES "^CONFIG_PIPCORE_DISPLAY_ST7796=y")
                set(CONFIG_PIPCORE_DISPLAY_ST7796 y)
            elseif(_line MATCHES "^CONFIG_PIPCORE_DISPLAY_ILI9488=y")
                set(CONFIG_PIPCORE_DISPLAY_ILI9488 y)
            endif()
        endforeach()
        unset(_pipcore_sdkconfig_lines)
    endif()
    unset(_pipcore_sdkconfig)
endif()

set(priv_requires
    "heap"
    "esp_timer"
    "esp_system"
    "esp_hw_support"
    "esp_rom"
    "freertos"
    "esp_driver_gpio"
    "esp_adc"
    "log"
)

if(CONFIG_PIPCORE_ENABLE_PREFS OR CONFIG_PIPCORE_ENABLE_WIFI OR (CONFIG_PIPCORE_ENABLE_DEBUG AND CONFIG_PIPCORE_DEBUG_CONSOLE))
    list(APPEND priv_requires "nvs_flash")
endif()

if(CONFIG_PIPCORE_ENABLE_AUDIO)
    list(APPEND priv_requires "esp_driver_i2s")
endif()

if(CONFIG_PIPCORE_ENABLE_TOUCH)
    list(APPEND priv_requires "esp_driver_i2c")
endif()

if(CONFIG_PIPCORE_DISPLAY_ST7789 OR CONFIG_PIPCORE_DISPLAY_ST7796 OR CONFIG_PIPCORE_DISPLAY_ILI9488)
    list(APPEND priv_requires "esp_driver_spi")
endif()

if(CONFIG_PIPCORE_ENABLE_DEBUG AND CONFIG_PIPCORE_DEBUG_CONSOLE)
    list(APPEND priv_requires
        "esp_driver_uart"
        "esp_driver_usb_serial_jtag"
        "esp_partition"
        "spi_flash"
        "esp_security"
        "efuse"
        "app_update"
    )
endif()

if(CONFIG_PIPCORE_ENABLE_WIFI)
    list(APPEND priv_requires
        "esp_wifi"
        "esp_netif"
        "esp_event"
    )
endif()

if(CONFIG_PIPCORE_ENABLE_OTA)
    list(APPEND priv_requires
        "esp_http_client"
        "app_update"
        "esp_partition"
        "spi_flash"
        "esp_security"
        "efuse"
        "mbedtls"
    )
endif()

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "Public"
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES "${priv_requires}"
)

set(pipcore_compile_opts
    -O2
    -fno-ipa-cp-clone
)
if(IDF_TARGET STREQUAL "esp32" OR IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
    list(APPEND pipcore_compile_opts -mtext-section-literals)
endif()

list(APPEND pipcore_compile_opts
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++2b;-ffast-math;-fno-trapping-math;-ffp-contract=fast;-fstrict-aliasing;-fomit-frame-pointer>
)

target_compile_options(${COMPONENT_LIB} PRIVATE ${pipcore_compile_opts})
