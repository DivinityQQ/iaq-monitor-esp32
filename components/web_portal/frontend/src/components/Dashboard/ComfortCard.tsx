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
import SentimentSatisfiedAltIcon from '@mui/icons-material/SentimentSatisfiedAlt';
import { metricsAtom } from '../../store/atoms';
import { getComfortColorVar } from '../../theme';
import { FeaturedCardSkeleton } from '../Common/FeaturedCardSkeleton';
import { featuredCardSx } from '../Common/cardStyles';
import { useSensorStaleStatus } from '../../hooks/useSensorStaleStatus';

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
  const theme = useTheme();
  const metrics = useAtomValue(metricsAtom);
  const [expanded, setExpanded] = useState(false);

  // Comfort depends on temperature & humidity (SHT45)
  const { isStale, staleReason } = useSensorStaleStatus([
    { id: 'sht45', label: 'Temp/Humidity' },
  ]);

  // Show loading skeleton until we have a complete comfort snapshot
  if (!metrics?.comfort ||
      typeof metrics.comfort.score !== 'number' ||
      typeof metrics.comfort.heat_index_c !== 'number' ||
      typeof metrics.comfort.abs_humidity_gm3 !== 'number' ||
      typeof metrics.comfort.dew_point_c !== 'number') {
    return <FeaturedCardSkeleton />;
  }

  const { score, category, heat_index_c, abs_humidity_gm3, dew_point_c } = metrics.comfort;
  const categoryLabel = category ?? 'Unknown';
  const comfortColor = getComfortColorVar(score, theme);

  return (
    <Card onClick={() => setExpanded(!expanded)} sx={featuredCardSx(comfortColor)}>
      <CardContent>
        {/* Header */}
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
          <SentimentSatisfiedAltIcon sx={{ fontSize: 32, color: comfortColor }} />
          <Box sx={{ display: 'flex', flexDirection: 'column' }}>
            <Typography variant="h5" fontWeight={600}>
              Comfort Score
            </Typography>
            {isStale && (
              <Typography variant="caption" color="text.secondary">
                {staleReason}
              </Typography>
            )}
          </Box>
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
              fontVariantNumeric: 'tabular-nums',
              minWidth: '3ch',
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
            label={categoryLabel}
            sx={{
              bgcolor: comfortColor,
              color: (theme) => (theme as any).vars?.palette?.common?.white || '#fff',
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
                {abs_humidity_gm3.toFixed(1)} g/m³
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

        {/* Expandable Explanation */}
        <Collapse in={expanded}>
          <Box sx={{ mt: 2, pt: 2, borderTop: 1, borderColor: 'divider' }}>
            <Typography variant="h6" gutterBottom fontWeight={600}>
              How Comfort Score is Calculated
            </Typography>

            <Typography variant="body2" paragraph>
              The comfort score (0-100) is calculated by applying penalties based on temperature
              and humidity deviations from optimal ranges. The score starts at 100 and penalties
              are subtracted.
            </Typography>

            <Typography variant="subtitle2" gutterBottom fontWeight={600} sx={{ mt: 2 }}>
              Temperature Ranges
            </Typography>
            <Grid container spacing={1} sx={{ mb: 2 }}>
              <Grid size={{ xs: 6, sm: 4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">20-24°C</Typography>
                  <Typography variant="body2" fontWeight={600}>Optimal (0% penalty)</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">18-20°C / 24-26°C</Typography>
                  <Typography variant="body2" fontWeight={600}>Cool/Warm (10-15%)</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">&lt;18°C / &gt;26°C</Typography>
                  <Typography variant="body2" fontWeight={600}>Cold/Hot (30%)</Typography>
                </Box>
              </Grid>
            </Grid>

            <Typography variant="subtitle2" gutterBottom fontWeight={600}>
              Humidity Ranges
            </Typography>
            <Grid container spacing={1} sx={{ mb: 2 }}>
              <Grid size={{ xs: 6, sm: 4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">40-60%</Typography>
                  <Typography variant="body2" fontWeight={600}>Optimal (0% penalty)</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">30-40% / 60-70%</Typography>
                  <Typography variant="body2" fontWeight={600}>Dry/Humid (10-15%)</Typography>
                </Box>
              </Grid>
              <Grid size={{ xs: 6, sm: 4 }}>
                <Box sx={{ p: 1, bgcolor: 'background.default', borderRadius: 1 }}>
                  <Typography variant="caption" color="text.secondary">&lt;30% / &gt;70%</Typography>
                  <Typography variant="body2" fontWeight={600}>Very Dry/Humid (25-30%)</Typography>
                </Box>
              </Grid>
            </Grid>

            <Typography variant="subtitle2" gutterBottom fontWeight={600} sx={{ mt: 2 }}>
              Additional Metrics
            </Typography>
            <Typography variant="body2" paragraph>
              <strong>Absolute Humidity:</strong> Calculated using temperature and relative humidity
              with the formula: AH = (6.112 × e^((17.67 × T)/(T + 243.5)) × RH × 2.1674) / (273.15 + T),
              measured in g/m³.
            </Typography>
            <Typography variant="body2" paragraph>
              <strong>Heat Index:</strong> For temperatures ≥27°C and humidity ≥40%, uses NOAA's
              simplified formula: HI = -8.78 + 1.61×T + 2.34×RH - 0.146×T×RH. Otherwise equals
              actual temperature.
            </Typography>
            <Typography variant="body2">
              <strong>Dew Point:</strong> Calculated using the Magnus formula. High dew points
              (≥18°C) correlate with increased mold risk and discomfort.
            </Typography>
          </Box>
        </Collapse>
      </CardContent>
    </Card>
  );
}
