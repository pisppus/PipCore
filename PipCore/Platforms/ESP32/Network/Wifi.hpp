#pragma once

#include <atomic>
#include <cstdint>
#include <esp_event.h>

#include "Config.hpp"
#include <Network/Wifi.hpp>

#if !PIPCORE_TARGET_ESP32
#error "pipcore::esp32::services::Wifi requires ESP32"
#endif

namespace pipcore::esp32::services
{
    class Wifi : public pipcore::net::Backend
    {
    public:
        Wifi() = default;
        ~Wifi() override;

        void configure(const pipcore::net::WifiConfig &cfg) noexcept override;
        void request(bool enabled) noexcept override;
        void service() noexcept override;
        void service(uint32_t nowMs) noexcept;

        [[nodiscard]] pipcore::net::WifiState state() const noexcept override
        {
            return _state.load(std::memory_order_relaxed);
        }
        [[nodiscard]] bool connected() const noexcept override
        {
            return _state.load(std::memory_order_relaxed) == pipcore::net::WifiState::Connected;
        }
        [[nodiscard]] uint32_t localIpV4() const noexcept override { return _ipV4.load(std::memory_order_relaxed); }

    private:
        void setState(pipcore::net::WifiState st) noexcept;
        void wifiOff() noexcept;
        [[nodiscard]] bool ensureStaStarted() noexcept;
        void startConnect(uint32_t nowMs) noexcept;
        void applyDnsServers() noexcept;
        static void eventHandler(void *arg, esp_event_base_t base, int32_t id, void *data) noexcept;
        void onStaEvent(int32_t id) noexcept;

        pipcore::net::WifiConfig _cfg = {};
        bool _configured = false;
        bool _hwOffApplied = false;
        bool _handlersRegistered = false;

        void *_staNetif = nullptr;
        bool _wifiInited = false;

        std::atomic<pipcore::net::WifiState> _state{pipcore::net::WifiState::Off};
        std::atomic<uint32_t> _ipV4{0};

        bool _requested = false;
        std::atomic<bool> _userDisconnect{false};
        std::atomic<uint32_t> _attemptStartMs{0};
        std::atomic<uint32_t> _nextRetryMs{0};

        std::atomic<uint32_t> _reconnectAtMs{0};
        std::atomic<uint32_t> _reconnectAttempt{0};
    };
}
