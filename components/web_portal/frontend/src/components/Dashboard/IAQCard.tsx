import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import Collapse from '@mui/material/Collapse';
import Grid from '@mui/material/Grid';
import Skeleton from '@mui/material/Skeleton';
import Typography from '@mui/material/Typography';
import { useTheme } from '@mui/material/styles';
import { useAtomValue } from 'jotai';
import { useState } from 'react';
import HealthAndSafetyIcon from '@mui/icons-material/HealthAndSafety';
import { metricsAtom } from '../../store/atoms';
import { getIAQColorVar } from '../../theme';

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
  const theme = useTheme();
  const metrics = useAtomValue(metricsAtom);
  const [expanded, setExpanded] = useState(false);

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
  const iaqColor = getIAQColorVar(iaqScore, theme);

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
      onClick={() => setExpanded(!expanded)}
      sx={{
        height: '100%',
        minHeight: 280,
        background: `linear-gradient(135deg, color-mix(in srgb, ${iaqColor} 15%, transparent) 0%, color-mix(in srgb, ${iaqColor} 5%, transparent) 100%)`,
        border: `2px solid color-mix(in srgb, ${iaqColor} 25%, transparent)`,
        transition: 'transform 0.2s, box-shadow 0.2s',
        cursor: 'pointer',
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
              color: (theme) => (theme as any).vars?.palette?.common?.white || '#fff',
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

        {/* Expandable Explanation */}
        <Collapse in={expanded}>
          <Box sx={{ mt: 2, pt: 2, borderTop: 1, borderColor: 'divider' }}>
            <Typography variant="h6" gutterBottom fontWeight={600}>
              How IAQ Score is Calculated
            </Typography>

            <Typography variant="body2" paragraph>
              The Indoor Air Quality (IAQ) score is a weighted average of multiple air quality
              components, each contributing to the overall 0-100 score. Higher scores indicate
              better air quality.
            </Typography>

            <Typography variant="subtitle2" gutterBottom fontWeight={600} sx={{ mt: 2 }}>
              Weighted Formula
            </Typography>
            <Typography variant="body2" paragraph>
              IAQ = 35% × CO₂ Score + 35% × Air Quality + 20% × VOC Score + 10% × Comfort
            </Typography>

            <Typography variant="subtitle2" gutterBottom fontWeight={600}>
              Component Descriptions
            </Typography>
            <Grid container spacing={1} sx={{ mb: 2 }}>
              <Grid size={{ xs: 12, sm: 6 }}>
                <Box sx={{ p: 1.5, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="body2" fontWeight={600} gutterBottom>
                    CO₂ Score (35%)
                  </Typography>
                  <Typography variant="caption" color="text.secondary">
                    Linear mapping from CO₂ concentration (ppm). ≤400ppm = 100, ≥2000ppm = 0.
                    Indicates ventilation quality and occupancy levels.
                  </Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 12, sm: 6 }}>
                <Box sx={{ p: 1.5, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="body2" fontWeight={600} gutterBottom>
                    Air Quality (35%)
                  </Typography>
                  <Typography variant="caption" color="text.secondary">
                    Normalized from EPA AQI (0-500 scale inverted to 100-0). Based on PM2.5
                    and PM10 particulate matter concentrations.
                  </Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 12, sm: 6 }}>
                <Box sx={{ p: 1.5, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="body2" fontWeight={600} gutterBottom>
                    VOC Score (20%)
                  </Typography>
                  <Typography variant="caption" color="text.secondary">
                    Derived from VOC gas index (0-500 scale). Measures volatile organic
                    compounds from sources like cleaning products, cooking, and building materials.
                  </Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 12, sm: 6 }}>
                <Box sx={{ p: 1.5, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="body2" fontWeight={600} gutterBottom>
                    Comfort (10%)
                  </Typography>
                  <Typography variant="caption" color="text.secondary">
                    Temperature and humidity comfort score (0-100). Based on deviations from
                    optimal ranges (20-24°C, 40-60% RH).
                  </Typography>
                </Box>
              </Grid>
            </Grid>

            <Typography variant="subtitle2" gutterBottom fontWeight={600}>
              Score Categories
            </Typography>
            <Grid container spacing={1}>
              <Grid size={{ xs: 6, sm: 2.4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">80-100</Typography>
                  <Typography variant="body2" fontWeight={600}>Excellent</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 2.4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">60-79</Typography>
                  <Typography variant="body2" fontWeight={600}>Good</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 2.4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">40-59</Typography>
                  <Typography variant="body2" fontWeight={600}>Fair</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 2.4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">20-39</Typography>
                  <Typography variant="body2" fontWeight={600}>Poor</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 2.4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">0-19</Typography>
                  <Typography variant="body2" fontWeight={600}>Very Poor</Typography>
                </Box>
              </Grid>
            </Grid>
          </Box>
        </Collapse>
      </CardContent>
    </Card>
  );
}
