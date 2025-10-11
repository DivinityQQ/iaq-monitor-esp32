# Sensor Fusion & Derived Metrics Implementation Plan

## Overview
Add intelligent sensor fusion (cross-sensor compensation) and derived metrics (EPA AQI, thermal comfort, pressure trends) to the ESP32 IAQ monitor by extending the existing clean architecture.

## Architecture Decision: **Extend, Don't Duplicate**

**✅ DO**:
- Extend `sensor_coordinator` with fusion logic (post-processing after reads)
- Extend `iaq_data` with new fields (single source of truth)
- Publish compensated values by default

**❌ DON'T**:
- Create separate fusion/metrics tasks
- Create parallel data structures
- Publish raw values to primary HA entities

### Rationale
- Sensor coordinator already owns sensor lifecycle, scheduling, and `iaq_data` updates
- Adding fusion as post-processing (raw → compensate → update) keeps logic centralized
- Metrics calculations are lightweight (can run in coordinator context)
- Avoids queues, extra tasks, and synchronization complexity

---

## Data Flow Architecture

```
┌─────────────────┐
│ Sensor Drivers  │
│ (read raw data) │
└────────┬────────┘
         │
         ▼
┌─────────────────────────────┐
│  Sensor Coordinator Task    │
│  ┌────────────────────────┐ │
│  │ 1. Read all sensors    │ │
│  │ 2. Apply fusion        │ │── Compensations:
│  │    (compensate)        │ │   • PM RH correction
│  │ 3. Calculate metrics   │ │   • CO2 pressure compensation
│  │    (AQI, comfort)      │ │   • Temperature self-heating offset
│  │ 4. Update iaq_data     │ │   • CO2 ABC baseline tracking
│  └────────────────────────┘ │
└────────┬────────────────────┘
         │
         ▼
┌─────────────────────────────┐
│       iaq_data_t            │
│  ┌──────────────────────┐   │
│  │ .fused (compensated) │   │
│  │ .metrics (AQI, etc.) │   │
│  │ .fusion_diagnostics  │   │
│  └──────────────────────┘   │
└────────┬────────────────────┘
         │
         ▼
┌─────────────────────────────┐
│      MQTT Publisher         │
│  Topics:                    │
│  • /state (compensated)     │
│  • /metrics (detailed)      │
│  • /diagnostics (optional)  │
└─────────────────────────────┘
```

---

## PM1.0 Strategy

**Decision**: Collect but don't promote to primary HA entities

**Usage**:
- ✅ Use for sensor health checks: `pm1/pm25` ratio should be 0.6-0.9
- ✅ Include in diagnostics payload
- ✅ Flag sensor drift if ratio is abnormal
- ❌ Don't create HA sensor entity by default (reduces clutter)
- ❌ Don't use in AQI calculation (EPA only uses PM2.5/PM10)

**Kconfig**: `CONFIG_MQTT_PUBLISH_PM1` (default: n)

---

## Additional Metrics Strategy

### Tier 1: Implemented (High Value, Low Complexity)
1. ✅ **Absolute Humidity** (g/m³) - moisture content, mold risk input
2. ✅ **VOC/NOx Categories** - user-friendly interpretation of SGP41 indices
3. ✅ **CO2 Rate of Change** (ppm/hr) - occupancy detection, ventilation failure
4. ✅ **PM2.5 Spike Detection** - cooking, smoking, outdoor pollution events
5. ✅ **Mold Risk Index** (0-100) - combines RH, dew point, duration

### Tier 2: Future Enhancements (Medium Complexity, Nice-to-Have)
- ⏳ **Humidex** - Canadian comfort index (complements heat index)
- ⏳ **Predicted Time to Threshold** - "CO2 will exceed 1000 ppm in 23 min"
- ⏳ **Ventilation Efficiency Score** - ACH estimation, fresh air effectiveness
- ⏳ **Sleep Quality Index** - nighttime IAQ scoring (bedroom optimization)

**Rationale**: Tier 1 adds ~110 LOC, ~60 bytes RAM, <1% CPU overhead while significantly increasing practical value (mold prevention, real-time alerts, better VOC interpretation).

---

## Pressure Trend

**Purpose**:
- Weather context for UI (↗↘→ arrows)
- Ventilation strategy hints (falling pressure → trapped pollutants)
- Building diagnostics (indoor/outdoor pressure correlation)

