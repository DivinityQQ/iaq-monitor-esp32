import { useState, useEffect } from 'react';
import { useAtomValue } from 'jotai';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import Grid from '@mui/material/Grid';
import Typography from '@mui/material/Typography';
import { RestartAlt as RestartIcon } from '@mui/icons-material';
import { sensorStatusMapAtom } from '../../store/atoms';
import { SystemHealth } from './SystemHealth';
import { MQTTHealth } from './MQTTHealth';
import { SensorStatus } from './SensorStatus';
import { ConfirmDialog } from '../Common/ConfirmDialog';
import { useNotification } from '../../contexts/SnackbarContext';
import { apiClient } from '../../api/client';
import { ALL_SENSOR_IDS } from '../../api/types';
import { logger } from '../../utils/logger';

/**
 * Health dashboard displaying system health, MQTT status, and sensor statuses
 */
export function HealthDashboard() {
  const sensorStatusMap = useAtomValue(sensorStatusMapAtom);
  const { showNotification } = useNotification();
  const [restartDialogOpen, setRestartDialogOpen] = useState(false);
  const [restarting, setRestarting] = useState(false);

  const handleRestartDevice = async () => {
    setRestarting(true);
    try {
      await apiClient.restartDevice();
      showNotification({
        message: 'Device restarting... WebSocket will reconnect automatically',
        severity: 'info',
      });
      setRestartDialogOpen(false);
      // Note: WebSocket auto-reconnect will handle reconnection
      // No need for hard reload - let the connection restore naturally
    } catch (error) {
      logger.error('Failed to restart device:', error);
      showNotification({
        message: 'Failed to initiate device restart',
        severity: 'error',
      });
      setRestarting(false);
    }
  };

  // Reset restarting state after successful reconnection (when health updates arrive)
  useEffect(() => {
    if (restarting && sensorStatusMap) {
      setRestarting(false);
      showNotification({
        message: 'Device reconnected successfully',
        severity: 'success',
      });
    }
  }, [sensorStatusMap, restarting, showNotification]);

  return (
    <Box>
      {/* Header with restart button */}
      <Box display="flex" justifyContent="space-between" alignItems="flex-start" mb={3}>
        <Box sx={{ mb: 4 }}>
          <Typography variant="h4" component="h1" fontWeight={600} gutterBottom>
            System Health
          </Typography>
          <Typography variant="body1" color="text.secondary">
            Monitor system status, MQTT connection, and sensor health
          </Typography>
        </Box>
        <Button
          variant="outlined"
          color="error"
          startIcon={<RestartIcon />}
          onClick={() => setRestartDialogOpen(true)}
          disabled={restarting}
        >
          Restart Device
        </Button>
      </Box>

      {/* Top row: System Health and MQTT Status */}
      <Grid container spacing={3}>
        <Grid size={{ xs: 12, tablet: 12, md: 6 }}>
          <SystemHealth />
        </Grid>
        <Grid size={{ xs: 12, tablet: 12, md: 6 }}>
          <MQTTHealth />
        </Grid>

        {/* Sensor Status Grid */}
        {ALL_SENSOR_IDS.map((sensorId) => (
          <Grid size={{ xs: 6, tablet: 4, md: 3 }} key={sensorId}>
            <SensorStatus
              sensorId={sensorId}
              status={sensorStatusMap?.[sensorId]}
              loading={!sensorStatusMap?.[sensorId]}
            />
          </Grid>
        ))}
      </Grid>

      {/* Restart confirmation dialog */}
      <ConfirmDialog
        open={restartDialogOpen}
        title="Restart Device"
        message="Are you sure you want to restart the device? This will temporarily interrupt all monitoring and connections."
        confirmLabel="Restart"
        cancelLabel="Cancel"
        severity="error"
        onConfirm={handleRestartDevice}
        onCancel={() => setRestartDialogOpen(false)}
        loading={restarting}
      />
    </Box>
  );
}
