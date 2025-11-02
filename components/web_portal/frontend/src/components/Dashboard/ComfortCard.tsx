import { Card, CardContent, Typography, Box, Chip, Grid, Skeleton } from '@mui/material';
import { useAtomValue } from 'jotai';
import SentimentSatisfiedAltIcon from '@mui/icons-material/SentimentSatisfiedAlt';
import { metricsAtom, comfortColorAtom } from '../../store/atoms';

/**
 * Featured Comfort Score card component
 *
 * Features:
 * - Large display of comfort score (0-100)
 * - Comfort category badge (Comfortable, Acceptable, Uncomfortable, etc.)
 * - Heat index in Celsius
 * - Absolute humidity in g/m³
 * - Dew point in Celsius
 * - Color-coded background based on comfort score
 * - Loading skeleton support
 */
export function ComfortCard() {
  const metrics = useAtomValue(metricsAtom);
  const comfortColor = useAtomValue(comfortColorAtom);

  // Show loading skeleton if metrics not available or incomplete
  if (!metrics?.comfort ||
      typeof metrics.comfort.score !== 'number' ||
      typeof metrics.comfort.heat_index_c !== 'number' ||
      typeof metrics.comfort.abs_humidity_gm3 !== 'number' ||
      typeof metrics.comfort.dew_point_c !== 'number') {
    return (
      <Card sx={{ height: '100%' }}>
        <CardContent>
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
            <Skeleton variant="circular" width={32} height={32} />
            <Skeleton variant="text" width="50%" height={32} />
          </Box>
          <Skeleton variant="text" width="60%" height={72} sx={{ mb: 1 }} />
          <Skeleton variant="rectangular" width="50%" height={32} sx={{ mb: 2, borderRadius: 2 }} />
          <Grid container spacing={1}>
            <Grid size={{ xs: 4 }}>
              <Skeleton variant="rectangular" height={60} sx={{ borderRadius: 2 }} />
            </Grid>
            <Grid size={{ xs: 4 }}>
              <Skeleton variant="rectangular" height={60} sx={{ borderRadius: 2 }} />
            </Grid>
            <Grid size={{ xs: 4 }}>
              <Skeleton variant="rectangular" height={60} sx={{ borderRadius: 2 }} />
            </Grid>
          </Grid>
        </CardContent>
      </Card>
    );
  }

  const { score, category, heat_index_c, abs_humidity_gm3, dew_point_c } = metrics.comfort;

  return (
    <Card
      sx={{
        height: '100%',
        background: `linear-gradient(135deg, ${comfortColor}15 0%, ${comfortColor}05 100%)`,
        border: `2px solid ${comfortColor}40`,
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
          <SentimentSatisfiedAltIcon sx={{ fontSize: 32, color: comfortColor }} />
          <Typography variant="h5" fontWeight={600}>
            Comfort Score
          </Typography>
        </Box>

        {/* Main Comfort Score */}
        <Box sx={{ display: 'flex', alignItems: 'baseline', gap: 1, mb: 1 }}>
          <Typography
            variant="h2"
            component="div"
            sx={{
              fontWeight: 700,
              fontSize: { xs: '3rem', sm: '3.5rem', md: '4rem' },
              color: comfortColor,
            }}
          >
            {score.toFixed(0)}
          </Typography>
          <Typography variant="h5" color="text.secondary">
            / 100
          </Typography>
        </Box>

        {/* Category Badge */}
        <Box sx={{ mb: 2 }}>
          <Chip
            label={category}
            sx={{
              bgcolor: comfortColor,
              color: 'white',
              fontWeight: 600,
              fontSize: '0.875rem',
              px: 1,
            }}
          />
        </Box>

        {/* Comfort Metrics Grid */}
        <Grid container spacing={1}>
          <Grid size={{ xs: 4 }}>
            <Box
              sx={{
                p: 1.5,
                bgcolor: 'background.paper',
                borderRadius: 2,
                border: '1px solid',
                borderColor: 'divider',
                textAlign: 'center',
              }}
            >
              <Typography variant="caption" color="text.secondary" display="block">
                Heat Index
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {heat_index_c.toFixed(1)}°C
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
                textAlign: 'center',
              }}
            >
              <Typography variant="caption" color="text.secondary" display="block">
                Abs. Humidity
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {abs_humidity_gm3.toFixed(1)}
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
                textAlign: 'center',
              }}
            >
              <Typography variant="caption" color="text.secondary" display="block">
                Dew Point
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {dew_point_c.toFixed(1)}°C
              </Typography>
            </Box>
          </Grid>
        </Grid>
      </CardContent>
    </Card>
  );
}