**Calculation**:
- Ring buffer: 72 samples (3 hours at 2.5-minute intervals)
- Method: Simple delta or linear regression
- Thresholds: > +0.5 hPa/3hr = RISING, < -0.5 hPa/3hr = FALLING

**Output**:
```c
typedef enum {
    PRESSURE_TREND_RISING,
    PRESSURE_TREND_STABLE,
    PRESSURE_TREND_FALLING,
    PRESSURE_TREND_UNKNOWN
} pressure_trend_t;
```

---

## MQTT Publishing Strategy

### Primary Telemetry: `iaq/{device_id}/state` (30s interval)
**Compensated values only** (what users should see):
```json
{
  "temp_c": 22.3,
  "rh_pct": 45.2,
  "pressure_pa": 101325,
  "pm25_ugm3": 8.5,
  "pm10_ugm3": 12.1,
  "co2_ppm": 687,
  "aqi": 35,
  "comfort_score": 82
}
```

### Metrics Detail: `iaq/{device_id}/metrics` (30s interval)
**Detailed breakdown**:
```json
{
  "aqi": {
    "value": 35,
    "category": "Good",
    "dominant": "pm25",
    "pm25_subindex": 35.2,
    "pm10_subindex": 28.1
  },
  "comfort": {
    "score": 82,
    "category": "Comfortable",
    "dew_point_c": 14.2,
    "heat_index_c": 22.8
  },
  "pressure": {
    "trend": "stable",
    "delta_3hr_hpa": -0.2
  },
  "co2_score": 85,
  "overall_iaq": 78
}
```

### Diagnostics (Optional): `iaq/{device_id}/diagnostics` (5min interval)
**Requires `CONFIG_MQTT_PUBLISH_DIAGNOSTICS=y`**:
```json
{
  "raw": {
    "pm1_ugm3": 5.2,
    "pm25_ugm3": 9.8,
    "pm10_ugm3": 13.5,
    "co2_ppm": 695,
    "temp_c": 22.8
  },
  "fusion": {
    "pm_rh_factor": 1.08,
    "co2_pressure_offset_ppm": -8.2,
    "temp_self_heat_offset_c": 0.5,
    "pm25_quality": 95,
    "pm1_pm25_ratio": 0.78
  },
  "abc": {
    "baseline_ppm": 412,
    "confidence_pct": 87,
    "nights_tracked": 9
  }
}
```

---

## Implementation Phases

### ✅ Phase 0: Planning (DONE)
- [x] Analyze existing architecture
- [x] Design data flow
- [x] Create PLAN.md

---

### Phase 1: Data Model & Configuration

**Goal**: Extend data structures and add Kconfig options

#### 1.1 Extend `iaq_data.h`
- [x] Add `fused` struct with compensated sensor values
- [x] Add `metrics` struct with AQI, comfort, trends (including Tier 1 additions)
- [x] Add `fusion_diagnostics` struct for observability
- [x] Add `pressure_trend_t` enum

**New structs**:
```c
typedef enum {
    PRESSURE_TREND_RISING,
    PRESSURE_TREND_STABLE,
    PRESSURE_TREND_FALLING,
    PRESSURE_TREND_UNKNOWN
} pressure_trend_t;

typedef struct {
    // Compensated sensor values
    float temp_c;
    float rh_pct;
    float pressure_pa;           // raw (used as reference)
    float pm1_ugm3;              // raw (for diagnostics)
    float pm25_ugm3;             // compensated (RH-corrected)
    float pm10_ugm3;             // compensated (RH-corrected)
    float co2_ppm;               // compensated (pressure + ABC)
} iaq_fused_data_t;

typedef struct {
    // AQI
    uint16_t aqi_value;          // 0-500
    const char* aqi_category;    // "Good", "Moderate", "Unhealthy", etc.
    const char* aqi_dominant;    // "pm25" or "pm10"
    float aqi_pm25_subindex;
    float aqi_pm10_subindex;

    // Comfort (Tier 1: added abs_humidity)
    float dew_point_c;
    float abs_humidity_gm3;      // Absolute humidity (g/m³)
    float heat_index_c;
    uint8_t comfort_score;       // 0-100
    const char* comfort_category;

    // Scores (Tier 1: added VOC/NOx categories)
    uint8_t co2_score;           // 0-100 ventilation score
    const char* voc_category;    // "Excellent", "Good", "Moderate", "Poor", "Very Poor", "Severe"
    const char* nox_category;    // Same categories as VOC
    uint8_t overall_iaq_score;   // 0-100 composite

    // Mold risk (Tier 1)
    uint8_t mold_risk_score;     // 0-100 (0=no risk, 100=high risk)
    const char* mold_risk_category; // "Low", "Moderate", "High", "Severe"

    // Trends & rates (Tier 1: added CO2 rate, PM spike)
    pressure_trend_t pressure_trend;
    float pressure_delta_3hr_hpa;
    float co2_rate_ppm_hr;       // Rate of change (ppm/hour)
    bool pm25_spike_detected;    // Sudden increase detected
} iaq_metrics_t;

typedef struct {
    float pm_rh_factor;          // applied PM correction (1.0 = none)
    float co2_pressure_offset_ppm;
    float temp_self_heat_offset_c;
    uint16_t co2_abc_baseline_ppm;
    uint8_t co2_abc_confidence_pct;
    uint8_t pm25_quality;        // 0-100
    float pm1_pm25_ratio;        // for health check
} iaq_fusion_diagnostics_t;

// Add to iaq_data_t:
iaq_fused_data_t fused;
iaq_metrics_t metrics;
iaq_fusion_diagnostics_t fusion_diag;
```

