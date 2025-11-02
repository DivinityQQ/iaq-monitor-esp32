import { Card, CardContent, Typography, Box, Chip, Grid, Skeleton } from '@mui/material';
import { useAtomValue } from 'jotai';
import AirIcon from '@mui/icons-material/Air';
import { metricsAtom, aqiColorAtom } from '../../store/atoms';

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
  const metrics = useAtomValue(metricsAtom);
  const aqiColor = useAtomValue(aqiColorAtom);

  // Show loading skeleton if metrics not available or incomplete
  if (!metrics?.aqi ||
      typeof metrics.aqi.value !== 'number' ||
      typeof metrics.aqi.pm25_subindex !== 'number' ||
      typeof metrics.aqi.pm10_subindex !== 'number') {
    return (
      <Card sx={{ height: '100%', minHeight: 280 }}>
        <CardContent>
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
            <Skeleton variant="circular" width={32} height={32} />
            <Skeleton variant="text" width="40%" height={32} />
          </Box>
          <Skeleton variant="text" width="60%" height={72} sx={{ mb: 1 }} />
          <Skeleton variant="rectangular" width="50%" height={32} sx={{ mb: 2, borderRadius: 2 }} />
          <Grid container spacing={1}>
            <Grid size={{ xs: 6 }}>
              <Skeleton variant="rectangular" height={60} sx={{ borderRadius: 2 }} />
            </Grid>
            <Grid size={{ xs: 6 }}>
              <Skeleton variant="rectangular" height={60} sx={{ borderRadius: 2 }} />
            </Grid>
          </Grid>
        </CardContent>
      </Card>
    );
  }

  const { value, category, dominant, pm25_subindex, pm10_subindex } = metrics.aqi;

  return (
    <Card
      sx={{
        height: '100%',
        minHeight: 280,
        background: `linear-gradient(135deg, ${aqiColor}15 0%, ${aqiColor}05 100%)`,
        border: `2px solid ${aqiColor}40`,
        transition: 'transform 0.2s, box-shadow 0.2s',
        '&:hover': {
          transform: 'translateY(-4px)',
          boxShadow: (theme) => theme.shadows[8],
        },
      }}
    >
      <CardContent>
        {/* Header */}
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
          <AirIcon sx={{ fontSize: 32, color: aqiColor }} />
          <Typography variant="h5" fontWeight={600}>
            Air Quality Index
          </Typography>
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
              color: 'white',
              fontWeight: 600,
              fontSize: '0.875rem',
              px: 1,
            }}
          />
        </Box>

        {/* Dominant Pollutant */}
        <Typography variant="body2" color="text.secondary" sx={{ mb: 2 }}>
          Dominant pollutant: <strong>{dominant}</strong>
        </Typography>

        {/* Sub-indices Grid */}
        <Grid container spacing={1}>
          <Grid size={{ xs: 6 }}>
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
                PM2.5
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {pm25_subindex.toFixed(0)}
              </Typography>
            </Box>
          </Grid>
          <Grid size={{ xs: 6 }}>
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
                PM10
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {pm10_subindex.toFixed(0)}
              </Typography>
            </Box>
          </Grid>
        </Grid>
      </CardContent>
    </Card>
  );
}
