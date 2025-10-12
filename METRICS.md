# IAQ Monitor Metrics Documentation

This document describes all derived metrics calculated from sensor fusion data, including sampling strategies, calculation methods, and interpretation guidelines.

---

## Table of Contents

1. [Overview](#overview)
2. [Sampling & Decimation](#sampling--decimation)
3. [Air Quality Index (AQI)](#air-quality-index-aqi)
4. [Thermal Comfort](#thermal-comfort)
5. [Air Quality Scores](#air-quality-scores)
6. [Pressure Trend](#pressure-trend)
7. [CO₂ Rate of Change](#co₂-rate-of-change)
8. [PM2.5 Spike Detection](#pm25-spike-detection)
9. [Mold Risk](#mold-risk)
10. [VOC/NOx Categories](#vocnox-categories)

---

## Overview

The IAQ monitor calculates metrics at two layers:

- **Sensor Fusion** (1 Hz): Applies cross-sensor compensations to raw readings (temperature self-heating, PM humidity correction, CO₂ pressure/ABC compensation). See `sensor_fusion.c`.
- **Metrics Calculation** (0.2 Hz / 5 s): Derives higher-level metrics from fused data using rolling windows and statistical methods. See `metrics_calc.c`.

All metrics are published to MQTT:
- `/state` topic: Fused sensor values + basic metrics (AQI value, comfort score)
- `/metrics` topic: Full breakdown of all derived metrics
- `/diagnostics` topic (optional): Raw values + fusion parameters for validation

---

## Sampling & Decimation

Metrics are calculated every **5 seconds** (0.2 Hz timer in `sensor_coordinator_task`), but history buffers are decimated to control memory usage:

| Metric | Buffer Size | Sampling Interval | Total History |
|--------|-------------|-------------------|---------------|
| **Pressure Trend** | 144 samples | 150 s (2.5 min) | 6 hours |
| **CO₂ Rate** | 64 samples | 60 s (1 min) | ~1 hour |
| **PM2.5 Spike** | 120 samples | 30 s | 60 minutes |

**Rationale:**
- Pressure changes slowly → 2.5 min decimation is sufficient for 3-hour trend detection.
- CO₂ changes faster (occupancy) → 1 min sampling captures meaningful trends.
- PM2.5 spikes are rapid (cooking, smoking) → 30 s sampling catches short bursts.

---

## Air Quality Index (AQI)

**Standard:** EPA AQI (United States Environmental Protection Agency)
**Pollutants:** PM2.5, PM10
**Location:** `calculate_aqi()` in `metrics_calc.c`

### Calculation

AQI is computed using piecewise linear interpolation from concentration breakpoints:

```
AQI = ((I_hi - I_lo) / (C_hi - C_lo)) * (C - C_lo) + I_lo
```

Where:
- `C` = measured pollutant concentration (µg/m³)
- `C_lo`, `C_hi` = concentration breakpoints surrounding `C`
- `I_lo`, `I_hi` = AQI index breakpoints (0-500 scale)

### PM2.5 Breakpoints (24-hr avg, but we use instantaneous)

| Concentration (µg/m³) | AQI Range | Category |
|-----------------------|-----------|----------|
| 0.0 – 12.0            | 0 – 50    | Good |
| 12.1 – 35.4           | 51 – 100  | Moderate |
| 35.5 – 55.4           | 101 – 150 | Unhealthy for Sensitive Groups |
| 55.5 – 150.4          | 151 – 200 | Unhealthy |
| 150.5 – 250.4         | 201 – 300 | Very Unhealthy |
| 250.5 – 500.0         | 301 – 500 | Hazardous |

### PM10 Breakpoints

| Concentration (µg/m³) | AQI Range | Category |
|-----------------------|-----------|----------|
| 0 – 54                | 0 – 50    | Good |
| 55 – 154              | 51 – 100  | Moderate |
| 155 – 254             | 101 – 150 | Unhealthy for Sensitive Groups |
| 255 – 354             | 151 – 200 | Unhealthy |
| 355 – 424             | 201 – 300 | Very Unhealthy |
| 425 – 604             | 301 – 500 | Hazardous |

### Output Fields

- `aqi_value`: Overall AQI (0-500, max of PM2.5 and PM10 sub-indices)
- `aqi_category`: Text category ("Good", "Moderate", etc.)
- `aqi_dominant`: Which pollutant drives the AQI ("pm25" or "pm10")
- `aqi_pm25_subindex`: PM2.5 contribution to AQI
- `aqi_pm10_subindex`: PM10 contribution to AQI

**Note:** We use **instantaneous concentrations** rather than 24-hour averages. This provides faster response but may overestimate AQI during short pollution events.

---

## Thermal Comfort

**Location:** `calculate_comfort()` in `metrics_calc.c`

### Dew Point (Magnus Formula)

```
α = (17.27 * T) / (237.7 + T) + ln(RH / 100)
Td = (237.7 * α) / (17.27 - α)
```

Where:
- `T` = temperature (°C)
- `RH` = relative humidity (%)
- `Td` = dew point (°C)

### Absolute Humidity

```
AH = (6.112 * exp((17.67 * T) / (T + 243.5)) * RH * 2.1674) / (273.15 + T)
```

Result in g/m³.

### Heat Index (NOAA Simplified)

For T ≥ 27°C and RH ≥ 40%:

```
HI = -8.78 + 1.61*T + 2.34*RH - 0.146*T*RH
```

Otherwise, HI = T (no correction needed).

### Comfort Score (0-100)

Weighted penalties applied to temperature and humidity ranges:

**Temperature Penalties:**
- Optimal: 20–24°C → 0% penalty
- Cool: 18–20°C → 10% penalty
- Warm: 24–26°C → 15% penalty
- Cold: <18°C → 30% penalty
- Hot: >26°C → 30% penalty

**Humidity Penalties:**
- Optimal: 40–60% → 0% penalty
- Dry: 30–40% → 10% penalty
- Humid: 60–70% → 15% penalty
- Very dry: <30% → 25% penalty
- Very humid: >70% → 30% penalty

```
Comfort Score = 100 - (temp_penalty + humidity_penalty)
```

### Comfort Category

| Score Range | Category |
|-------------|----------|
| 80–100      | Comfortable |
| 60–79       | Acceptable |
| 40–59       | Slightly Uncomfortable |
| 20–39       | Uncomfortable |
| 0–19        | Very Uncomfortable |

---

## Air Quality Scores

### CO₂ Score (0-100)

Linear mapping with thresholds:

| CO₂ (ppm) | Score | Interpretation |
|-----------|-------|----------------|
| ≤ 400     | 100   | Excellent (outdoor baseline) |
| 600       | 85    | Good |
| 800       | 70    | Acceptable |
| 1000      | 50    | Fair |
| 1500      | 25    | Poor |
| ≥ 2000    | 0     | Very Poor (ventilation needed) |

### Overall IAQ Score (0-100)

Weighted average:
```
IAQ = 0.35*CO₂_score + 0.35*AQI_normalized + 0.20*VOC_score + 0.10*comfort_score
```

Where:
- `AQI_normalized = 100 - (AQI / 5)` (scales 0-500 AQI to 100-0)
- `VOC_score` derived from VOC index (500 → 0, 0 → 100)

---

## Pressure Trend

**Window:** 3 hours (last 3 hours of samples at 2.5-min intervals)
**Location:** `update_pressure_trend()` in `metrics_calc.c`

### Algorithm

1. Find oldest sample within 3-hour window
2. Compute delta: `Δp = p_latest - p_oldest`
3. Classify trend:

| Δp (hPa/3hr) | Trend |
|--------------|-------|
| < -1.5       | Falling |
| -1.5 to +1.5 | Stable |
| > +1.5       | Rising |

**Use case:** Weather prediction (falling pressure → approaching storm, rising → clearing skies).

---

## CO₂ Rate of Change

**Window:** Configurable (default 15 minutes via `CONFIG_METRICS_CO2_RATE_WINDOW_MIN`)
**Sampling:** 1-minute intervals
**Location:** `update_co2_rate()` in `metrics_calc.c`

### Algorithm (Robust + Smoothed)

1. Sample fused CO₂ every 60 seconds into a ring buffer.
2. Select all samples within the configured window (chronological order).
3. Apply a 3-point median filter across the series to suppress jitter while preserving real changes.
4. Fit a simple linear regression of CO₂ vs time (in hours) across the filtered series to estimate the slope (ppm/hr).
5. Require a minimum time span of 5 minutes within the window; otherwise report `null` (stabilizing).
6. Clamp the slope to plausible bounds (±2500 ppm/hr) to prevent outliers.
7. Apply an exponential moving average (EMA, α = 0.25) to stabilize the reported rate.

**Interpretation:**
- Positive → CO₂ rising (occupancy increase / poor ventilation)
- Negative → CO₂ falling (effective ventilation / occupants leaving)
- Near zero → Steady state

This approach avoids extreme values during startup or brief spikes and stabilizes within a few minutes once enough samples are available.

---

## PM2.5 Spike Detection

**Window:** 60 minutes (120 samples at 30-second intervals)
**Location:** `update_pm_spike_detection()` in `metrics_calc.c`

### Algorithm

1. **Compute rolling baseline:** Median of PM2.5 over last 10 minutes
2. **Compute rolling standard deviation:** Over last 10 minutes
3. **Detect spike:**
   ```
   if (current_PM2.5 > baseline + 2.5 * std_dev) AND (current_PM2.5 > baseline + 15 µg/m³):
       spike_detected = true
   ```

**Rationale:**
- Combines **statistical threshold** (2.5σ above baseline) with **absolute threshold** (+15 µg/m³) to avoid false positives from low-variance clean air.
- Catches cooking events, smoking, outdoor pollution intrusion.

**Output:** Boolean `pm25_spike_detected`

---

## Mold Risk

**Location:** `calculate_mold_risk()` in `metrics_calc.c`

### Score Calculation (0-100)

Based on dew point ranges:

| Dew Point (°C) | Risk Level | Score |
|----------------|------------|-------|
| < 10           | Low        | 0–25  |
| 10–15          | Moderate   | 25–50 |
| 15–18          | High       | 50–75 |
| ≥ 18           | Severe     | 75–100 |

**Formula:**
```
if Td < 10:   score = (Td / 10) * 25
if 10 ≤ Td < 15: score = 25 + ((Td - 10) / 5) * 25
if 15 ≤ Td < 18: score = 50 + ((Td - 15) / 3) * 25
if Td ≥ 18:   score = 75 + min(((Td - 18) / 7) * 25, 25)
```

**Category Mapping:**

| Score | Category |
|-------|----------|
| 0–25  | Low      |
| 26–50 | Moderate |
| 51–75 | High     |
| 76–100| Severe   |

**Why dew point?** Mold growth correlates with surface condensation risk, which depends on dew point rather than RH alone.

---

## VOC/NOx Categories

**Input:** Sensirion SGP41 gas index (0-500 scale)
**Location:** `categorize_gas_index()` in `metrics_calc.c`

### Categories

| Index Range | Category |
|-------------|----------|
| 0–100       | Excellent |
| 101–150     | Good |
| 151–200     | Moderate |
| 201–250     | Poor |
| 251–350     | Very Poor |
| 351–500     | Severe |

**Note:** SGP41 outputs **relative** indices (calibrated to typical indoor air as 100). Values above 100 indicate worsening air quality, below 100 indicate cleaner than baseline.

---

## Summary

All metrics are designed to:
1. **Provide actionable insights** (e.g., "ventilate room" if CO₂ rate is rising)
2. **Balance responsiveness and stability** (median filtering, rolling windows)
3. **Match Home Assistant expectations** (standard units, text categories for automations)

For implementation details, see:
- `components/sensor_coordinator/metrics_calc.c` – Calculation logic
- `components/sensor_coordinator/sensor_fusion.c` – Input data preparation
- `components/connectivity/mqtt_manager.c` – MQTT publishing
