/*---------------------------------------------------------*\
| ThermalrightLhmReader.cpp                                 |
|                                                           |
|   Reads LHM http://127.0.0.1:8085/data.json (keepalive).    |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-only                   |
\*---------------------------------------------------------*/

#include <cctype>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

#include "ThermalrightLhmReader.h"

namespace
{
    constexpr unsigned short LHM_DEFAULT_PORT = 8085;

    int ClampPct(int value)
    {
        if(value < 0)
        {
            return(0);
        }

        if(value > 100)
        {
            return(100);
        }

        return(value);
    }

    int ClampTemp(int value)
    {
        if(value < 0)
        {
            return(0);
        }

        if(value > 110)
        {
            return(110);
        }

        return(value);
    }

    bool StrContainsInsensitive(const std::string& haystack, const char* needle)
    {
        if(needle == nullptr)
        {
            return(false);
        }

        std::string lower_haystack(haystack);
        std::string lower_needle(needle);

        for(char& ch : lower_haystack)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        for(char& ch : lower_needle)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        return(lower_haystack.find(lower_needle) != std::string::npos);
    }

    bool IsCpuHardwarePath(const std::string& sensor_id)
    {
        return(StrContainsInsensitive(sensor_id, "/amdcpu/")
            || StrContainsInsensitive(sensor_id, "/intelcpu/"));
    }

    bool IsGpuHardwarePath(const std::string& sensor_id)
    {
        return(StrContainsInsensitive(sensor_id, "/gpu-nvidia/")
            || StrContainsInsensitive(sensor_id, "/gpu-amd/")
            || StrContainsInsensitive(sensor_id, "/gpu-intel/"));
    }

    std::string SensorDisplayLabel(const std::string& sensor_id, const std::string& sensor_text)
    {
        if(!sensor_text.empty())
        {
            return(sensor_text);
        }

        const size_t slash = sensor_id.find_last_of('/');

        if(slash != std::string::npos && slash + 1 < sensor_id.size())
        {
            return(sensor_id.substr(slash + 1));
        }

        return(sensor_id);
    }

    void AppendUniqueOption(
        std::vector<ThermalrightLhmSensorOption>& options,
        const std::string& sensor_id,
        const std::string& label)
    {
        if(sensor_id.empty() || label.empty())
        {
            return;
        }

        for(const ThermalrightLhmSensorOption& existing : options)
        {
            if(existing.sensor_id == sensor_id)
            {
                return;
            }
        }

        options.push_back({ sensor_id, label });
    }

    size_t FindJsonKey(const std::string& json, const char* key, size_t from_pos)
    {
        const std::string pattern = std::string("\"") + key + "\"";

        for(size_t pos = from_pos; pos < json.size();)
        {
            pos = json.find(pattern, pos);

            if(pos == std::string::npos)
            {
                return(std::string::npos);
            }

            if(pos > 0 && json[pos - 1] == '\\')
            {
                pos += pattern.size();
                continue;
            }

            return(pos);
        }

        return(std::string::npos);
    }

    std::string ExtractJsonQuotedString(const std::string& json, size_t key_pos)
    {
        const size_t colon = json.find(':', key_pos);

        if(colon == std::string::npos)
        {
            return("");
        }

        size_t quote_start = json.find('"', colon + 1);

        if(quote_start == std::string::npos)
        {
            return("");
        }

        size_t quote_end = quote_start + 1;

        while(quote_end < json.size())
        {
            quote_end = json.find('"', quote_end);

            if(quote_end == std::string::npos)
            {
                return("");
            }

            if(json[quote_end - 1] != '\\')
            {
                break;
            }

            quote_end++;
        }

        return(json.substr(quote_start + 1, quote_end - quote_start - 1));
    }

