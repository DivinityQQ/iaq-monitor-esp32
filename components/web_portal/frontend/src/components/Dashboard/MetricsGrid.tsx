import Grid from '@mui/material/Grid';
import useMediaQuery from '@mui/material/useMediaQuery';
import { useTheme } from '@mui/material/styles';
import { useAtomValue } from 'jotai';
import ThermostatIcon from '@mui/icons-material/Thermostat';
import WaterDropIcon from '@mui/icons-material/WaterDrop';
import Co2Icon from '@mui/icons-material/Co2';
import GrainIcon from '@mui/icons-material/Grain';
import CloudIcon from '@mui/icons-material/Cloud';
import ScienceIcon from '@mui/icons-material/Science';
import GrassIcon from '@mui/icons-material/Grass';
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
 * - 10 sensor cards for individual readings (Temperature, Humidity, Mold Risk, CO2, Pressure, PM2.5, PM10, PM1.0, VOC, NOx)
 * - 3 featured cards (AQI, Comfort, and IAQ)
 * - Auto-updates from Jotai atoms (stateAtom, metricsAtom)
 * - Proper responsive breakpoints for different screen sizes
 */
export function MetricsGrid() {
  const theme = useTheme();
  const isCompact = useMediaQuery(theme.breakpoints.down('tablet'));
  const state = useAtomValue(stateAtom);
  const metrics = useAtomValue(metricsAtom);

  return (
    <Grid container spacing={3}>
      {/* Row 1: Temperature, Humidity, Mold Risk, CO2, Pressure */}
      <Grid size={{ xs: 6, tablet: 4, md: 3, lg: 2.4 }}>
        <SensorCard
          value={state?.temp_c ?? null}
          lastValue={state?.last?.temp_c ?? null}
          unit="°C"
          label="Temperature"
          icon={<ThermostatIcon />}
          color="error.main"
          precision={2}
          sensorId="sht45"
        />
      </Grid>

      <Grid size={{ xs: 6, tablet: 4, md: 3, lg: 2.4 }}>
        <SensorCard
          value={state?.rh_pct ?? null}
          lastValue={state?.last?.rh_pct ?? null}
          unit="%"
          label="Humidity"
          icon={<WaterDropIcon />}
          color="info.main"
          precision={2}
          sensorId="sht45"
          subtitle={
            metrics?.comfort?.abs_humidity_gm3 != null
              ? `${metrics.comfort.abs_humidity_gm3.toFixed(1)} g/m³`
              : undefined
          }
        />
      </Grid>

      <Grid size={{ xs: 6, tablet: 4, md: 3, lg: 2.4 }}>
        <SensorCard
          value={metrics?.mold_risk?.score ?? null}
          lastValue={metrics?.last?.mold_risk?.score ?? null}
          unit="%"
          label="Mold Risk"
          icon={<GrassIcon />}
          color="success.main"
          precision={0}
          sensorId="sht45"
          subtitle={metrics?.mold_risk?.category ?? metrics?.last?.mold_risk?.category ?? undefined}
        />
      </Grid>

      <Grid size={{ xs: 6, tablet: 4, md: 3, lg: 2.4 }}>
        <SensorCard
          value={state?.co2_ppm ?? null}
          lastValue={state?.last?.co2_ppm ?? null}
          unit="ppm"
          label="CO₂"
          icon={<Co2Icon />}
          color="warning.main"
          precision={0}
          sensorId="s8"
          subtitle={(() => {
            const score =
              typeof metrics?.co2_score === 'number' ? `Score: ${metrics.co2_score.toFixed(0)}` : null;
            const rate =
              typeof metrics?.co2_rate_ppm_hr === 'number'
                ? `${metrics.co2_rate_ppm_hr >= 0 ? '+' : ''}${metrics.co2_rate_ppm_hr.toFixed(0)} ppm/hr`
                : null;
            if (isCompact) return score ?? rate ?? undefined;
            return [score, rate].filter(Boolean).join(' · ') || undefined;
          })()}
        />
      </Grid>

      <Grid size={{ xs: 6, tablet: 4, md: 3, lg: 2.4 }}>
        <SensorCard
          value={state?.pressure_hpa ?? null}
          lastValue={state?.last?.pressure_hpa ?? null}
          unit="hPa"
          label="Pressure"
          icon={<CloudIcon />}
          color="info.main"
          precision={2}
          sensorId="bmp280"
          subtitle={(() => {
            const trend = metrics?.pressure?.trend ?? null;
            const delta =
              typeof metrics?.pressure?.delta_hpa === 'number'
                ? `${metrics.pressure.delta_hpa >= 0 ? '+' : ''}${metrics.pressure.delta_hpa.toFixed(2)} hPa`
                : null;
            if (isCompact) return trend ?? delta ?? undefined;
            return [trend, delta].filter(Boolean).join(' · ') || undefined;
          })()}
        />
      </Grid>

      {/* Row 2: PM2.5, PM10, PM1.0, VOC Index, NOx Index */}
      <Grid size={{ xs: 6, tablet: 4, md: 3, lg: 2.4 }}>
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

      <Grid size={{ xs: 6, tablet: 4, md: 3, lg: 2.4 }}>
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

      <Grid size={{ xs: 6, tablet: 4, md: 3, lg: 2.4 }}>
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

      <Grid size={{ xs: 6, tablet: 4, md: 3, lg: 2.4 }}>
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

      <Grid size={{ xs: 6, tablet: 4, md: 3, lg: 2.4 }}>
        <SensorCard
          value={state?.nox_index ?? null}
          lastValue={state?.last?.nox_index ?? null}
          unit="index"
          label="NOx Index"
          icon={<ScienceIcon />}
          color="error.main"
          precision={0}
          sensorId="sgp41"
          subtitle={metrics?.nox_category ?? undefined}
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
