import { useAtomValue } from 'jotai';
import Alert from '@mui/material/Alert';
import Box from '@mui/material/Box';
import Container from '@mui/material/Container';
import Grid from '@mui/material/Grid';
import Typography from '@mui/material/Typography';
import { powerAtom } from '../../store/atoms';
import { BatteryStatus } from './BatteryStatus';
import { SupplyStatus } from './SupplyStatus';
import { PowerOutputs } from './PowerOutputs';
import { ChargingConfig } from './ChargingConfig';
import { BatteryAlarms } from './BatteryAlarms';

/**
 * Power dashboard displaying battery status, supply info, and power controls
 */
export function PowerDashboard() {
  const power = useAtomValue(powerAtom);

  // Show unavailable state when PowerFeather is disabled or uninitialized
  if (power && !power.available) {
    return (
      <Container maxWidth="xl" sx={{ py: 3, px: { xs: 2, sm: 3 } }}>
        <Box sx={{ mb: 4 }}>
          <Typography variant="h4" component="h1" fontWeight={600} gutterBottom>
            Power
          </Typography>
          <Typography variant="body1" color="text.secondary">
            Monitor battery status and power configuration
          </Typography>
        </Box>
        <Alert severity="info" sx={{ maxWidth: 600 }}>
          PowerFeather is not available on this device.
          {power.error && (
            <Typography variant="body2" sx={{ mt: 1 }}>
              {power.error}
            </Typography>
          )}
        </Alert>
      </Container>
    );
  }

  return (
    <Container maxWidth="xl" sx={{ py: 3, px: { xs: 2, sm: 3 } }}>
      {/* Header */}
      <Box sx={{ mb: 4 }}>
        <Typography variant="h4" component="h1" fontWeight={600} gutterBottom>
          Power
        </Typography>
        <Typography variant="body1" color="text.secondary">
          Monitor battery status and power configuration
        </Typography>
      </Box>

      {/* Main grid layout */}
      <Grid container spacing={3}>
        {/* Left column: Battery Status + Supply Status + Charging Config */}
        <Grid size={{ xs: 12, md: 6 }}>
          <Grid container spacing={3}>
            <Grid size={12}>
              <BatteryStatus />
            </Grid>
            <Grid size={12}>
              <SupplyStatus />
            </Grid>
            <Grid size={12}>
              <ChargingConfig />
            </Grid>
          </Grid>
        </Grid>

        {/* Right column: Power Outputs + Battery Alarms */}
        <Grid size={{ xs: 12, md: 6 }}>
          <Grid container spacing={3}>
            <Grid size={12}>
              <PowerOutputs />
            </Grid>
            <Grid size={12}>
              <BatteryAlarms />
            </Grid>
          </Grid>
        </Grid>
      </Grid>
    </Container>
  );
}
