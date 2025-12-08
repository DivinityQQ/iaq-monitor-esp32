import { useEffect } from 'react';
import { useSetAtom } from 'jotai';
import Box from '@mui/material/Box';
import Container from '@mui/material/Container';
import Grid from '@mui/material/Grid';
import Typography from '@mui/material/Typography';
import { refreshOTAInfoAtom } from '../../store/atoms';
import { VersionInfo } from './VersionInfo';
import { FirmwareUpdate } from './FirmwareUpdate';
import { FrontendUpdate } from './FrontendUpdate';

/**
 * OTA update dashboard displaying version info and update options
 */
export function OTADashboard() {
  const refreshInfo = useSetAtom(refreshOTAInfoAtom);

  // Fetch OTA info on mount
  useEffect(() => {
    refreshInfo();
  }, [refreshInfo]);

  return (
    <Container maxWidth="xl" sx={{ py: 3, px: { xs: 2, sm: 3 } }}>
      {/* Header */}
      <Box sx={{ mb: 4 }}>
        <Typography variant="h4" component="h1" fontWeight={600} gutterBottom>
          System Update
        </Typography>
        <Typography variant="body1" color="text.secondary">
          Update firmware and frontend components over the air
        </Typography>
      </Box>

      {/* Main grid layout */}
      <Grid container spacing={3}>
        {/* Version Info - Full width on top */}
        <Grid size={12}>
          <VersionInfo />
        </Grid>

        {/* Firmware Update */}
        <Grid size={{ xs: 12, md: 6 }}>
          <FirmwareUpdate />
        </Grid>

        {/* Frontend Update */}
        <Grid size={{ xs: 12, md: 6 }}>
          <FrontendUpdate />
        </Grid>
      </Grid>
    </Container>
  );
}
