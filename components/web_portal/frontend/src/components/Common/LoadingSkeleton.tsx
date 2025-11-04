import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Container from '@mui/material/Container';
import Grid from '@mui/material/Grid';
import Skeleton from '@mui/material/Skeleton';
import Typography from '@mui/material/Typography';

/**
 * Card skeleton component
 * Mimics the structure of SensorCard for smooth loading→content transition
 */
export function CardSkeleton() {
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

/**
 * Featured card skeleton component
 * Mimics the structure of AQICard/ComfortCard
 */
export function FeaturedCardSkeleton() {
  return (
    <Card sx={{ minHeight: 280 }}>
      <CardContent>
        <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
          <Skeleton variant="circular" width={32} height={32} />
          <Skeleton variant="text" width="50%" height={32} />
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

/**
 * Metrics grid skeleton component
 * Mimics the structure of MetricsGrid for the initial loading state
 */
export function MetricsGridSkeleton() {
  return (
    <Grid container spacing={3}>
      {/* 8 sensor cards */}
      {Array.from({ length: 8 }).map((_, index) => (
        <Grid key={index} size={{ xs: 12, sm: 6, md: 4, lg: 3 }}>
          <CardSkeleton />
        </Grid>
      ))}

      {/* 2 featured cards */}
      <Grid size={{ xs: 12, md: 6 }}>
        <FeaturedCardSkeleton />
      </Grid>
      <Grid size={{ xs: 12, md: 6 }}>
        <FeaturedCardSkeleton />
      </Grid>
    </Grid>
  );
}

/**
 * Dashboard skeleton component
 * Full dashboard loading state with header and grid
 */
export function DashboardSkeleton() {
  return (
    <Container maxWidth="xl" sx={{ py: 3 }}>
      {/* Header skeleton */}
      <Box sx={{ mb: 4 }}>
        <Skeleton variant="text" width="30%" height={40} sx={{ mb: 1 }} />
        <Skeleton variant="text" width="50%" height={24} />
      </Box>

      {/* Metrics grid skeleton */}
      <Box sx={{ mb: 6 }}>
        <MetricsGridSkeleton />
      </Box>

      {/* Charts section skeleton */}
      <Box sx={{ mb: 4 }}>
        <Skeleton variant="text" width="30%" height={32} sx={{ mb: 1 }} />
        <Skeleton variant="text" width="60%" height={20} />
      </Box>
    </Container>
  );
}

/**
 * Simple loading spinner component
 * For minimal loading states
 */
export function LoadingSpinner() {
  return (
    <Box
      sx={{
        display: 'flex',
        justifyContent: 'center',
        alignItems: 'center',
        minHeight: '50vh',
      }}
    >
      <Box sx={{ textAlign: 'center' }}>
        <Skeleton variant="circular" width={60} height={60} sx={{ mx: 'auto', mb: 2 }} />
        <Typography variant="body2" color="text.secondary">
          Loading...
        </Typography>
      </Box>
    </Box>
  );
}
