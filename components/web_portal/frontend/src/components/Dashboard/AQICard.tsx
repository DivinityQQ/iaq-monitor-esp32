import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import Collapse from '@mui/material/Collapse';
import Grid from '@mui/material/Grid';
import Typography from '@mui/material/Typography';
import { useTheme } from '@mui/material/styles';
import { useAtomValue } from 'jotai';
import { useState } from 'react';
import AirIcon from '@mui/icons-material/Air';
import { metricsAtom } from '../../store/atoms';
import { getAQIColorVar } from '../../theme';
import { FeaturedCardSkeleton } from '../Common/FeaturedCardSkeleton';
import { featuredCardSx } from '../Common/cardStyles';
import { useSensorStaleStatus } from '../../hooks/useSensorStaleStatus';

/**
 * Featured AQI (Air Quality Index) card component
 *
 * Features:
 * - Large display of main AQI value
 * - AQI category badge (Good, Moderate, Unhealthy, etc.)
 * - Dominant pollutant indicator
 * - PM2.5 and PM10 sub-indices in grid layout
 * - Color-coded background based on AQI value
 * - Loading skeleton support
 */
export function AQICard() {
  const theme = useTheme();
  const metrics = useAtomValue(metricsAtom);
  const [expanded, setExpanded] = useState(false);

  // AQI depends primarily on PM sensors
  const { isStale, staleReason } = useSensorStaleStatus([
    { id: 'pms5003', label: 'PM sensors' },
  ]);

  // Show loading skeleton until we have a complete AQI snapshot
  if (!metrics?.aqi ||
      typeof metrics.aqi.value !== 'number' ||
      typeof metrics.aqi.pm25_subindex !== 'number' ||
      typeof metrics.aqi.pm10_subindex !== 'number') {
    return <FeaturedCardSkeleton />;
  }

  const { value, category, dominant, pm25_subindex, pm10_subindex } = metrics.aqi;
  const aqiColor = getAQIColorVar(value, theme);

  const formatDominant = (dominant: string): string => {
    if (dominant === 'pm25') return 'PM2.5';
    if (dominant === 'pm10') return 'PM10';
    return dominant;
  };

  return (
    <Card onClick={() => setExpanded(!expanded)} sx={featuredCardSx(aqiColor)}>
      <CardContent>
        {/* Header */}
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
          <AirIcon sx={{ fontSize: 32, color: aqiColor }} />
          <Box sx={{ display: 'flex', flexDirection: 'column' }}>
            <Typography variant="h5" fontWeight={600}>
              Air Quality Index
            </Typography>
            {isStale && (
              <Typography variant="caption" color="text.secondary">
                {staleReason}
              </Typography>
            )}
          </Box>
        </Box>

        {/* Main AQI Value */}
        <Typography
          variant="h2"
          component="div"
          sx={{
            fontWeight: 700,
            fontSize: { xs: '3rem', sm: '3.5rem', md: '4rem' },
            color: aqiColor,
            mb: 1,
            fontVariantNumeric: 'tabular-nums',
            minWidth: '3ch',
          }}
        >
          {value.toFixed(0)}
        </Typography>

        {/* Category Badge */}
        <Box sx={{ mb: 2 }}>
          <Chip
            label={category}
            sx={{
              bgcolor: aqiColor,
              color: (theme) => (theme as any).vars?.palette?.common?.white || '#fff',
              fontWeight: 600,
              fontSize: '0.875rem',
              px: 1,
            }}
          />
        </Box>

        {/* Sub-indices Grid */}
        <Grid container spacing={1}>
          <Grid size={{ xs: 4 }}>
            <Box
              sx={{
                p: 1.5,
                bgcolor: 'background.paper',
                borderRadius: 2,
                border: '1px solid',
                borderColor: 'divider',
              }}
            >
              <Typography variant="caption" color="text.secondary" display="block">
                Dominant
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {formatDominant(dominant)}
              </Typography>
            </Box>
          </Grid>
          <Grid size={{ xs: 4 }}>
            <Box
              sx={{
                p: 1.5,
                bgcolor: 'background.paper',
                borderRadius: 2,
                border: '1px solid',
                borderColor: 'divider',
              }}
            >
              <Typography variant="caption" color="text.secondary" display="block">
                PM2.5 Index
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {pm25_subindex.toFixed(0)}
              </Typography>
            </Box>
          </Grid>
          <Grid size={{ xs: 4 }}>
            <Box
              sx={{
                p: 1.5,
                bgcolor: 'background.paper',
                borderRadius: 2,
                border: '1px solid',
                borderColor: 'divider',
              }}
            >
              <Typography variant="caption" color="text.secondary" display="block">
                PM10 Index
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {pm10_subindex.toFixed(0)}
              </Typography>
            </Box>
          </Grid>
        </Grid>

        {/* Expandable Explanation */}
        <Collapse in={expanded}>
          <Box sx={{ mt: 2, pt: 2, borderTop: 1, borderColor: 'divider' }}>
            <Typography variant="h6" gutterBottom fontWeight={600}>
              How AQI is Calculated
            </Typography>

            <Typography variant="body2" paragraph>
              The EPA Air Quality Index uses piecewise linear interpolation to convert
              pollutant concentrations (PM2.5 and PM10 in µg/m³) to a 0-500 scale.
              The overall AQI is the maximum of the individual pollutant sub-indices.
            </Typography>

            <Typography variant="subtitle2" gutterBottom fontWeight={600} sx={{ mt: 2 }}>
              PM2.5 Breakpoints
            </Typography>
            <Grid container spacing={1} sx={{ mb: 2 }}>
              <Grid size={{ xs: 6, sm: 3 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">0-12 µg/m³</Typography>
                  <Typography variant="body2" fontWeight={600}>Good (0-50)</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 3 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">12-35 µg/m³</Typography>
                  <Typography variant="body2" fontWeight={600}>Moderate (51-100)</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 3 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">35-55 µg/m³</Typography>
                  <Typography variant="body2" fontWeight={600}>Unhealthy for Sensitive (101-150)</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 3 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">55-150 µg/m³</Typography>
                  <Typography variant="body2" fontWeight={600}>Unhealthy (151-200)</Typography>
                </Box>
              </Grid>
            </Grid>

            <Typography variant="subtitle2" gutterBottom fontWeight={600}>
              PM10 Breakpoints
            </Typography>
            <Grid container spacing={1} sx={{ mb: 2 }}>
              <Grid size={{ xs: 6, sm: 3 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">0-54 µg/m³</Typography>
                  <Typography variant="body2" fontWeight={600}>Good (0-50)</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 3 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">55-154 µg/m³</Typography>
                  <Typography variant="body2" fontWeight={600}>Moderate (51-100)</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 3 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">155-254 µg/m³</Typography>
                  <Typography variant="body2" fontWeight={600}>Unhealthy for Sensitive (101-150)</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 3 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">255-354 µg/m³</Typography>
                  <Typography variant="body2" fontWeight={600}>Unhealthy (151-200)</Typography>
                </Box>
              </Grid>
            </Grid>

            <Typography variant="body2" color="text.secondary">
              <strong>Note:</strong> The dominant pollutant is whichever has the higher sub-index.
              We use instantaneous concentrations rather than 24-hour averages for faster response,
              which may overestimate AQI during short pollution events.
            </Typography>
          </Box>
        </Collapse>
      </CardContent>
    </Card>
  );
}
