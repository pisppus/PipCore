#include "Config.hpp"
#if PIPCORE_ENABLE_WIFI
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <algorithm>
#include <cstring>

#include "Platforms/ESP32/Network/Wifi.hpp"
#include "Platforms/ESP32/Core/Core.hpp"
#include <Log.hpp>

namespace pipcore::esp32::services
{
    namespace
    {
        constexpr int32_t kEvtGotIp = -1;

        [[nodiscard]] inline uint32_t ipToV4(const esp_ip4_addr_t &ip) noexcept
        {
            return ((static_cast<uint32_t>(esp_ip4_addr1(&ip)) << 24) | (static_cast<uint32_t>(esp_ip4_addr2(&ip)) << 16) |
                    (static_cast<uint32_t>(esp_ip4_addr3(&ip)) << 8) | static_cast<uint32_t>(esp_ip4_addr4(&ip)));
        }

        [[nodiscard]] inline esp_ip4_addr_t v4ToEsp(const uint32_t v4) noexcept
        {
            esp_ip4_addr_t ip = {};
            esp_netif_set_ip4_addr(&ip, static_cast<uint8_t>(v4 >> 24), static_cast<uint8_t>(v4 >> 16),
                                   static_cast<uint8_t>(v4 >> 8), static_cast<uint8_t>(v4));
            return ip;
        }
    }

    Wifi::~Wifi()
    {
        request(false);
        service();
    }

    void Wifi::configure(const pipcore::net::WifiConfig &cfg) noexcept
    {
        _cfg = cfg;
        _configured = true;
    }

    void Wifi::request(bool enabled) noexcept
    {
        _requested = enabled;
        if (!enabled)
            _nextRetryMs.store(0, std::memory_order_relaxed);
    }

    void Wifi::service() noexcept
    {
        service(nowMs());
    }

    void Wifi::setState(pipcore::net::WifiState st) noexcept
    {
        _state.store(st, std::memory_order_relaxed);
    }

    void Wifi::wifiOff() noexcept
    {
        _ipV4.store(0, std::memory_order_relaxed);
        setState(pipcore::net::WifiState::Off);
        _hwOffApplied = true;

        if (_wifiInited)
        {
            esp_wifi_disconnect();
            esp_wifi_stop();
            esp_wifi_deinit();
            _wifiInited = false;
        }

        if (_staNetif)
        {
            esp_netif_t *netif = static_cast<esp_netif_t *>(_staNetif);
            _staNetif = nullptr;
            esp_netif_destroy_default_wifi(netif);
        }
    }

