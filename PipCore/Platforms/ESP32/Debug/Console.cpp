#include "Platforms/ESP32/Debug/Console.hpp"
#if PIPCORE_TARGET_ESP32 && PIPCORE_ENABLE_DEBUG && PIPCORE_DEBUG_CONSOLE

#ifndef CONFIG_PIPCORE_DEBUG_CONSOLE_ACCEPT_RISK
#error "PipCore debug console is UNAUTHENTICATED (flash reads, NVS dump, file writes). "\
       "Enable CONFIG_PIPCORE_DEBUG_CONSOLE_ACCEPT_RISK=y for development builds, or "\
       "disable CONFIG_PIPCORE_DEBUG_CONSOLE for production."
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_clk_tree.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_mac.h>
#include <esp_partition.h>
#include <esp_flash.h>
#include <esp_secure_boot.h>
#include <esp_efuse.h>
#include <esp_ota_ops.h>
#include <nvs.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <inttypes.h>

#if PIPCORE_DEBUG_TRANSPORT_UART0
#include <driver/uart.h>
#else
#include <driver/usb_serial_jtag.h>
#endif

#include "Platforms/ESP32/Core/Alloc.hpp"
#include <Debug.hpp>
#include <Log.hpp>
#include <Storage.hpp>

namespace pipcore::debug
{
    namespace
    {
#if PIPCORE_DEBUG_TRANSPORT_UART0
        bool serialBegin() noexcept
        {
            return uart_driver_install(UART_NUM_0, 2048, 0, 0, nullptr, 0) == ESP_OK;
        }

        int serialRead() noexcept
        {
            uint8_t b = 0;
            return (uart_read_bytes(UART_NUM_0, &b, 1, 0) == 1) ? b : -1;
        }

        void serialWrite(const char *data, size_t len) noexcept
        {
            uart_write_bytes(UART_NUM_0, data, len);
        }
#else
        bool serialBegin() noexcept
        {
            usb_serial_jtag_driver_config_t cfg = {};
            cfg.rx_buffer_size = 2048;
            cfg.tx_buffer_size = 2048;
            return usb_serial_jtag_driver_install(&cfg) == ESP_OK;
        }

        int serialRead() noexcept
        {
            uint8_t b = 0;
            return (usb_serial_jtag_read_bytes(&b, 1, 0) == 1) ? b : -1;
        }

        void serialWrite(const char *data, size_t len) noexcept
        {
            usb_serial_jtag_write_bytes(data, len, portMAX_DELAY);
        }
#endif

        void serialPrintf(const char *fmt, ...) noexcept
        {
            char buf[512];
            va_list args;
            va_start(args, fmt);
            const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            if (n > 0)
                serialWrite(buf, static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n) : sizeof(buf) - 1);
        }

        void serialPrint(const char *text) noexcept
        {
            serialWrite(text, std::strlen(text));
        }

