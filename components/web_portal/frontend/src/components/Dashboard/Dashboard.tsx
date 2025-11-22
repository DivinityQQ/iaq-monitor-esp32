import Box from '@mui/material/Box';
import Container from '@mui/material/Container';
import Typography from '@mui/material/Typography';
import { MetricsGrid } from './MetricsGrid';

/**
 * Main Dashboard view component
 *
 * Features:
 * - Container for all dashboard content
 * - Metrics grid with sensor cards
 * - Section headers with typography
 * - Proper spacing and responsive layout
 * - Placeholder for charts (Week 4)
 */
export function Dashboard() {
  return (
    <Container maxWidth="xl" sx={{ py: 3, px: { xs: 2, sm: 3 } }}>
      {/* Dashboard Header */}
      <Box display="flex" justifyContent="space-between" alignItems="flex-start" sx={{ mb: 4 }}>
        <Box>
          <Typography variant="h4" component="h1" fontWeight={600} gutterBottom>
            Dashboard
          </Typography>
          <Typography variant="body1" color="text.secondary">
            Real-time indoor air quality monitoring
          </Typography>
        </Box>
      </Box>

      {/* Metrics Grid */}
      <Box sx={{ mb: 6 }}>
        <MetricsGrid />
      </Box>
    </Container>
  );
}
