import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Grid from '@mui/material/Grid';
import Skeleton from '@mui/material/Skeleton';

interface FeaturedCardSkeletonProps {
  /** Number of sub-metric boxes in the bottom grid (default: 3) */
  subMetricCount?: number;
}

/**
 * Reusable skeleton loading state for featured metric cards (AQI, IAQ, Comfort).
 * Provides consistent loading UI across all featured cards.
 */
export function FeaturedCardSkeleton({ subMetricCount = 3 }: FeaturedCardSkeletonProps) {
  return (
    <Card sx={{ height: '100%', minHeight: 280 }}>
      <CardContent>
        {/* Header: icon + title */}
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
          <Skeleton variant="circular" width={32} height={32} />
          <Skeleton variant="text" width="40%" height={32} />
        </Box>
        {/* Main value */}
        <Skeleton variant="text" width="60%" height={72} sx={{ mb: 1 }} />
        {/* Category badge */}
        <Skeleton variant="rectangular" width="50%" height={32} sx={{ mb: 2, borderRadius: 2 }} />
        {/* Sub-metrics grid */}
        <Grid container spacing={1}>
          {Array.from({ length: subMetricCount }).map((_, i) => (
            <Grid key={i} size={{ xs: Math.floor(12 / subMetricCount) as 4 | 6 | 12 }}>
              <Skeleton variant="rectangular" height={60} sx={{ borderRadius: 2 }} />
            </Grid>
          ))}
        </Grid>
      </CardContent>
    </Card>
  );
}
