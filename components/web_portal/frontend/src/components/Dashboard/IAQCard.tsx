import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import Collapse from '@mui/material/Collapse';
import Grid from '@mui/material/Grid';
import Typography from '@mui/material/Typography';
import { useTheme } from '@mui/material/styles';
import { useAtomValue } from 'jotai';
import { useState, useMemo } from 'react';
import HealthAndSafetyIcon from '@mui/icons-material/HealthAndSafety';
import { metricsAtom } from '../../store/atoms';
import type { Metrics, MetricsLast } from '../../api/types';
import { getIAQColorVar } from '../../theme';
import { FeaturedCardSkeleton } from '../Common/FeaturedCardSkeleton';
import { featuredCardSx } from '../Common/cardStyles';
import { useSensorStaleStatus } from '../../hooks/useSensorStaleStatus';

interface IAQData {
  overall_iaq_score: number;
  aqi_value: number;
  co2_score: number;
  comfort_score: number;
}

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
 * - Falls back to last known values when sensors are stale/disabled
 */
export function IAQCard() {
  const theme = useTheme();
  const metrics = useAtomValue(metricsAtom);
  const [expanded, setExpanded] = useState(false);

  // IAQ uses multiple sensors
  const { isStale, staleReason } = useSensorStaleStatus([
    { id: 'sht45', label: 'Temp/Humidity' },
    { id: 'bmp280', label: 'Pressure' },
    { id: 'pms5003', label: 'PM' },
    { id: 's8', label: 'CO₂' },
  ]);

  // Helper to check if IAQ data is complete from a metrics source
  const extractIAQData = (src: Metrics | MetricsLast | undefined): IAQData | undefined => {
    if (!src) return undefined;
    if (
      typeof src.overall_iaq_score === 'number' &&
      typeof src.aqi?.value === 'number' &&
      typeof src.co2_score === 'number' &&
      typeof src.comfort?.score === 'number'
    ) {
      return {
        overall_iaq_score: src.overall_iaq_score,
        aqi_value: src.aqi.value,
        co2_score: src.co2_score,
        comfort_score: src.comfort.score,
      };
    }
    return undefined;
  };

  // Determine which IAQ data to use: current or last known
  const { iaqData, usingLast } = useMemo(() => {
    const current = extractIAQData(metrics ?? undefined);
    if (current) return { iaqData: current, usingLast: false };
    const last = extractIAQData(metrics?.last);
    if (last) return { iaqData: last, usingLast: true };
    return { iaqData: undefined, usingLast: false };
  }, [metrics]);

  // Show loading skeleton if no data available (current or last)
  if (!iaqData) {
    return <FeaturedCardSkeleton />;
  }

  const { overall_iaq_score, aqi_value, co2_score, comfort_score } = iaqData;
  const iaqColor = getIAQColorVar(overall_iaq_score, theme);
  const showStale = isStale || usingLast;

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
  const airQualityScore = Math.max(0, 100 - (aqi_value / 5));

  return (
    <Card onClick={() => setExpanded(!expanded)} sx={featuredCardSx(iaqColor)}>
      <CardContent>
        {/* Header */}
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
          <HealthAndSafetyIcon sx={{ fontSize: 32, color: iaqColor }} />
          <Box sx={{ display: 'flex', flexDirection: 'column' }}>
            <Typography variant="h5" fontWeight={600}>
              Indoor Air Quality
            </Typography>
            {showStale && (
              <Typography variant="caption" color="text.secondary">
                {staleReason || 'Last known value'}
              </Typography>
            )}
          </Box>
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
            {overall_iaq_score.toFixed(0)}
          </Typography>
          <Typography variant="h5" color="text.secondary">
            / 100
          </Typography>
        </Box>

        {/* Category Badge */}
        <Box sx={{ mb: 2 }}>
          <Chip
            label={getCategory(overall_iaq_score)}
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
                {co2_score.toFixed(0)}
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
                {comfort_score.toFixed(0)}
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
