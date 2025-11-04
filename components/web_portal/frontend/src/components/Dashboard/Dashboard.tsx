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
    <Container maxWidth="xl" sx={{ py: 3 }}>
      {/* Dashboard Header */}
      <Box sx={{ mb: 4 }}>
        <Typography variant="h4" component="h1" fontWeight={600} gutterBottom>
          Dashboard
        </Typography>
        <Typography variant="body1" color="text.secondary">
          Real-time indoor air quality monitoring
        </Typography>
      </Box>

      {/* Metrics Grid */}
      <Box sx={{ mb: 6 }}>
        <MetricsGrid />
      </Box>

      {/* Charts Section - Placeholder for Week 4 */}
      <Box sx={{ mb: 4 }}>
        <Typography variant="h5" component="h2" fontWeight={600} gutterBottom>
          Historical Trends
        </Typography>
        <Typography variant="body2" color="text.secondary" sx={{ mb: 3 }}>
          Coming in Week 4: Interactive charts with 24-hour trends
        </Typography>
        {/* Charts will be implemented in Week 4 */}
      </Box>
    </Container>
  );
}
