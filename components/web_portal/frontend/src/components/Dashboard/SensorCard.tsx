import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Skeleton from '@mui/material/Skeleton';
import Typography from '@mui/material/Typography';
import TrendingUpIcon from '@mui/icons-material/TrendingUp';
import TrendingDownIcon from '@mui/icons-material/TrendingDown';
import type { ReactNode } from 'react';

interface SensorCardProps {
  /** Main sensor value to display */
  value: number | null;
  /** Unit of measurement (e.g., "°C", "%", "ppm") */
  unit: string;
  /** Label/title for the sensor (e.g., "Temperature", "Humidity") */
  label: string;
  /** Icon component to display */
  icon: ReactNode;
  /** Color for the icon (defaults to primary.main) */
  color?: string;
  /** Optional trend indicator (positive = up, negative = down) */
  trend?: number | null;
  /** Optional subtitle/secondary text */
  subtitle?: string;
  /** Show loading skeleton */
  loading?: boolean;
  /** Number of decimals to display for the value (default: 1) */
  precision?: number;
}

/**
 * Reusable sensor card component for displaying sensor readings
 *
 * Features:
 * - Displays sensor value with unit and label
 * - Optional icon with customizable color
 * - Optional trend indicator (up/down arrow with percentage)
 * - Optional subtitle for additional context
 * - Loading skeleton support
 * - Responsive typography
 */
export function SensorCard({
  value,
  unit,
  label,
  icon,
  color = 'primary.main',
  trend,
  subtitle,
  loading = false,
  precision = 1,
}: SensorCardProps) {
  // Show loading skeleton if loading or value is null
  if (loading || value === null) {
    return (
      <Card sx={{ minHeight: 148 }}>
        <CardContent>
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
            <Skeleton variant="circular" width={24} height={24} />
            <Skeleton variant="text" width="60%" height={24} />
          </Box>
          <Skeleton variant="text" width="80%" height={48} sx={{ mb: 1 }} />
          <Skeleton variant="text" width="40%" height={20} />
        </CardContent>
      </Card>
    );
  }

  return (
    <Card
      sx={{
        height: '100%',
        minHeight: 148,
        transition: 'transform 0.2s, box-shadow 0.2s',
        '&:hover': {
          transform: 'translateY(-4px)',
          boxShadow: (theme) => theme.shadows[4],
        },
      }}
    >
      <CardContent>
        {/* Header: Icon + Label */}
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
          <Box sx={{ color, display: 'flex', alignItems: 'center' }}>{icon}</Box>
          <Typography variant="body2" color="text.secondary" fontWeight={500}>
            {label}
          </Typography>
        </Box>

        {/* Main Value */}
        <Box sx={{ display: 'flex', alignItems: 'baseline', gap: 1, whiteSpace: 'nowrap' }}>
          <Typography
            variant="h3"
            component="div"
            sx={{
              fontWeight: 600,
              fontSize: { xs: '2rem', sm: '2.25rem', tablet: '2.25rem', md: '2.5rem' },
              fontVariantNumeric: 'tabular-nums',
            }}
          >
            {value.toFixed(precision)}
          </Typography>
          <Typography
            variant="h5"
            color="text.secondary"
            sx={{ fontSize: { xs: '1rem', sm: '1.125rem', tablet: '1.125rem' } }}
          >
            {unit}
          </Typography>
        </Box>

        {/* Subtitle or Trend */}
        {(subtitle || trend !== undefined) && (
          <Box
            sx={{
              mt: 1,
              display: 'flex',
              alignItems: 'center',
              gap: 0.5,
              minHeight: 24,
            }}
          >
            {/* Trend indicator */}
            {trend !== null && trend !== undefined && (
              <Box
                sx={{
                  display: 'flex',
                  alignItems: 'center',
                  gap: 0.25,
                  color: trend > 0 ? 'error.main' : trend < 0 ? 'success.main' : 'text.secondary',
                }}
              >
                {trend > 0 ? (
                  <TrendingUpIcon fontSize="small" />
                ) : trend < 0 ? (
                  <TrendingDownIcon fontSize="small" />
                ) : null}
                <Typography variant="caption" fontWeight={500}>
                  {Math.abs(trend).toFixed(1)}%
                </Typography>
              </Box>
            )}

            {/* Subtitle */}
            {subtitle && (
              <Typography variant="caption" color="text.secondary">
                {subtitle}
              </Typography>
            )}
          </Box>
        )}
      </CardContent>
    </Card>
  );
}
