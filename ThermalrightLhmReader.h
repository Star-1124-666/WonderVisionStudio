/*---------------------------------------------------------*\
| ThermalrightLhmReader.h                                   |
|                                                           |
|   LHM Remote Web Server only (http://127.0.0.1:8085).     |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-only                   |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>

#include "ThermalrightSystemMetrics.h"

struct ThermalrightLhmSensorOption
{
    std::string sensor_id;
    std::string label;
};

struct ThermalrightLhmSensorBindings
{
    std::string cpu_temp_sensor_id;
    std::string cpu_load_sensor_id;
    std::string gpu_temp_sensor_id;
    std::string gpu_load_sensor_id;
};

struct ThermalrightLhmSensorCatalog
{
    std::vector<ThermalrightLhmSensorOption> cpu_temp_sensors;
    std::vector<ThermalrightLhmSensorOption> cpu_load_sensors;
    std::vector<ThermalrightLhmSensorOption> gpu_temp_sensors;
    std::vector<ThermalrightLhmSensorOption> gpu_load_sensors;
};

class ThermalrightLhmReader
{
public:
    ThermalrightLhmReader();
    ~ThermalrightLhmReader();

    ThermalrightLhmReader(const ThermalrightLhmReader&) = delete;
    ThermalrightLhmReader& operator=(const ThermalrightLhmReader&) = delete;

    bool FetchSensorCatalog(ThermalrightLhmSensorCatalog& catalog);
    bool TryRead(const ThermalrightLhmSensorBindings& bindings, ThermalrightSystemMetrics& metrics);

    void Shutdown();
};