#### 1.2 Initialize New Fields in `iaq_data.c`
- [x] Set default/NAN values for all new fields
- [x] Initialize strings to "unknown"

#### 1.3 Add Kconfig Options
**File**: `components/sensor_coordinator/Kconfig.projbuild`

- [x] Add "Sensor Fusion" menu with:
  - `CONFIG_FUSION_PM_RH_ENABLE` (bool, default y)
  - `CONFIG_FUSION_PM_RH_A` (float, default 0.3)
  - `CONFIG_FUSION_PM_RH_B` (float, default 3.0)
  - `CONFIG_FUSION_CO2_PRESSURE_ENABLE` (bool, default y)
  - `CONFIG_FUSION_CO2_PRESSURE_REF_PA` (int, default 101325)
  - `CONFIG_FUSION_TEMP_SELF_HEAT_OFFSET_C` (float, default 0.0)
  - `CONFIG_FUSION_CO2_ABC_ENABLE` (bool, default y)

- [x] Add "Derived Metrics" menu with:
  - `CONFIG_METRICS_AQI_ENABLE` (bool, default y)
  - `CONFIG_METRICS_COMFORT_ENABLE` (bool, default y)
  - `CONFIG_METRICS_PRESSURE_TREND_ENABLE` (bool, default y)
  - `CONFIG_METRICS_PRESSURE_TREND_WINDOW_HR` (int, default 3, range 1-6)
  - `CONFIG_METRICS_PRESSURE_TREND_THRESHOLD_HPA` (float, default 0.5)

**File**: `main/Kconfig.projbuild`
- [x] Add `CONFIG_MQTT_PUBLISH_DIAGNOSTICS` (bool, default n)
- [x] Add `CONFIG_MQTT_PUBLISH_PM1` (bool, default n)

#### 1.4 Test Phase 1
- [x] Build with all new configs enabled
- [x] Build with all new configs disabled
- [x] Verify `iaq_data` fields initialize correctly

---

### Phase 2: Sensor Fusion Logic

**Goal**: Implement cross-sensor compensation algorithms

#### 2.1 Create Fusion Module
**New files**: `components/sensor_coordinator/sensor_fusion.c/.h`

- [x] Create header with public API:
  ```c
  void fusion_init(void);
  void fusion_apply(iaq_data_t* data);
  ```

- [x] Implement PM RH correction:
  - Guard: RH valid, < 90%, recent (< 60s)
  - Formula: `pm_dry = pm_raw / (1 + a*(RH/100)^b)`
  - Calculate quality score based on RH range
  - Update `pm1_pm25_ratio` for health check

- [x] Implement CO2 pressure compensation:
  - Guard: Pressure valid, within reasonable range (95000-106000 Pa)
  - Formula: `co2_corrected = co2_raw * (P_ref / P_measured)`

- [x] Implement temperature self-heating correction:
  - Load offset from NVS or use Kconfig default
  - `temp_ambient = temp_sht45 - offset_c`

