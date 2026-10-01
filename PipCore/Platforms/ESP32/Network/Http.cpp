#include "Config.hpp"
#if PIPCORE_ENABLE_OTA
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cstring>
#include <cstdlib>

#include "Platforms/ESP32/Network/Internal.hpp"

namespace pipcore::esp32::services
{
    using namespace detail;

    bool Ota::beginHttp(const char *url, bool forManifest) noexcept
    {
        stopHttp();
        if (!url || !url[0])
            return false;

        if (forManifest)
        {
            _st.downloaded = 0;
            _st.total = 0;
            _manifestLen = 0;

            if (!_manifestBuf)
            {
                _manifestBuf = static_cast<uint8_t *>(std::malloc(kManifestLimitBytes + 1));
                if (!_manifestBuf)
                {
                    _st.platformCode = -1;
                    return false;
                }
            }
        }

        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = CONFIG_PIPCORE_OTA_HTTP_TIMEOUT_MS;
        cfg.buffer_size = 16384;
        cfg.buffer_size_tx = 1024;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif
        cfg.disable_auto_redirect = false;

        _http.client = esp_http_client_init(&cfg);
        if (!_http.client)
        {
            _st.platformCode = -1;
            return false;
        }

        const esp_err_t openErr = esp_http_client_open(_http.client, 0);
        if (openErr != ESP_OK)
        {
            _st.httpCode = 0;
            _st.platformCode = static_cast<int>(openErr);
            stopHttp();
            return false;
        }

        const int64_t length = esp_http_client_fetch_headers(_http.client);
        const int code = esp_http_client_get_status_code(_http.client);
        _st.httpCode = code;
        if (code != 200)
        {
            _st.platformCode = 0;
            stopHttp();
            return false;
        }

        _http.active = true;
        _http.forManifest = forManifest;
        _st.platformCode = 0;
        _http.chunked = length < 0;
        _http.bodyTotal = length > 0 ? static_cast<uint32_t>(length) : 0;
        _http.bodyRead = 0;
        if (forManifest)
        {
            _st.downloaded = 0;
            _st.total = 0;
        }
        return true;
    }

    bool Ota::readHttpBody() noexcept
    {
        if (!_http.active || !_http.forManifest)
            return false;

        size_t budget = 1024;
        while (budget > 0)
        {
            const size_t space = kManifestLimitBytes > _manifestLen ? (kManifestLimitBytes - _manifestLen) : 0;
            if (space == 0)
            {
                _manifestLen = kManifestLimitBytes + 1;
                return true;
            }

            size_t chunk = 256;
            if (chunk > space)
                chunk = space;
            if (chunk > budget)
                chunk = budget;

            const int read = esp_http_client_read(_http.client, reinterpret_cast<char *>(_manifestBuf + _manifestLen),
                                                  static_cast<int>(chunk));
            if (read < 0)
                return false;
            if (read == 0)
                break;

            _manifestLen += static_cast<size_t>(read);
            _http.bodyRead += static_cast<uint32_t>(read);
            budget -= static_cast<size_t>(read);
        }

        if (_http.chunked)
            return esp_http_client_is_complete_data_received(_http.client) != 0;

        if (_http.bodyTotal > 0 && _http.bodyRead >= _http.bodyTotal)
            return true;

        return false;
    }
}

#endif