        void serialPrint(uint32_t value) noexcept
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(value));
            serialPrint(buf);
        }

        void serialPrintHex(uint32_t value) noexcept
        {
            char buf[12];
            std::snprintf(buf, sizeof(buf), "%" PRIX32, value);
            serialPrint(buf);
        }

        void serialPrintLine(const char *text) noexcept
        {
            serialPrint(text);
            serialPrint("\n");
        }

        [[nodiscard]] uint32_t cpuFreqHz() noexcept
        {
            static uint32_t freq = 0;
            if (freq == 0)
            {
                esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &freq);
            }
            return freq;
        }

        int fastHexVal(char c) noexcept
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        bool storageReady() noexcept
        {
#if PIPCORE_ENABLE_STORAGE
            static bool mounted = false;
            static bool attempted = false;
            if (!attempted)
            {
                attempted = true;
                mounted = storage::begin(false);
            }
            return mounted;
#else
            return false;
#endif
        }

        void listFsFiles() noexcept
        {
            serialPrint("FS:[");
            if (storageReady())
            {
                storage::File root = storage::open("/");
                if (root && root.isDirectory())
                {
                    bool first = true;
                    storage::File file = root.openNextFile();
                    while (file)
                    {
                        if (!file.isDirectory())
                        {
                            if (!first)
                                serialPrint(",");
                            first = false;
                            serialPrint("{\"name\":\"");
                            serialPrint(file.name());
                            serialPrint("\",\"size\":");
                            serialPrint(static_cast<uint32_t>(file.size()));
                            serialPrint("}");
                        }
                        file = root.openNextFile();
                    }
                }
            }
            serialPrintLine("]");
        }

        void getAllocs() noexcept
        {
            Tracker &tracker = Tracker::instance();
            if (!tracker._dirty.exchange(false, std::memory_order_relaxed))
            {
                serialPrintLine("ALLOCS:NO_CHANGE");
                return;
            }

            struct TempAlloc
            {
                void *caller;
                uint32_t size;
                const char *tag;
            };

            TempAlloc active[32];
            size_t activeCount = 0;
            uint32_t total = 0, peak = 0;

            tracker.lock();
            AllocHeader *curr = tracker._head;
            while (curr != nullptr && activeCount < 32)
            {
                active[activeCount++] = {curr->caller, curr->size, curr->tag};
                curr = curr->next;
            }
            total = tracker._totalAllocated.load(std::memory_order_relaxed);
            peak = tracker._peakAllocated;
            tracker.unlock();

            serialPrint("ALLOCS:{\"total\":");
            serialPrint(total);
            serialPrint(",\"peak\":");
            serialPrint(peak);
            serialPrint(",\"tags\":[");
            for (size_t i = 0; i < activeCount; ++i)
            {
                if (i > 0)
                    serialPrint(",");
                serialPrint("{\"tag\":\"");
                serialPrint(active[i].tag ? active[i].tag : "unknown");
                if (active[i].caller != nullptr)
                {
                    serialPrint(" @ 0x");
                    serialPrintHex(reinterpret_cast<uintptr_t>(active[i].caller));
                }
                serialPrint("\",\"bytes\":");
                serialPrint(active[i].size);
                serialPrint(",\"count\":1}");
            }
            serialPrintLine("]}");
        }

        void serialPrintJsonEscaped(const char *text) noexcept
        {
            for (const char *p = text; p && *p; ++p)
            {
                if (*p == '"' || *p == '\\')
                {
                    const char seq[2] = {'\\', *p};
                    serialWrite(seq, 2);
                }
                else
                {
                    serialWrite(p, 1);
                }
            }
        }

        void getNvs() noexcept
        {
            serialPrint("NVS:{\"keys\":[");
            nvs_iterator_t it = nullptr;
            esp_err_t res = nvs_entry_find(nullptr, nullptr, NVS_TYPE_ANY, &it);
            bool first = true;
            while (res == ESP_OK && it != nullptr)
            {
                nvs_entry_info_t info;
                nvs_entry_info(it, &info);

                char val[96] = {};
                nvs_handle_t handle = 0;
                if (nvs_open(info.namespace_name, NVS_READONLY, &handle) == ESP_OK)
                {
                    switch (info.type)
                    {
                    case NVS_TYPE_U8:
                    {
                        uint8_t v = 0;
                        if (nvs_get_u8(handle, info.key, &v) == ESP_OK)
                            std::snprintf(val, sizeof(val), "%u", v);
                        break;
                    }
                    case NVS_TYPE_U32:
                    {
                        uint32_t v = 0;
                        if (nvs_get_u32(handle, info.key, &v) == ESP_OK)
                            std::snprintf(val, sizeof(val), "%lu", static_cast<unsigned long>(v));
                        break;
                    }
                    case NVS_TYPE_U64:
                    {
                        uint64_t v = 0;
                        if (nvs_get_u64(handle, info.key, &v) == ESP_OK)
                            std::snprintf(val, sizeof(val), "%llu", static_cast<unsigned long long>(v));
                        break;
                    }
                    case NVS_TYPE_I32:
                    {
                        int32_t v = 0;
                        if (nvs_get_i32(handle, info.key, &v) == ESP_OK)
                            std::snprintf(val, sizeof(val), "%ld", static_cast<long>(v));
                        break;
                    }
                    case NVS_TYPE_STR:
                    {
                        size_t sz = sizeof(val);
                        if (nvs_get_str(handle, info.key, val, &sz) != ESP_OK)
                            val[0] = '\0';
                        break;
                    }
                    case NVS_TYPE_BLOB:
                    {
                        size_t sz = 0;
                        if (nvs_get_blob(handle, info.key, nullptr, &sz) == ESP_OK)
                            std::snprintf(val, sizeof(val), "<%u bytes>", static_cast<unsigned>(sz));
                        break;
                    }
                    default:
                        break;
                    }
                    nvs_close(handle);
                }

                if (!first)
                    serialPrint(",");
                first = false;
                serialPrint("{\"key\":\"");
                serialPrint(info.key);
                serialPrint("\",\"val\":\"");
                serialPrintJsonEscaped(val);
                serialPrint("\"}");
                res = nvs_entry_next(&it);
            }
            if (it != nullptr)
                nvs_release_iterator(it);
            serialPrintLine("]}");
        }

        void getProfile() noexcept
        {
            Profiler &prof = Profiler::instance();
            prof.calculateSelfCycles();

            struct NodeSnap
            {
                const char *name;
                const ProfileNode *parent;
                uint32_t total;
                uint32_t self;
                uint32_t calls;
                uint32_t max;
            };

            NodeSnap nodes[64];
            size_t count = 0;
            prof.lock();
            for (ProfileNode *curr = prof._head; curr != nullptr && count < 64; curr = curr->next)
                nodes[count++] = {curr->name, curr->parent, static_cast<uint32_t>(curr->totalCycles),
                                  static_cast<uint32_t>(curr->selfCycles), curr->callCount, curr->maxCycles};
            prof.unlock();

            serialPrint("PROFILE:{\"frequency\":");
            serialPrint(cpuFreqHz());
            serialPrint(",\"nodes\":[");

            for (size_t i = 0; i < count; ++i)
            {
                if (i > 0)
                    serialPrint(",");

                int parentIdx = -1;
                if (nodes[i].parent != nullptr)
                {
                    for (size_t j = 0; j < count; ++j)
                    {
                        if (nodes[j].parent == nodes[i].parent && j != i)
                        {
                            parentIdx = static_cast<int>(j);
                            break;
                        }
                    }
                }

                serialPrint("{\"name\":\"");
                serialPrint(nodes[i].name ? nodes[i].name : "unknown");
                serialPrint("\",\"parent\":");
                serialPrint(static_cast<uint32_t>(parentIdx));
                serialPrint(",\"total\":");
                serialPrint(nodes[i].total);
                serialPrint(",\"self\":");
                serialPrint(nodes[i].self);
                serialPrint(",\"calls\":");
                serialPrint(nodes[i].calls);
                serialPrint(",\"max\":");
                serialPrint(nodes[i].max);
                serialPrint("}");
            }
            serialPrintLine("]}");
        }

        void getFlashInfo(uint32_t &size, uint32_t &speed, uint32_t &mode, uint32_t &jedecId) noexcept
        {
            esp_flash_read_id(nullptr, &jedecId);

            uint8_t hdr[5] = {0};
            if (esp_flash_read(nullptr, hdr, 0x0000, sizeof(hdr)) == ESP_OK)
            {
                static constexpr uint32_t kSizes[8] = {1, 2, 4, 8, 16, 32, 64, 128};
                static constexpr uint32_t kSpeeds[16] = {40000000, 26000000, 20000000, 80000000, 0, 0, 0, 0,
                                                         0, 0, 0, 0, 0, 0, 0, 80000000};
                const uint8_t sizeIdx = static_cast<uint8_t>(hdr[3] >> 4);
                const uint8_t speedIdx = static_cast<uint8_t>(hdr[3] & 0x0F);
                size = (sizeIdx < 8) ? kSizes[sizeIdx] * 1024 * 1024 : 0;
                speed = kSpeeds[speedIdx];
                mode = hdr[4];
            }
        }

        void handleGetFile(const char *filepath) noexcept
        {
            if (!storageReady())
            {
                serialPrint("FILE_ERR:{\"name\":\"");
                serialPrint(filepath);
                serialPrintLine("\",\"err\":\"FS mount failed\"}");
                return;
            }

            storage::File f = storage::open(filepath);
            if (!f && filepath[0] != '/')
            {
                char tempPath[64];
                std::snprintf(tempPath, sizeof(tempPath), "/%s", filepath);
                f = storage::open(tempPath);
            }

            if (f)
            {
                serialPrint("FILE_DATA:{\"name\":\"");
                serialPrint(filepath);
                serialPrint("\",\"hex\":\"");
                uint8_t buf[64];
                char hex[sizeof(buf) * 2];
                static constexpr char kHexDigits[] = "0123456789ABCDEF";
                size_t n = 0;
                while ((n = f.read(buf, sizeof(buf))) > 0)
                {
                    for (size_t i = 0; i < n; ++i)
                    {
                        hex[i * 2] = kHexDigits[buf[i] >> 4];
                        hex[i * 2 + 1] = kHexDigits[buf[i] & 0xF];
                    }
                    serialWrite(hex, n * 2);
                }
                serialPrintLine("\"}");
            }
            else
            {
                serialPrint("FILE_ERR:{\"name\":\"");
                serialPrint(filepath);
                serialPrintLine("\",\"err\":\"File not found\"}");
            }
        }

        void serverTask() noexcept
        {
            if (!serialBegin())
                log::error("debug console: serial driver install failed");

            char *line = static_cast<char *>(std::malloc(512));
            if (!line)
                return;

            storage::File uploadFile;
            bool uploadActive = false;

            size_t len = 0;
            bool lineOverflow = false;
            while (true)
            {
                int processed = 0;
                int ic = 0;
                while (processed < 4096 && (ic = serialRead()) >= 0)
                {
                    ++processed;
                    const char c = static_cast<char>(ic);
                    if (c == '\n' || c == '\r')
                    {
                        if (len > 0)
                        {

                            if (lineOverflow)
                            {
                                serialPrintLine("ERR:LINE_TOO_LONG");
                                len = 0;
                                lineOverflow = false;
                                continue;
                            }

                            line[len] = '\0';

                            if (std::strcmp(line, "GET_ALLOCS") == 0)
                            {
                                getAllocs();
                            }
                            else if (std::strcmp(line, "GET_FLASH") == 0)
                            {
                                uint32_t size = 0, speed = 0, mode = 0, jedecId = 0;
                                getFlashInfo(size, speed, mode, jedecId);

                                serialPrint("FLASH:{\"size\":");
                                serialPrint(size);
                                serialPrint(",\"speed\":");
                                serialPrint(speed);
                                serialPrint(",\"mode\":");
                                serialPrint(mode);
                                serialPrint(",\"jedec_id\":");
                                serialPrint(jedecId);
                                serialPrintLine("}");
                            }
                            else if (std::strcmp(line, "GET_NVS") == 0)
                            {
                                getNvs();
                            }
                            else if (std::strcmp(line, "GET_CPU") == 0)
                            {
                                esp_chip_info_t chipInfo;
                                esp_chip_info(&chipInfo);
                                serialPrint("CPU:{\"freq\":");
                                serialPrint(cpuFreqHz() / 1000000U);
                                serialPrint(",\"cores\":");
                                serialPrint(static_cast<uint32_t>(chipInfo.cores));
                                serialPrint(",\"revision\":");
                                serialPrint(static_cast<uint32_t>(chipInfo.revision));
                                serialPrintLine("}");
                            }
                            else if (std::strcmp(line, "GET_PROFILE") == 0)
                            {
                                getProfile();
                            }
                            else if (std::strcmp(line, "RESET_PROFILE") == 0)
                            {
                                Profiler::instance().clear();
                                serialPrintLine("PROFILE:RESET_OK");
                            }
                            else if (std::strcmp(line, "GET_FS") == 0)
                            {
                                listFsFiles();
                            }
                            else if (std::strncmp(line, "GET_FILE:", 9) == 0)
                            {
                                handleGetFile(line + 9);
                            }
                            else if (std::strncmp(line, "WRITE_START:", 12) == 0)
                            {
                                const char *filepath = line + 12;
                                if (uploadActive)
                                {
                                    uploadFile.close();
                                    uploadActive = false;
                                }

                                if (!storageReady())
                                {
                                    serialPrintLine("UPLOAD:WRITE_ERR:FS mount failed");
                                }
                                else
                                {
                                    char path[64];
                                    std::snprintf(path, sizeof(path), "%s%s", (filepath[0] != '/') ? "/" : "", filepath);
                                    uploadFile = storage::open(path, storage::OpenMode::Write);
                                    if (uploadFile)
                                    {
                                        uploadActive = true;
                                        serialPrintLine("UPLOAD:WRITE_OK");
                                    }
                                    else
                                    {
                                        serialPrintLine("UPLOAD:WRITE_ERR:Failed to open file");
                                    }
                                }
                            }
                            else if (std::strncmp(line, "WRITE_CHUNK:", 12) == 0)
                            {
                                const char *hexData = line + 12;
                                if (uploadActive && uploadFile)
                                {
                                    const size_t hexLen = std::strlen(hexData);
                                    bool err = false;
                                    if ((hexLen & 1U) != 0U)
                                    {
                                        err = true;
                                    }
                                    else
                                    {
                                        for (size_t i = 0; i + 1 < hexLen; i += 2)
                                        {
                                            const int hi = fastHexVal(hexData[i]);
                                            const int lo = fastHexVal(hexData[i + 1]);
                                            if (hi < 0 || lo < 0)
                                            {
                                                err = true;
                                                break;
                                            }
                                            const uint8_t byte = static_cast<uint8_t>((hi << 4) | lo);
                                            if (uploadFile.write(&byte, 1) != 1)
                                            {
                                                err = true;
                                                break;
                                            }
                                        }
                                    }
                                    serialPrintLine(err ? "UPLOAD:WRITE_ERR:Invalid hex or flash write failed"
                                                        : "UPLOAD:WRITE_OK");
                                }
                                else
                                {
                                    serialPrintLine("UPLOAD:WRITE_ERR:No active upload session");
                                }
                            }
                            else if (std::strcmp(line, "WRITE_END") == 0)
                            {
                                if (uploadActive)
                                {
                                    uploadFile.close();
                                    uploadActive = false;
                                    serialPrintLine("UPLOAD:WRITE_OK");
                                }
                                else
                                {
                                    serialPrintLine("UPLOAD:WRITE_ERR:No active upload session");
                                }
                            }
                            else if (std::strncmp(line, "DELETE_FILE:", 12) == 0)
                            {
                                const char *filepath = line + 12;
                                if (storageReady())
                                {
                                    char path[64];
                                    std::snprintf(path, sizeof(path), "%s%s", (filepath[0] != '/') ? "/" : "", filepath);
                                    serialPrintLine(storage::remove(path) ? "UPLOAD:DELETE_OK"
                                                                          : "UPLOAD:DELETE_ERR:Delete failed");
                                }
                                else
                                {
                                    serialPrintLine("UPLOAD:DELETE_ERR:FS mount failed");
                                }
                            }
                            else if (std::strcmp(line, "GET_PARTITIONS") == 0)
                            {
                                esp_partition_iterator_t it =
                                    esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
                                serialPrint("PARTITIONS:[");
                                bool first = true;
                                while (it != nullptr)
                                {
                                    const esp_partition_t *p = esp_partition_get(it);
                                    if (!first)
                                        serialPrint(",");
                                    first = false;

                                    const char *color = "part-free";
                                    if (p->type == ESP_PARTITION_TYPE_APP)
                                        color = "part-app";
                                    else if (p->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS)
                                        color = "part-nvs";
                                    else if (p->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS ||
                                             p->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT ||
                                             p->subtype == 0x83)
                                        color = "part-fs";
                                    else if (p->subtype == ESP_PARTITION_SUBTYPE_DATA_OTA)
                                        color = "part-pt";

                                    const uint32_t flags = p->encrypted ? 0x01 : 0x00;
                                    serialPrintf("{\"name\":\"%s\",\"type\":\"%d\",\"subtype\":\"%d\",\"offset\":%lu,\"size\":%"
                                                 "lu,\"flags\":%lu,\"color\":\"%s\"}",
                                                 p->label, p->type, p->subtype, static_cast<unsigned long>(p->address),
                                                 static_cast<unsigned long>(p->size), static_cast<unsigned long>(flags), color);
                                    it = esp_partition_next(it);
                                }
                                serialPrintLine("]");
                            }
                            else if (std::strncmp(line, "READ_FLASH:", 11) == 0)
                            {
                                unsigned long offset = 0;
                                unsigned long size = 0;
                                if (std::sscanf(line + 11, "%lu,%lu", &offset, &size) == 2)
                                {
                                    if (size > 1024)
                                        size = 1024;
                                    uint8_t *temp = static_cast<uint8_t *>(std::malloc(size));
                                    if (!temp)
                                    {
                                        serialPrint("HEX_DATA:{\"offset\":");
                                        serialPrint(offset);
                                        serialPrintLine(",\"hex\":\"\"}");
                                    }
                                    else
                                    {
                                        if (esp_flash_read(nullptr, temp, offset, size) == ESP_OK)
                                        {
                                            serialPrint("HEX_DATA:{\"offset\":");
                                            serialPrint(offset);
                                            serialPrint(",\"hex\":\"");
                                            static constexpr char kHexDigits[] = "0123456789ABCDEF";
                                            char hex[128];
                                            size_t n = 0;
                                            for (uint32_t i = 0; i < size; ++i)
                                            {
                                                hex[n++] = kHexDigits[temp[i] >> 4];
                                                hex[n++] = kHexDigits[temp[i] & 0xF];
                                                if (n == sizeof(hex))
                                                {
                                                    serialWrite(hex, n);
                                                    n = 0;
                                                }
                                            }
                                            if (n > 0)
                                                serialWrite(hex, n);
                                            serialPrintLine("\"}");
                                        }
                                        else
                                        {
                                            serialPrint("HEX_DATA:{\"offset\":");
                                            serialPrint(offset);
                                            serialPrintLine(",\"hex\":\"\"}");
                                        }
                                        std::free(temp);
                                    }
                                }
                            }
                            else if (std::strcmp(line, "GET_BOOT_SECURITY") == 0)
                            {
                                uint8_t bootHeader[8] = {0};
                                uint8_t magic = 0x00;
                                uint32_t entryPoint = 0x0;
                                uint8_t segments = 0;

                                const esp_partition_t *running = esp_ota_get_running_partition();
                                const uint32_t appOffset = running ? running->address : 0x10000;

                                if (esp_flash_read(nullptr, bootHeader, appOffset, sizeof(bootHeader)) == ESP_OK)
                                {
                                    magic = bootHeader[0];
                                    segments = bootHeader[1];
                                    entryPoint =
                                        (bootHeader[7] << 24) | (bootHeader[6] << 16) | (bootHeader[5] << 8) | bootHeader[4];
                                }

                                const bool cryptoEnabled = esp_efuse_is_flash_encryption_enabled();
                                const bool secureBootEnabled = esp_secure_boot_enabled();
                                const char *activeSlot = running ? running->label : "factory";

                                uint8_t mac[6] = {0};
                                esp_efuse_mac_get_default(mac);
                                char macStr[18];
                                std::snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
                                              mac[3], mac[4], mac[5]);

                                char uidStr[20];
                                std::snprintf(uidStr, sizeof(uidStr), "0x%02X%02X%02X%02X%02X%02X", mac[5], mac[4], mac[3],
                                              mac[2], mac[1], mac[0]);

                                serialPrintf(
                                    "BOOT_SEC:{\"magic\":\"0x%02X\",\"entry\":\"0x%08X\",\"segments\":%u,\"crypto\":%s,"
                                    "\"secure_boot\":%s,\"active_slot\":\"%s\",\"mac\":\"%s\",\"unique_id\":\"%s\"}\n",
                                    magic, entryPoint, segments, cryptoEnabled ? "true" : "false",
                                    secureBootEnabled ? "true" : "false", activeSlot, macStr, uidStr);
                            }
                            len = 0;
                            lineOverflow = false;
                        }
                    }
                    else if (len < 511)
                    {
                        line[len++] = c;
                    }
                    else
                    {
                        lineOverflow = true;
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(processed != 0 ? 1 : 20));
            }
        }
    }

    Console &Console::instance() noexcept
    {
        static Console console;
        return console;
    }

    void Console::begin() noexcept
    {
        if (_started)
            return;
        _started = true;

        if (xTaskCreatePinnedToCore([](void *)
                                    { serverTask(); }, "PipCoreConsole",
                                    static_cast<configSTACK_DEPTH_TYPE>(CONFIG_PIPCORE_DEBUG_CONSOLE_STACK), nullptr, 1,
                                    nullptr, tskNO_AFFINITY) != pdPASS)
        {
            _started = false;
            log::error("debug console: failed to create task");
        }
    }
}

#endif
