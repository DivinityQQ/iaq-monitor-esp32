import Box from '@mui/material/Box';
import Container from '@mui/material/Container';
import Grid from '@mui/material/Grid';
import ToggleButton from '@mui/material/ToggleButton';
import ToggleButtonGroup from '@mui/material/ToggleButtonGroup';
import Typography from '@mui/material/Typography';
import { useMemo, useState } from 'react';
import { ChartCard } from './ChartCard';
import { MultiSeriesChart } from './MultiSeriesChart';
import { RANGES, SINGLE_SERIES_CHARTS, type RangeKey } from './config/chartConfig';

/**
 * Container component for charts with controls and future enhancements
 * Placeholder for future features:
 * - Time range selector (1m, 5m, 1h, 24h)
 * - Chart type selector (line, area, bar)
 * - Export functionality
 */
export function ChartContainer() {
  const [range, setRange] = useState<RangeKey>('5m');

  const handleRange = (_: any, val: RangeKey | null) => {
    if (val) setRange(val);
  };

  const rangeOptions = useMemo(
    () => (['1m', '5m', '1h', '1d', '7d'] as RangeKey[]).map((key) => ({
      key,
      label: RANGES[key].label,
    })),
    []
  );

  return (
    <Container maxWidth="xl" sx={{ py: 3, px: { xs: 2, sm: 3 } }}>
      {/* Page Header with Time Range Controls */}
      <Box display="flex" justifyContent="space-between" alignItems="flex-start" sx={{ mb: 4 }}>
        <Box>
          <Typography variant="h4" component="h1" fontWeight={600} gutterBottom>
            Charts
          </Typography>
          <Typography variant="body1" color="text.secondary">
            Historical sensor data visualization
          </Typography>
        </Box>
        <ToggleButtonGroup size="small" exclusive value={range} onChange={handleRange}>
          {rangeOptions.map((option) => (
            <ToggleButton key={option.key} value={option.key}>
              {option.label}
            </ToggleButton>
          ))}
        </ToggleButtonGroup>
      </Box>

      <Grid container spacing={2}>
        {SINGLE_SERIES_CHARTS.map((metric) => (
          <Grid key={metric} size={{ xs: 12, tablet: 12, md: 6 }}>
            <ChartCard metric={metric} range={range} />
          </Grid>
        ))}
        <Grid size={{ xs: 12, tablet: 12, md: 6 }}>
          <MultiSeriesChart range={range} />
        </Grid>
      </Grid>
    </Container>
  );
}
