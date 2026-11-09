/*---------------------------------------------------------*\
| ThermalrightSystemMetrics.h                               |
|                                                           |
|   Telemetry values shown on the PA120 segment display.     |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-only                   |
\*---------------------------------------------------------*/

#pragma once

struct ThermalrightSystemMetrics
{
    int  cpu_temp_c     = 0;
    int  gpu_temp_c     = 0;
    int  cpu_load_pct   = 0;
    int  gpu_load_pct   = 0;
    bool cpu_load_valid = false;
    bool gpu_load_valid = false;
    bool cpu_temp_valid = false;
    bool gpu_temp_valid = false;
};

