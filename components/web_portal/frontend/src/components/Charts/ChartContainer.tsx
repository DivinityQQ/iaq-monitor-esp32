import Box from '@mui/material/Box';
import Grid from '@mui/material/Grid';
import ToggleButton from '@mui/material/ToggleButton';
import ToggleButtonGroup from '@mui/material/ToggleButtonGroup';
import Typography from '@mui/material/Typography';
import { useMemo, useState } from 'react';
import { useTheme } from '@mui/material/styles';
import { ChartTile, RangeSeconds } from './ChartTile';
import { PMChartTile } from './PMChartTile';

/**
 * Container component for charts with controls and future enhancements
 * Placeholder for future features:
 * - Time range selector (1m, 5m, 1h, 24h)
 * - Chart type selector (line, area, bar)
 * - Export functionality
 */
export function ChartContainer() {
  const theme = useTheme();
  const [range, setRange] = useState<RangeSeconds>(60);

  const handleRange = (_: any, val: RangeSeconds | null) => {
    if (val) setRange(val);
  };

  const colors = useMemo(
    () => ({
      temp: theme.palette.error.main,
      rh: theme.palette.info.main,
      co2: theme.palette.warning.main,
      pm: theme.palette.primary.main,
    }),
    [theme.palette],
  );

  return (
    <Box>
      {/* Page Header with Time Range Controls */}
      <Box display="flex" justifyContent="space-between" alignItems="flex-start" mb={3}>
        <Box sx={{ mb: 4 }}>
          <Typography variant="h4" component="h1" fontWeight={600} gutterBottom>
            Charts
          </Typography>
          <Typography variant="body1" color="text.secondary">
            Historical sensor data visualization
          </Typography>
        </Box>
        <ToggleButtonGroup size="small" exclusive value={range} onChange={handleRange}>
          <ToggleButton value={60}>60s</ToggleButton>
          <ToggleButton value={300}>5m</ToggleButton>
          <ToggleButton value={3600}>1h</ToggleButton>
        </ToggleButtonGroup>
      </Box>

      <Grid container spacing={2}>
        <Grid size={{ xs: 12, tablet: 12, md: 6 }}>
          <ChartTile
            title="Temperature"
            color={colors.temp}
            unitSuffix="°C"
            metric="temp_c"
            range={range}
            yMin={15}
            softYMax={35}
            decimals={1}
          />
        </Grid>
        <Grid size={{ xs: 12, tablet: 12, md: 6 }}>
          <ChartTile
            title="Humidity"
            color={colors.rh}
            unitSuffix="%"
            metric="rh_pct"
            range={range}
            yMin={0}
            yMax={100}
            decimals={1}
          />
        </Grid>
        <Grid size={{ xs: 12, tablet: 12, md: 6 }}>
          <ChartTile
            title="CO2"
            color={colors.co2}
            unitSuffix="ppm"
            metric="co2_ppm"
            range={range}
            yMin={400}
            softYMax={2000}
            decimals={0}
          />
        </Grid>
        <Grid size={{ xs: 12, tablet: 12, md: 6 }}>
          <PMChartTile range={range} yMin={0} softYMax={50} />
        </Grid>
      </Grid>
    </Box>
  );
}