- [x] Implement CO2 ABC baseline tracking:
  - Ring buffer for nightly minima (7 days)
  - Detect "night" by low CO2 slope + time window (2-6 AM)
  - Require 7 consecutive nights before applying baseline shift
  - Store baseline in NVS (`fusion_cfg/abc_baseline_ppm`)
  - Calculate confidence based on # nights tracked

- [x] Add NVS helpers:
  - Load coefficients on init
  - Save ABC baseline to NVS
  - Console command to persist custom coefficients

#### 2.2 Pressure Trend Calculation
**Moved to `metrics_calc.c` for better separation of concerns**

- [x] Define ring buffer (in metrics_calc.c)
- [x] Implement `update_pressure_trend()` with history tracking
- [x] Return RISING/STABLE/FALLING based on threshold
- [x] Return UNKNOWN if insufficient data (< 1 hour)

#### 2.3 Integrate Fusion into Coordinator
**Modify `sensor_coordinator.c`**:

- [x] Call `fusion_init()` in `sensor_coordinator_init()`
- [x] Call `metrics_init()` in `sensor_coordinator_init()`
- [x] In coordinator task loop, after all sensors read:
  ```c
  // After reading all sensors
  fusion_apply(data);  // Modifies data->fused in-place
  metrics_calculate_all(data);  // Calculate all metrics including trends
  ```

#### 2.4 Update CMakeLists.txt
- [x] Add `sensor_fusion.c` to SRCS in `components/sensor_coordinator/CMakeLists.txt`

#### 2.5 Test Phase 2
- [x] Build succeeds without errors
- [ ] Simulation mode: Verify compensations applied (runtime test pending)
- [ ] Console log fusion diagnostics (runtime test pending)
- [ ] Verify NVS persistence (runtime test pending)

---

### Phase 3: Derived Metrics Calculators

**Goal**: Implement AQI, comfort, and score calculations

#### 3.1 Create Metrics Module
**New files**: `components/sensor_coordinator/metrics_calc.c/.h`

- [x] Create header with public API
- [x] Create unified `metrics_calculate_all(iaq_data_t*)` entry point

#### 3.2 Implement EPA AQI Calculator
- [x] Define EPA breakpoint tables for PM2.5 and PM10
- [x] Implement piecewise linear interpolation
- [x] Handle edge cases (NaN, < 0, > max)
- [x] Calculate both PM2.5 and PM10 sub-indices
- [x] Return max(pm25_aqi, pm10_aqi) as overall AQI
- [x] Map AQI to category strings

#### 3.3 Implement Comfort Calculators
- [x] **Dew point** (Magnus formula)
- [x] **Absolute humidity** (Tier 1 enhancement)
- [x] **Heat index** (NOAA formula with >27°C threshold)
- [x] **Comfort score** with configurable targets and penalties
- [x] Map comfort score to categories

#### 3.4 Implement Score Calculators
- [x] **CO2 score** (piecewise linear mapping)
- [x] **Overall IAQ score** (weighted average: 40% AQI, 40% CO2, 20% comfort)

#### 3.5 Implement Tier 1 Metrics
- [x] **VOC/NOx categories** (index-to-category mapping)
- [x] **Mold risk index** (RH + dew point proximity to cold surfaces)
- [x] **Pressure trend** (ring buffer, RISING/STABLE/FALLING)
- [x] **CO2 rate of change** (ppm/hr over configurable window)
- [x] **PM2.5 spike detection** (threshold above rolling baseline)

#### 3.6 Integrate into Coordinator
**Modify `sensor_coordinator.c`**:

- [x] Call `metrics_calculate_all(data)` after `fusion_apply(data)`
- [x] Single unified entry point for all metrics

#### 3.7 Update CMakeLists.txt
- [x] Add `metrics_calc.c` to SRCS

#### 3.8 Test Phase 3
- [x] Build succeeds without errors
- [ ] Console log metrics output (runtime test pending)
- [ ] Verify category mappings (runtime test pending)

---

### ✅ Phase 4: MQTT Publishing & Architecture Decoupling (DONE)

**Goal**: Publish compensated values, metrics, and optional diagnostics with fully decoupled timer-based architecture

**Key Architectural Decision**: Implemented **fully decoupled pipeline** where sensor readings, fusion, metrics, and MQTT publishing run independently on separate timers rather than event-driven coupling.

