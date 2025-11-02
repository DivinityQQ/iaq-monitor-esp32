import { Card, CardContent, Typography, Box, Chip, Grid, Skeleton } from '@mui/material';
import { useAtomValue } from 'jotai';
import HealthAndSafetyIcon from '@mui/icons-material/HealthAndSafety';
import { metricsAtom, iaqColorAtom } from '../../store/atoms';

/**
 * Featured IAQ (Indoor Air Quality) score card component
 *
 * Features:
 * - Large display of overall IAQ score (0-100)
 * - IAQ category badge (Excellent, Good, Fair, Poor, Very Poor)
 * - Sub-metrics showing IAQ components:
 *   - Air Quality (from AQI, 40% weight)
 *   - CO₂ Score (40% weight)
 *   - Comfort (20% weight)
 * - Color-coded background based on IAQ score
 * - Loading skeleton support
 */
export function IAQCard() {
  const metrics = useAtomValue(metricsAtom);
  const iaqColor = useAtomValue(iaqColorAtom);

  // Show loading skeleton if metrics not available or incomplete
  if (!metrics?.overall_iaq_score ||
      typeof metrics.overall_iaq_score !== 'number' ||
      !metrics.aqi || typeof metrics.aqi.value !== 'number' ||
      typeof metrics.co2_score !== 'number' ||
      !metrics.comfort || typeof metrics.comfort.score !== 'number') {
    return (
      <Card sx={{ height: '100%', minHeight: 280 }}>
        <CardContent>
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
            <Skeleton variant="circular" width={32} height={32} />
            <Skeleton variant="text" width="60%" height={32} />
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

  const iaqScore = metrics.overall_iaq_score;

  // Determine IAQ category based on score
  const getCategory = (score: number): string => {
    if (score >= 80) return 'Excellent';
    if (score >= 60) return 'Good';
    if (score >= 40) return 'Fair';
    if (score >= 20) return 'Poor';
    return 'Very Poor';
  };

  // Calculate normalized AQI score (inverted, 0-100 scale)
  // Backend formula: 100 - (aqi / 500 * 100), clamped to 0
  const airQualityScore = Math.max(0, 100 - (metrics.aqi.value / 5));

  return (
    <Card
      sx={{
        height: '100%',
        minHeight: 280,
        background: `linear-gradient(135deg, ${iaqColor}15 0%, ${iaqColor}05 100%)`,
        border: `2px solid ${iaqColor}40`,
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
          <HealthAndSafetyIcon sx={{ fontSize: 32, color: iaqColor }} />
          <Typography variant="h5" fontWeight={600}>
            Indoor Air Quality
          </Typography>
        </Box>

        {/* Main IAQ Score */}
        <Box sx={{ display: 'flex', alignItems: 'baseline', gap: 1, mb: 1 }}>
          <Typography
            variant="h2"
            component="div"
            sx={{
              fontWeight: 700,
              fontSize: { xs: '3rem', sm: '3.5rem', md: '4rem' },
              color: iaqColor,
              fontVariantNumeric: 'tabular-nums',
              minWidth: '3ch',
            }}
          >
            {iaqScore.toFixed(0)}
          </Typography>
          <Typography variant="h5" color="text.secondary">
            / 100
          </Typography>
        </Box>

        {/* Category Badge */}
        <Box sx={{ mb: 2 }}>
          <Chip
            label={getCategory(iaqScore)}
            sx={{
              bgcolor: iaqColor,
              color: 'white',
              fontWeight: 600,
              fontSize: '0.875rem',
              px: 1,
            }}
          />
        </Box>

        {/* IAQ Components Grid */}
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
                Air Quality
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {airQualityScore.toFixed(0)}
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
                CO₂ Score
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {metrics.co2_score.toFixed(0)}
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
                Comfort
              </Typography>
              <Typography variant="h6" fontWeight={600}>
                {metrics.comfort.score.toFixed(0)}
              </Typography>
            </Box>
          </Grid>
        </Grid>
      </CardContent>
    </Card>
  );
}