    bool ParseFloatString(const std::string& text, float& value)
    {
        if(text.empty())
        {
            return(false);
        }

        std::string numeric;

        for(char ch : text)
        {
            if((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+')
            {
                numeric.push_back(ch);
            }
            else if(!numeric.empty())
            {
                break;
            }
        }

        if(numeric.empty())
        {
            return(false);
        }

        try
        {
            value = std::stof(numeric);
            return(true);
        }
        catch(...)
        {
            return(false);
        }
    }

    bool ExtractJsonValueField(const std::string& json, size_t key_pos, float& value)
    {
        const size_t colon = json.find(':', key_pos);

        if(colon == std::string::npos)
        {
            return(false);
        }

        size_t pos = colon + 1;

        while(pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
        {
            pos++;
        }

        if(pos >= json.size())
        {
            return(false);
        }

        if(json[pos] == '"')
        {
            const std::string text = ExtractJsonQuotedString(json, key_pos);
            return(ParseFloatString(text, value));
        }

        if(json[pos] == 'n')
        {
            return(false);
        }

        size_t end = pos;

        while(end < json.size())
        {
            const char ch = json[end];

            if(ch == ',' || ch == '}' || ch == ']')
            {
                break;
            }

            end++;
        }

        return(ParseFloatString(json.substr(pos, end - pos), value));
    }

    std::string ExtractPrecedingJsonText(const std::string& json, size_t sensor_key)
    {
        const size_t search_from = sensor_key > 1024 ? sensor_key - 1024 : 0;
        size_t       best_pos    = std::string::npos;
        size_t       pos         = search_from;

        while(true)
        {
            pos = FindJsonKey(json, "Text", pos);

            if(pos == std::string::npos || pos >= sensor_key)
            {
                break;
            }

            best_pos = pos;
            pos     += 6;
        }

        if(best_pos == std::string::npos)
        {
            return("");
        }

        return(ExtractJsonQuotedString(json, best_pos));
    }

    bool ExtractSensorNumericValue(const std::string& json, size_t sensor_key, float& value)
    {
        const size_t raw_value_key = FindJsonKey(json, "RawValue", sensor_key);

        if(raw_value_key != std::string::npos && raw_value_key < sensor_key + 384
            && ExtractJsonValueField(json, raw_value_key, value))
        {
            return(true);
        }

        const size_t search_from = sensor_key > 1024 ? sensor_key - 1024 : 0;
        size_t       best_pos    = std::string::npos;
        size_t       pos         = search_from;

        while(true)
        {
            pos = FindJsonKey(json, "Value", pos);

            if(pos == std::string::npos || pos >= sensor_key)
            {
                break;
            }

            best_pos = pos;
            pos     += 8;
        }

        if(best_pos == std::string::npos)
        {
            return(false);
        }

        return(ExtractJsonValueField(json, best_pos, value));
    }

    void ClassifySensor(
        const std::string& sensor_id,
        const std::string& sensor_text,
        ThermalrightLhmSensorCatalog& catalog)
    {
        const bool           is_temp = StrContainsInsensitive(sensor_id, "/temperature/");
        const bool           is_load = StrContainsInsensitive(sensor_id, "/load/");
        const std::string    label   = SensorDisplayLabel(sensor_id, sensor_text);

        if(is_temp && IsCpuHardwarePath(sensor_id)
            && !StrContainsInsensitive(label, "distance to tjmax"))
        {
            AppendUniqueOption(catalog.cpu_temp_sensors, sensor_id, label);
        }

        if(is_load && IsCpuHardwarePath(sensor_id))
        {
            AppendUniqueOption(catalog.cpu_load_sensors, sensor_id, label);
        }

        if(is_temp && IsGpuHardwarePath(sensor_id))
        {
            AppendUniqueOption(catalog.gpu_temp_sensors, sensor_id, label);
        }

        if(is_load && IsGpuHardwarePath(sensor_id))
        {
            AppendUniqueOption(catalog.gpu_load_sensors, sensor_id, label);
        }
    }

#ifdef _WIN32
    std::string FetchLocalhostJson(unsigned short port, const wchar_t* path)
    {
        std::string body;

        HINTERNET session = WinHttpOpen(
            L"OpenRGBThermalrightPlugin/1.8.3",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);

        if(session == nullptr)
        {
            return(body);
        }

        WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000);

        HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", port, 0);

        if(connection == nullptr)
        {
            WinHttpCloseHandle(session);
            return(body);
        }

        HINTERNET request = WinHttpOpenRequest(
            connection,
            L"GET",
            path,
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            0);

        if(request == nullptr)
        {
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return(body);
        }

        if(!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            || !WinHttpReceiveResponse(request, nullptr))
        {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return(body);
        }

        DWORD available = 0;

        do
        {
            if(!WinHttpQueryDataAvailable(request, &available) || available == 0)
            {
                break;
            }

            std::string chunk(available, '\0');
            DWORD       read_bytes = 0;

            if(!WinHttpReadData(request, chunk.data(), available, &read_bytes) || read_bytes == 0)
            {
                break;
            }

            chunk.resize(read_bytes);
            body += chunk;
        } while(available > 0);

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        return(body);
    }
#endif

    bool ParseJsonSensors(
        const std::string& json,
        ThermalrightLhmSensorCatalog* catalog,
        const ThermalrightLhmSensorBindings* bindings,
        ThermalrightSystemMetrics* metrics,
        bool* updated)
    {
        bool saw_cpu_temp = false;
        bool saw_cpu_load = false;
        bool saw_gpu_temp = false;
        bool saw_gpu_load = false;

        size_t search_pos = 0;

        while(true)
        {
            const size_t sensor_key = FindJsonKey(json, "SensorId", search_pos);

            if(sensor_key == std::string::npos)
            {
                break;
            }

            const std::string sensor_id   = ExtractJsonQuotedString(json, sensor_key);
            const std::string sensor_text = ExtractPrecedingJsonText(json, sensor_key);
            float             value       = 0.0f;
            const bool        has_value   = ExtractSensorNumericValue(json, sensor_key, value);

            if(!sensor_id.empty())
            {
                if(catalog != nullptr)
                {
                    ClassifySensor(sensor_id, sensor_text, *catalog);
                }

                if(bindings != nullptr && metrics != nullptr && has_value)
                {
                    if(!bindings->cpu_temp_sensor_id.empty() && sensor_id == bindings->cpu_temp_sensor_id && value < 200.0f)
                    {
                        const int temp_c = ClampTemp(static_cast<int>(value + 0.5f));

                        if(temp_c != metrics->cpu_temp_c)
                        {
                            metrics->cpu_temp_c     = temp_c;
                            metrics->cpu_temp_valid = true;
                            *updated                = true;
                        }

                        saw_cpu_temp = true;
                    }

                    if(!bindings->cpu_load_sensor_id.empty() && sensor_id == bindings->cpu_load_sensor_id)
                    {
                        const int load_pct = ClampPct(static_cast<int>(value + 0.5f));

                        if(load_pct != metrics->cpu_load_pct)
                        {
                            metrics->cpu_load_pct = load_pct;
                            *updated              = true;
                        }

                        metrics->cpu_load_valid = true;
                        saw_cpu_load = true;
                    }

                    if(!bindings->gpu_temp_sensor_id.empty() && sensor_id == bindings->gpu_temp_sensor_id && value < 200.0f)
                    {
                        const int temp_c = ClampTemp(static_cast<int>(value + 0.5f));

                        if(temp_c != metrics->gpu_temp_c)
                        {
                            metrics->gpu_temp_c     = temp_c;
                            metrics->gpu_temp_valid = true;
                            *updated                = true;
                        }

                        saw_gpu_temp = true;
                    }

                    if(!bindings->gpu_load_sensor_id.empty() && sensor_id == bindings->gpu_load_sensor_id)
                    {
                        const int load_pct = ClampPct(static_cast<int>(value + 0.5f));

                        if(load_pct != metrics->gpu_load_pct)
                        {
                            metrics->gpu_load_pct = load_pct;
                            *updated              = true;
                        }

                        metrics->gpu_load_valid = true;
                        saw_gpu_load = true;
                    }
                }
            }

            search_pos = sensor_key + 8;
        }

        if(bindings == nullptr || metrics == nullptr)
        {
            return(true);
        }

        return(saw_cpu_temp || saw_cpu_load || saw_gpu_temp || saw_gpu_load || *updated);
    }
}

ThermalrightLhmReader::ThermalrightLhmReader()
{
}

ThermalrightLhmReader::~ThermalrightLhmReader()
{
    Shutdown();
}

bool ThermalrightLhmReader::FetchSensorCatalog(ThermalrightLhmSensorCatalog& catalog)
{
#ifdef _WIN32
    const std::string json = FetchLocalhostJson(LHM_DEFAULT_PORT, L"/data.json");

    if(json.empty())
    {
        return(false);
    }

    catalog.cpu_temp_sensors.clear();
    catalog.cpu_load_sensors.clear();
    catalog.gpu_temp_sensors.clear();
    catalog.gpu_load_sensors.clear();

    return(ParseJsonSensors(json, &catalog, nullptr, nullptr, nullptr));
#else
    return(false);
#endif
}

bool ThermalrightLhmReader::TryRead(const ThermalrightLhmSensorBindings& bindings, ThermalrightSystemMetrics& metrics)
{
#ifdef _WIN32
    const std::string json = FetchLocalhostJson(LHM_DEFAULT_PORT, L"/data.json");

    if(json.empty())
    {
        return(false);
    }

    bool updated = false;
    return(ParseJsonSensors(json, nullptr, &bindings, &metrics, &updated));
#else
    return(false);
#endif
}

void ThermalrightLhmReader::Shutdown()
{
}