**Architecture**:
- **Sensors**: Staggered reads on individual cadences (10-30s, configurable per sensor)
- **Fusion**: 1 Hz independent timer applying all compensations
- **Metrics**: 0.2 Hz (5s) independent timer calculating all derived metrics
- **MQTT Publishing**: Independent timers for state (30s), metrics (30s), health (30s), diagnostics (5min)

**Removed event-driven coupling**:
- Deleted `mqtt_event_task` that waited on sensor update event bits
- Removed all `SENSOR_UPDATED_*_BIT` event flags from coordinator
- Removed old per-sensor MQTT topics and publish functions
- Timers now drive all periodic operations independently

#### 4.1 Restructure MQTT State Publishing
**Modified `mqtt_manager.c`**:

- [x] Updated state topic (`/state`) to use compensated values:
  ```c
  IAQ_DATA_WITH_LOCK() {
      cJSON_AddNumberToObject(root, "temp_c", data->fused.temp_c);
      cJSON_AddNumberToObject(root, "rh_pct", data->fused.rh_pct);
      cJSON_AddNumberToObject(root, "pressure_pa", data->fused.pressure_pa);
      cJSON_AddNumberToObject(root, "pm25_ugm3", data->fused.pm25_ugm3);
      cJSON_AddNumberToObject(root, "pm10_ugm3", data->fused.pm10_ugm3);
      cJSON_AddNumberToObject(root, "co2_ppm", data->fused.co2_ppm);

      #ifdef CONFIG_METRICS_AQI_ENABLE
      cJSON_AddNumberToObject(root, "aqi", data->metrics.aqi_value);
      #endif

      #ifdef CONFIG_METRICS_COMFORT_ENABLE
      cJSON_AddNumberToObject(root, "comfort_score", data->metrics.comfort_score);
      #endif
  }
  ```

- [x] Optionally publishes PM1.0 if `CONFIG_MQTT_PUBLISH_PM1` enabled
- [x] Publishes basic metrics (AQI value, comfort score) in `/state` for quick overview

#### 4.2 Add Metrics Topic
- [x] Created `mqtt_publish_metrics()` function (timer-driven, 30s interval):
  ```c
  cJSON *metrics = cJSON_CreateObject();

  cJSON *aqi = cJSON_CreateObject();
  cJSON_AddNumberToObject(aqi, "value", data->metrics.aqi_value);
  cJSON_AddStringToObject(aqi, "category", data->metrics.aqi_category);
  cJSON_AddStringToObject(aqi, "dominant", data->metrics.aqi_dominant);
  cJSON_AddNumberToObject(aqi, "pm25_subindex", data->metrics.aqi_pm25_subindex);
  cJSON_AddNumberToObject(aqi, "pm10_subindex", data->metrics.aqi_pm10_subindex);
  cJSON_AddItemToObject(metrics, "aqi", aqi);

  cJSON *comfort = cJSON_CreateObject();
  cJSON_AddNumberToObject(comfort, "score", data->metrics.comfort_score);
  cJSON_AddStringToObject(comfort, "category", data->metrics.comfort_category);
  cJSON_AddNumberToObject(comfort, "dew_point_c", data->metrics.dew_point_c);
  cJSON_AddNumberToObject(comfort, "heat_index_c", data->metrics.heat_index_c);
  cJSON_AddItemToObject(metrics, "comfort", comfort);

  // ... pressure trend, scores

  esp_mqtt_client_enqueue(s_mqtt_client, TOPIC_METRICS, json_str, ...);
  ```

- [x] Publishes on independent timer (30s, configurable via `CONFIG_MQTT_METRICS_PUBLISH_INTERVAL_SEC`)
- [x] Includes nested JSON structure: `aqi{...}`, `comfort{...}`, `pressure{...}`, `mold_risk{...}`
- [x] Includes root-level scores and trends: `co2_score`, `voc_category`, `nox_category`, `overall_iaq_score`, `co2_rate_ppm_hr`, `pm25_spike_detected`

#### 4.3 Add Diagnostics Topic (Optional)
- [x] Guarded with `#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS`
- [x] Created `mqtt_publish_diagnostics()` function
- [x] Publishes at lower rate (5 min timer, configurable via `CONFIG_MQTT_DIAGNOSTICS_PUBLISH_INTERVAL_SEC`)
- [x] Includes raw values, fusion corrections, ABC state, PM1/PM2.5 ratio

