#include "Config.hpp"
#if PIPCORE_ENABLE_OTA
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_flash.h>
#include <cstdlib>
#include <cstring>

#include "Platforms/ESP32/Network/Internal.hpp"

namespace pipcore::esp32::services
{
    using namespace detail;

    bool Ota::beginFirmwareDownload(uint32_t nowMs) noexcept
    {
        const pipcore::ota::Manifest &manifest = _st.manifest;

        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *part = esp_ota_get_next_update_partition(running);
        if (!part)
        {
            setError(pipcore::ota::Error::FlashLayoutInvalid, nowMs);
            return false;
        }

        uint32_t flashSize = 0;
        if (esp_flash_get_size(nullptr, &flashSize) != ESP_OK)
            flashSize = 0;
        const uint64_t partEnd = static_cast<uint64_t>(part->address) + static_cast<uint64_t>(part->size);
        if (flashSize == 0 || partEnd > flashSize || part->size < manifest.size)
        {
            setError(pipcore::ota::Error::FlashLayoutInvalid, nowMs);
            return false;
        }

        if (!beginHttp(manifest.url, false))
        {
            failHttpOpen(nowMs);
            return false;
        }

        _st.downloaded = 0;
        _st.total = manifest.size;

        if (!_http.shaInit)
        {
            static bool psaReady = false;
            if (!psaReady)
            {
                if (psa_crypto_init() != PSA_SUCCESS)
                {
                    setError(pipcore::ota::Error::HashPipelineFailed, nowMs);
                    return false;
                }
                psaReady = true;
            }

            _http.sha = psa_hash_operation_init();
            if (psa_hash_setup(&_http.sha, PSA_ALG_SHA_256) != PSA_SUCCESS)
            {
                setError(pipcore::ota::Error::HashPipelineFailed, nowMs);
                return false;
            }
            _http.shaInit = true;
        }

        const esp_err_t beginErr = esp_ota_begin(part, manifest.size, &_http.otaHandle);
        if (beginErr != ESP_OK)
        {
            setError(pipcore::ota::Error::UpdateBeginFailed, nowMs, 0, static_cast<int>(beginErr));
            return false;
        }

        _http.updateStarted = true;

        _http.dlBuf = static_cast<uint8_t *>(std::malloc(16 * 1024));
        if (!_http.dlBuf)
        {
            setError(pipcore::ota::Error::UpdateBeginFailed, nowMs, 0, -1);
            return false;
        }

        setState(pipcore::ota::State::Downloading, nowMs);
        return true;
    }

    bool Ota::stepFirmwareDownload(uint32_t nowMs) noexcept
    {
        if (!_http.active || _http.forManifest)
        {
            setError(pipcore::ota::Error::UpdateWriteFailed, nowMs);
            return false;
        }

        constexpr size_t kDlChunk = 16 * 1024;
        size_t budget = 64 * 1024;
        bool drained = false;
        while (budget > 0 && !drained)
        {
            const uint32_t remaining = _st.total > _st.downloaded ? (_st.total - _st.downloaded) : 0;
            if (remaining == 0)
                break;

            size_t chunk = kDlChunk;
            if (chunk > remaining)
                chunk = remaining;
            if (chunk > budget)
                chunk = budget;

            const int read = esp_http_client_read(_http.client, reinterpret_cast<char *>(_http.dlBuf), static_cast<int>(chunk));
            if (read < 0)
            {
                setError(pipcore::ota::Error::UpdateWriteFailed, nowMs);
                return false;
            }
            if (read == 0)
            {
                drained = true;
                break;
            }

            if (psa_hash_update(&_http.sha, _http.dlBuf, static_cast<size_t>(read)) != PSA_SUCCESS)
            {
                setError(pipcore::ota::Error::HashPipelineFailed, nowMs);
                return false;
            }

            const esp_err_t writeErr = esp_ota_write(_http.otaHandle, _http.dlBuf, static_cast<size_t>(read));
            if (writeErr != ESP_OK)
            {
                setError(pipcore::ota::Error::UpdateWriteFailed, nowMs, 0, static_cast<int>(writeErr));
                return false;
            }

            _st.downloaded += static_cast<uint32_t>(read);
            budget -= static_cast<size_t>(read);
            if (_st.downloaded > _st.total)
            {
                setError(pipcore::ota::Error::PayloadSizeMismatch, nowMs);
                return false;
            }
        }

        if (_st.downloaded < _st.total)
        {
            if (drained && (!(_http.chunked && esp_http_client_is_complete_data_received(_http.client))))
            {
                setError(pipcore::ota::Error::DownloadTruncated, nowMs);
                return false;
            }
            return true;
        }

        uint8_t hash[32] = {};
        size_t hashLen = 0;
        if (psa_hash_finish(&_http.sha, hash, sizeof(hash), &hashLen) != PSA_SUCCESS || hashLen != sizeof(hash))
        {
            _http.shaInit = false;
            setError(pipcore::ota::Error::HashPipelineFailed, nowMs);
            return false;
        }
        _http.shaInit = false;

        if (std::memcmp(hash, _st.manifest.sha256, sizeof(hash)) != 0)
        {
            setError(pipcore::ota::Error::HashMismatch, nowMs);
            return false;
        }

        setState(pipcore::ota::State::Installing, nowMs);
        const esp_err_t endErr = esp_ota_end(_http.otaHandle);
        if (endErr != ESP_OK)
        {
            _http.updateStarted = false;
            setError(pipcore::ota::Error::UpdateEndFailed, nowMs, 0, static_cast<int>(endErr));
            return false;
        }

        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *part = esp_ota_get_next_update_partition(running);
        const esp_err_t bootErr = part ? esp_ota_set_boot_partition(part) : ESP_ERR_INVALID_STATE;
        if (bootErr != ESP_OK)
        {
            _http.updateStarted = false;
            setError(pipcore::ota::Error::UpdateEndFailed, nowMs, 0, static_cast<int>(bootErr));
            return false;
        }

        _http.updateStarted = false;
        stopHttp();
        wifiRelease();
        setState(pipcore::ota::State::Success, nowMs);
        return true;
    }
}

#endif
