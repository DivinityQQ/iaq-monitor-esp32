import Grid from '@mui/material/Grid';
import { useAtomValue } from 'jotai';
import ThermostatIcon from '@mui/icons-material/Thermostat';
import WaterDropIcon from '@mui/icons-material/WaterDrop';
import Co2Icon from '@mui/icons-material/Co2';
import GrainIcon from '@mui/icons-material/Grain';
import CloudIcon from '@mui/icons-material/Cloud';
import ScienceIcon from '@mui/icons-material/Science';
import { stateAtom, metricsAtom } from '../../store/atoms';
import { SensorCard } from './SensorCard';
import { AQICard } from './AQICard';
import { ComfortCard } from './ComfortCard';
import { IAQCard } from './IAQCard';

/**
 * Metrics grid layout component
 *
 * Features:
 * - Responsive grid layout using MUI Grid2
 * - 8 sensor cards for individual readings (Temperature, Humidity, CO2, Pressure, PM2.5, PM10, PM1.0, VOC)
 * - 3 featured cards (AQI, Comfort, and IAQ)
 * - Auto-updates from Jotai atoms (stateAtom, metricsAtom)
 * - Proper responsive breakpoints for different screen sizes
 */
export function MetricsGrid() {
  const state = useAtomValue(stateAtom);
  const metrics = useAtomValue(metricsAtom);

  return (
    <Grid container spacing={3}>
      {/* Row 1: Temperature, Humidity, CO2, Pressure */}
      <Grid size={{ xs: 6, tablet: 4, md: 3 }}>
        <SensorCard
          value={state?.temp_c ?? null}
          lastValue={state?.last?.temp_c ?? null}
          unit="°C"
          label="Temperature"
          icon={<ThermostatIcon />}
          color="error.main"
          sensorId="sht45"
        />
      </Grid>

      <Grid size={{ xs: 6, tablet: 4, md: 3 }}>
        <SensorCard
          value={state?.rh_pct ?? null}
          lastValue={state?.last?.rh_pct ?? null}
          unit="%"
          label="Humidity"
          icon={<WaterDropIcon />}
          color="info.main"
           sensorId="sht45"
        />
      </Grid>

      <Grid size={{ xs: 6, tablet: 4, md: 3 }}>
        <SensorCard
          value={state?.co2_ppm ?? null}
          lastValue={state?.last?.co2_ppm ?? null}
          unit="ppm"
          label="CO₂"
          icon={<Co2Icon />}
          color="warning.main"
          precision={0}
          sensorId="s8"
          subtitle={typeof metrics?.co2_score === 'number' ? `Score: ${metrics.co2_score.toFixed(0)}` : undefined}
        />
      </Grid>

      <Grid size={{ xs: 6, tablet: 4, md: 3 }}>
        <SensorCard
          value={state?.pressure_hpa ?? null}
          lastValue={state?.last?.pressure_hpa ?? null}
          unit="hPa"
          label="Pressure"
          icon={<CloudIcon />}
          color="primary.main"
          sensorId="bmp280"
          subtitle={metrics?.pressure?.trend ?? undefined}
        />
      </Grid>

      {/* Row 2: PM2.5, PM10, PM1.0, VOC Index */}
      <Grid size={{ xs: 6, tablet: 4, md: 3 }}>
        <SensorCard
          value={state?.pm25_ugm3 ?? null}
          lastValue={state?.last?.pm25_ugm3 ?? null}
          unit="µg/m³"
          label="PM2.5"
          icon={<GrainIcon />}
          color="error.main"
          sensorId="pms5003"
          subtitle={metrics?.pm25_spike_detected ? 'Spike detected' : undefined}
        />
      </Grid>

      <Grid size={{ xs: 6, tablet: 4, md: 3 }}>
        <SensorCard
          value={state?.pm10_ugm3 ?? null}
          lastValue={state?.last?.pm10_ugm3 ?? null}
          unit="µg/m³"
          label="PM10"
          icon={<GrainIcon />}
          color="warning.main"
          sensorId="pms5003"
        />
      </Grid>

      <Grid size={{ xs: 6, tablet: 4, md: 3 }}>
        <SensorCard
          value={state?.pm1_ugm3 ?? null}
          lastValue={state?.last?.pm1_ugm3 ?? null}
          unit="µg/m³"
          label="PM1.0"
          icon={<GrainIcon />}
          color="info.main"
          sensorId="pms5003"
        />
      </Grid>

      <Grid size={{ xs: 6, tablet: 4, md: 3 }}>
        <SensorCard
          value={state?.voc_index ?? null}
          lastValue={state?.last?.voc_index ?? null}
          unit="index"
          label="VOC Index"
          icon={<ScienceIcon />}
          color="success.main"
          precision={0}
          sensorId="sgp41"
          subtitle={metrics?.voc_category ?? undefined}
        />
      </Grid>

      {/* Featured row as a full-width nested container item */}
      <Grid container size={{ xs: 12 }} spacing={3} alignItems="start">
        <Grid size={{ xs: 12, tablet: 6, md: 4 }}>
          <AQICard />
        </Grid>
        <Grid size={{ xs: 12, tablet: 6, md: 4 }}>
          <ComfortCard />
        </Grid>
        <Grid size={{ xs: 12, tablet: 6, md: 4 }}>
          <IAQCard />
        </Grid>
      </Grid>
    </Grid>
  );
}