#### 4.4 Update HA Discovery
- [x] Updated HA discovery to use unified topics (`/state` and `/metrics`)
- [x] Added entities for all new metrics with proper JSON path templates:
  - `sensor.{device}_aqi` (device_class: aqi)
  - `sensor.{device}_aqi_category` (text)
  - `sensor.{device}_aqi_dominant` (text - pollutant driving AQI)
  - `sensor.{device}_comfort_score` (0-100)
  - `sensor.{device}_comfort_category` (text)
  - `sensor.{device}_dew_point` (device_class: temperature)
  - `sensor.{device}_abs_humidity` (device_class: humidity, g/m³)
  - `sensor.{device}_heat_index` (device_class: temperature)
  - `sensor.{device}_pressure_trend` (text with icon)
  - `sensor.{device}_pressure_delta_3hr` (device_class: pressure, hPa)
  - `sensor.{device}_co2_score` (0-100)
  - `sensor.{device}_voc_category` (text)
  - `sensor.{device}_nox_category` (text)
  - `sensor.{device}_overall_iaq_score` (0-100)
  - `sensor.{device}_mold_risk` (0-100)
  - `sensor.{device}_mold_category` (text)
  - `sensor.{device}_co2_rate` (ppm/hr)
  - `sensor.{device}_pm25_spike` (boolean)

- [x] PM1.0 only auto-discovered if `CONFIG_MQTT_PUBLISH_PM1` enabled
- [x] Diagnostics topic not included in discovery (raw data for debugging only)

#### 4.5 Updated Console Commands
- [x] Updated `mqtt publish` command to test unified topics (`/state`, `/metrics`, `/health`, `/diagnostics`)

#### 4.6 Updated Public API
- [x] Removed old per-sensor publish functions from `mqtt_manager.h`
- [x] Added public API for unified publishers: `mqtt_publish_state()`, `mqtt_publish_metrics()`, `mqtt_publish_diagnostics()`
- [x] Updated header documentation with topic descriptions

#### 4.7 Test Phase 4
- [x] Build succeeds without errors
- [ ] Runtime test: Verify `/state` topic uses compensated values
- [ ] Runtime test: Verify `/metrics` topic publishes correctly
- [ ] Runtime test: Verify `/diagnostics` topic (if enabled)
- [ ] Runtime test: Test HA discovery (entities appear in HA)

---

### Phase 5: Console Commands & Testing

**Goal**: Add observability commands and comprehensive tests

#### 5.1 Add Console Commands
**Modify `console_commands.c`**:

- [ ] Add `fusion` command group:
  ```
  fusion status                    # Show applied compensations, ABC state
  fusion set pm_offset <a> <b>     # Set PM RH correction coefficients
  fusion set temp_offset <c>       # Set temperature self-heating offset
  fusion set co2_pressure <enable> # Enable/disable CO2 pressure comp
  fusion reset_abc                 # Reset CO2 ABC baseline
  ```

- [ ] Add `metrics` command:
  ```
  metrics status                   # Show current AQI, comfort, trends
  ```

#### 5.2 Unit Tests
**Create `components/sensor_coordinator/test/`**:

- [ ] **test_aqi.c**:
  - EPA golden vectors (official test cases)
  - Breakpoint edge cases (12.0, 12.1, 35.4, 35.5, etc.)
  - NaN handling
  - Out-of-range handling (negative, > 500)

- [ ] **test_comfort.c**:
  - Dew point vs. reference tables
  - Heat index vs. NOAA reference
  - Comfort score edge cases (extreme temps/RH)

- [ ] **test_fusion.c** (optional):
  - PM RH correction at boundary RH (90%, 95%)
  - CO2 pressure compensation with different pressures
  - ABC baseline tracking state machine

#### 5.3 Integration Testing
- [ ] Enable `CONFIG_IAQ_SIMULATION=y`
- [ ] Run for 30 minutes, verify:
  - All compensations applied
  - AQI calculated correctly
  - Comfort metrics reasonable
  - Pressure trend updates
  - MQTT topics published
  - HA entities populated

#### 5.4 Performance Profiling
- [ ] Add timing instrumentation:
  ```c
  int64_t start = esp_timer_get_time();
  fusion_apply(data);
  int64_t fusion_us = esp_timer_get_time() - start;

  start = esp_timer_get_time();
  calculate_all_metrics(data);
  int64_t metrics_us = esp_timer_get_time() - start;

  ESP_LOGI(TAG, "Fusion: %lld us, Metrics: %lld us", fusion_us, metrics_us);
  ```