    bool Wifi::ensureStaStarted() noexcept
    {
        if (_wifiInited)
            return true;

        if (!nvsEnsureReady())
            return false;

        if (esp_netif_init() != ESP_OK)
            return false;

        esp_event_loop_create_default();

        if (!_handlersRegistered)
        {
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Wifi::eventHandler, this, nullptr);
            esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Wifi::eventHandler, this, nullptr);
            _handlersRegistered = true;
        }

        if (!_staNetif)
            _staNetif = esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&cfg) != ESP_OK)
            return false;
        _wifiInited = true;

        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_wifi_set_mode(WIFI_MODE_STA);

        if (_cfg.disableSleep)
            esp_wifi_set_ps(WIFI_PS_NONE);
        else
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

        return true;
    }

    void Wifi::startConnect(uint32_t now) noexcept
    {
        _ipV4.store(0, std::memory_order_relaxed);
        _attemptStartMs.store(now, std::memory_order_relaxed);
        _hwOffApplied = false;
        _reconnectAtMs.store(0, std::memory_order_relaxed);

        if (!ensureStaStarted())
        {
            setState(pipcore::net::WifiState::Failed);
            _nextRetryMs.store(now + _cfg.retryDelayMs, std::memory_order_relaxed);
            return;
        }

        if (_cfg.staticIp != 0 && _cfg.subnet != 0 && _cfg.gateway != 0)
        {
            auto *netif = static_cast<esp_netif_t *>(_staNetif);
            esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_INIT;
            if (netif && esp_netif_dhcpc_get_status(netif, &status) == ESP_OK && status != ESP_NETIF_DHCP_STOPPED)
            {
                esp_netif_dhcpc_stop(netif);
                esp_netif_ip_info_t info = {};
                info.ip = v4ToEsp(_cfg.staticIp);
                info.gw = v4ToEsp(_cfg.gateway);
                info.netmask = v4ToEsp(_cfg.subnet);
                esp_netif_set_ip_info(netif, &info);
            }
        }

        wifi_config_t wifiCfg = {};

        const size_t ssidLen = ::strnlen(_cfg.ssid, sizeof(wifiCfg.sta.ssid) - 1);
        std::memcpy(wifiCfg.sta.ssid, _cfg.ssid, ssidLen);
        const size_t passLen = ::strnlen(_cfg.password, sizeof(wifiCfg.sta.password) - 1);
        std::memcpy(wifiCfg.sta.password, _cfg.password, passLen);
        wifiCfg.sta.threshold.authmode = _cfg.password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;

        wifiCfg.sta.scan_method = _cfg.fullScan ? WIFI_ALL_CHANNEL_SCAN : WIFI_FAST_SCAN;
        wifiCfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        if (esp_wifi_set_config(WIFI_IF_STA, &wifiCfg) != ESP_OK)
            log::warning("wifi: set_config failed");

        _userDisconnect.store(false, std::memory_order_relaxed);
        if (esp_wifi_start() != ESP_OK)
            log::warning("wifi: start failed");

        setState(pipcore::net::WifiState::Connecting);
    }

    void Wifi::applyDnsServers() noexcept
    {
        auto *netif = static_cast<esp_netif_t *>(_staNetif);
        if (!netif)
            return;

        if (_cfg.dns1 != 0)
        {
            esp_netif_dns_info_t dns = {};
            dns.ip.u_addr.ip4 = v4ToEsp(_cfg.dns1);
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
        }
        if (_cfg.dns2 != 0)
        {
            esp_netif_dns_info_t dns = {};
            dns.ip.u_addr.ip4 = v4ToEsp(_cfg.dns2);
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
        }
    }

    void Wifi::eventHandler(void *arg, esp_event_base_t base, int32_t id, void *data) noexcept
    {
        (void)data;
        auto *self = static_cast<Wifi *>(arg);
        if (base == WIFI_EVENT)
            self->onStaEvent(id);
        else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
            self->onStaEvent(kEvtGotIp);
    }

    void Wifi::onStaEvent(int32_t id) noexcept
    {
        if (id == WIFI_EVENT_STA_START)
        {
            esp_wifi_connect();
            return;
        }

        if (id == kEvtGotIp)
        {
            auto *netif = static_cast<esp_netif_t *>(_staNetif);
            esp_netif_ip_info_t info = {};
            if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK)
                _ipV4.store(ipToV4(info.ip), std::memory_order_relaxed);

            _reconnectAttempt.store(0, std::memory_order_relaxed);
            _reconnectAtMs.store(0, std::memory_order_relaxed);
            applyDnsServers();
            setState(pipcore::net::WifiState::Connected);
            return;
        }

        if (id == WIFI_EVENT_STA_DISCONNECTED)
        {
            _ipV4.store(0, std::memory_order_relaxed);
            if (_state.load(std::memory_order_relaxed) == pipcore::net::WifiState::Connected ||
                _state.load(std::memory_order_relaxed) == pipcore::net::WifiState::Connecting)
            {
                if (_cfg.autoReconnect && !_userDisconnect.load(std::memory_order_relaxed))
                {
                    const uint32_t attempt = _reconnectAttempt.load(std::memory_order_relaxed) + 1U;
                    const uint32_t capped = (attempt > 5U) ? 5U : attempt;
                    _reconnectAttempt.store(capped, std::memory_order_relaxed);
                    const uint32_t delay = _cfg.retryDelayMs << (capped - 1U);
                    _reconnectAtMs.store(nowMs() + delay, std::memory_order_relaxed);
                    _attemptStartMs.store(nowMs(), std::memory_order_relaxed);
                    setState(pipcore::net::WifiState::Connecting);
                    return;
                }
                setState(pipcore::net::WifiState::Failed);
                _nextRetryMs.store(nowMs() + _cfg.retryDelayMs, std::memory_order_relaxed);
            }
        }
    }

    void Wifi::service(uint32_t now) noexcept
    {
        if (!_requested)
        {
            if (_state.load(std::memory_order_relaxed) != pipcore::net::WifiState::Off || !_hwOffApplied)
                wifiOff();
            return;
        }

        if (!_configured || !_cfg.ssid[0])
        {
            if (_state.load(std::memory_order_relaxed) != pipcore::net::WifiState::Failed)
                setState(pipcore::net::WifiState::Failed);
            return;
        }

        if (_state.load(std::memory_order_relaxed) == pipcore::net::WifiState::Off)
        {
            startConnect(now);
            return;
        }

        if (_state.load(std::memory_order_relaxed) == pipcore::net::WifiState::Failed)
        {
            if (_nextRetryMs.load(std::memory_order_relaxed) == 0)
                _nextRetryMs.store(now + _cfg.retryDelayMs, std::memory_order_relaxed);

            if ((int32_t)(now - _nextRetryMs.load(std::memory_order_relaxed)) >= 0)
            {
                _nextRetryMs.store(0, std::memory_order_relaxed);
                startConnect(now);
            }
            return;
        }

        if (_state.load(std::memory_order_relaxed) == pipcore::net::WifiState::Connecting)
        {

            if (_reconnectAtMs.load(std::memory_order_relaxed) != 0)
            {
                if ((int32_t)(now - _reconnectAtMs.load(std::memory_order_relaxed)) >= 0)
                {
                    _reconnectAtMs.store(0, std::memory_order_relaxed);
                    _attemptStartMs.store(now, std::memory_order_relaxed);
                    esp_wifi_connect();
                }
                return;
            }

            const uint32_t el = now - _attemptStartMs.load(std::memory_order_relaxed);
            if (el >= _cfg.connectTimeoutMs)
            {
                _userDisconnect.store(true, std::memory_order_relaxed);
                esp_wifi_disconnect();
                setState(pipcore::net::WifiState::Failed);
                _nextRetryMs.store(now + _cfg.retryDelayMs, std::memory_order_relaxed);
            }
            return;
        }
    }
}

#endif