- [ ] Target: < 5% of sensor read cadence overhead

#### 5.5 Documentation
- [ ] Update [CLAUDE.md](CLAUDE.md) with:
  - New data model
  - Fusion algorithms
  - Metrics formulas
  - Console commands
  - MQTT topics

- [ ] Create METRICS.md with:
  - AQI calculation details
  - Comfort formulas
  - Fusion compensation math
  - Configuration guide

---

## File Manifest

### New Files (8 total)
```
components/sensor_coordinator/
  ├── sensor_fusion.c              # Fusion compensations
  ├── sensor_fusion.h
  ├── metrics_calc.c               # AQI, comfort calculators
  ├── metrics_calc.h
  └── test/
      ├── test_aqi.c               # Unit tests
      ├── test_comfort.c
      └── test_fusion.c

PLAN.md                            # This file
```

### Modified Files (8 total)
```
components/iaq_data/
  ├── include/iaq_data.h           # Add fused, metrics, fusion_diag structs
  └── iaq_data.c                   # Initialize new fields

components/sensor_coordinator/
  ├── sensor_coordinator.c         # Integrate fusion + metrics
  ├── Kconfig.projbuild            # Add fusion/metrics configs
  └── CMakeLists.txt               # Add new .c files

components/connectivity/
  └── mqtt_manager.c               # Add /metrics and /diagnostics topics

components/console_commands/
  └── console_commands.c           # Add fusion/metrics commands

main/
  └── Kconfig.projbuild            # Add MQTT diagnostics flag
```

---

## Configuration Summary

### Defaults (Most Users)
```
CONFIG_FUSION_PM_RH_ENABLE=y
CONFIG_FUSION_CO2_PRESSURE_ENABLE=y
CONFIG_FUSION_CO2_ABC_ENABLE=y
CONFIG_METRICS_AQI_ENABLE=y
CONFIG_METRICS_COMFORT_ENABLE=y
CONFIG_METRICS_PRESSURE_TREND_ENABLE=y
CONFIG_MQTT_PUBLISH_DIAGNOSTICS=n
CONFIG_MQTT_PUBLISH_PM1=n
```

### Advanced Users (via `idf.py menuconfig`)
- Tune PM RH coefficients (`a`, `b`)
- Adjust temperature self-heating offset
- Enable diagnostics publishing
- Enable PM1.0 HA entity
- Adjust pressure trend window

### Runtime (via Console)
- `fusion set` commands for field calibration
- `fusion reset_abc` if sensor location changes

---

## Success Criteria

- [ ] All phases completed
- [ ] Unit tests pass
- [ ] Simulation mode works end-to-end
- [ ] MQTT topics publish correctly
- [ ] HA entities auto-discovered
- [ ] Console commands functional
- [ ] NVS persistence verified
- [ ] Performance overhead < 5%
- [ ] Documentation updated

---

## Notes

### PM RH Correction Coefficients
- Default: `a=0.3, b=3.0` (conservative, based on literature)
- Field calibration: Compare with reference instrument at different RH levels
- Store final values in NVS via console

### CO2 ABC Baseline
- Only applies in stable indoor environments
- If device is mobile (car, portable), disable ABC
- Confidence builds over 7+ nights
- Reset if location changes

### AQI NowCast (Future Enhancement)
- Current: Simple 1-hour average (last 12-20 readings)
- Future: 12-hour weighted average with variability-based weights
- Requires: 144-sample ring buffer (~600 bytes RAM)

---

## Estimated Timeline

| Phase | Tasks | Estimated Time |
|-------|-------|----------------|
| Phase 1 | Data model & config | 2 days |
| Phase 2 | Sensor fusion | 3 days |
| Phase 3 | Metrics calculators | 3 days |
| Phase 4 | MQTT publishing | 2 days |
| Phase 5 | Console & testing | 3 days |
| **Total** | | **13 days part-time** |

---

## References

### EPA AQI
- Breakpoints: https://www.airnow.gov/sites/default/files/2020-05/aqi-technical-assistance-document-sept2018.pdf
- Calculator: https://www.airnow.gov/aqi/aqi-calculator/

### NOAA Heat Index
- Formula: https://www.wpc.ncep.noaa.gov/html/heatindex_equation.shtml

### Magnus Formula (Dew Point)
- Reference: https://en.wikipedia.org/wiki/Dew_point#Calculating_the_dew_point

### PM RH Correction
- Literature: Jayaratne et al., "The influence of humidity on the performance of low-cost air particle mass sensors"

---

## Implementation Summary (2025-10-03)

### ✅ Completed Phases (1-4)

**Phase 1: Data Model & Configuration** - Complete
- Extended `iaq_data.h` with `fused`, `metrics`, and `fusion_diag` structures
- Added comprehensive Kconfig options for all fusion algorithms and metrics
- Initialized all new fields in `iaq_data.c` with proper no-data conventions (NaN, UINT16_MAX)

**Phase 2: Sensor Fusion Logic** - Complete
- Implemented `sensor_fusion.c` with all compensations:
  - PM RH correction with configurable coefficients (a=0.3, b=3.0)
  - CO2 pressure compensation (reference: 101325 Pa)
  - Temperature self-heating offset
  - CO2 ABC baseline tracking (7-day nightly minimum ring buffer)
- NVS persistence for fusion coefficients and ABC baseline
- Integrated fusion into coordinator with **1 Hz independent timer** (decoupled from sensor reads)

**Phase 3: Derived Metrics Calculators** - Complete
- Implemented `metrics_calc.c` with all Tier 1 metrics:
  - EPA AQI with full PM2.5/PM10 breakpoint tables
  - Thermal comfort: dew point, absolute humidity, heat index, comfort score/category
  - Air quality scores: CO2 score, VOC/NOx categories, overall IAQ score
  - Mold risk assessment (RH + dew point proximity)
  - Pressure trend tracking (ring buffer, RISING/STABLE/FALLING)
  - CO2 rate of change (ppm/hr)
  - PM2.5 spike detection (threshold above rolling baseline)
- Integrated metrics into coordinator with **0.2 Hz (5s) independent timer**

**Phase 4: MQTT Publishing & Architecture Decoupling** - Complete
- **Major architectural improvement**: Implemented fully decoupled pipeline
  - Sensors: Staggered reads on individual cadences (10-30s)
  - Fusion: 1 Hz independent timer
  - Metrics: 0.2 Hz (5s) independent timer
  - MQTT: Independent timers for state/metrics/health (30s), diagnostics (5min)
- Created unified MQTT topics:
  - `/state` - Compensated (fused) sensor values + basic metrics (AQI, comfort score)
  - `/metrics` - Detailed derived metrics with nested JSON (aqi, comfort, pressure, mold_risk, scores, trends)
  - `/health` - System status, per-sensor state/errors, warmup countdown
  - `/diagnostics` - Raw values, fusion corrections, ABC state (optional, guarded by CONFIG)
- Removed event-driven coupling:
  - Deleted `mqtt_event_task` that waited on sensor update events
  - Removed all `SENSOR_UPDATED_*_BIT` event flags
  - Removed old per-sensor MQTT topics and functions
- Updated Home Assistant discovery for all 20+ new entities with proper JSON path templates
- Updated console `mqtt publish` command to test unified topics
- Exposed public API: `mqtt_publish_state()`, `mqtt_publish_metrics()`, `mqtt_publish_diagnostics()`

### Build Status
- ✅ **Build succeeds without errors** (as of 2025-10-03)
- All components compile cleanly
- No deprecated code remaining

### Remaining Work (Phase 5)
- Console commands for fusion control (`fusion status`, `fusion set`, `fusion reset_abc`)
- Console commands for metrics viewing (`metrics status`)
- Runtime testing with simulation mode
- Unit tests for AQI, comfort, fusion algorithms
- Performance profiling (target: < 5% overhead)
- Documentation updates (CLAUDE.md, METRICS.md)

### Key Architectural Decisions Made
1. **Timer-based decoupling** over event-driven architecture for better modularity and predictability
2. **Unified MQTT topics** (`/state`, `/metrics`) over per-sensor topics for cleaner HA integration
3. **Nested JSON structure** in `/metrics` for logical grouping of related values
4. **Public fusion/metrics APIs** retained for console commands and manual testing
5. **Pressure/CO2 trend calculations in metrics_calc.c** (not fusion) for separation of concerns
