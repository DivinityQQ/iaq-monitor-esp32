import { useState } from 'react';
import { useAtomValue, useSetAtom } from 'jotai';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import IconButton from '@mui/material/IconButton';
import Skeleton from '@mui/material/Skeleton';
import Tooltip from '@mui/material/Tooltip';
import Typography from '@mui/material/Typography';
import {
  Cloud as CloudIcon,
  CloudOff as CloudOffIcon,
  Refresh as RefreshIcon,
} from '@mui/icons-material';
import { mqttStatusAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import { useNotification } from '../../contexts/SnackbarContext';
import { logger } from '../../utils/logger';

/**
 * MQTT status card displaying broker connection information
 */
export function MQTTHealth() {
  const mqttStatus = useAtomValue(mqttStatusAtom);
  const setMqttStatus = useSetAtom(mqttStatusAtom);
  const { showNotification } = useNotification();
  const [refreshing, setRefreshing] = useState(false);

  const handleRefresh = async () => {
    setRefreshing(true);
    try {
      const status = await apiClient.getMQTTStatus();
      setMqttStatus(status);
      showNotification({ message: 'MQTT status refreshed', severity: 'success' });
    } catch (error) {
      logger.error('Failed to refresh MQTT status:', error);
      showNotification({
        message: 'Failed to refresh MQTT status',
        severity: 'error',
      });
    } finally {
      setRefreshing(false);
    }
  };

  if (!mqttStatus) {
    return (
      <Card sx={{ height: '100%' }}>
        <CardContent>
          <Box display="flex" alignItems="center" justifyContent="space-between" mb={2}>
            <Typography variant="h6">MQTT Status</Typography>
            <Skeleton variant="circular" width={32} height={32} />
          </Box>

          <Box display="flex" flexDirection="column" gap={2}>
            {/* Connection Status Skeleton */}
            <Box>
              <Skeleton variant="text" width={80} height={16} sx={{ mb: 1 }} />
              <Box display="flex" alignItems="center" gap={1}>
                <Skeleton variant="circular" width={36} height={36} />
                <Skeleton variant="rounded" width={100} height={24} />
              </Box>
            </Box>

            {/* Configuration Status Skeleton */}
            <Box>
              <Skeleton variant="rounded" width={120} height={24} />
              <Skeleton variant="text" width="80%" height={16} sx={{ mt: 1 }} />
            </Box>

            {/* Broker URL Skeleton */}
            <Box>
              <Skeleton variant="text" width={60} height={16} sx={{ mb: 0.5 }} />
              <Skeleton variant="text" width="90%" height={20} />
            </Box>

            {/* Info message Skeleton */}
            <Box mt={1}>
              <Skeleton variant="text" width="95%" height={16} />
            </Box>
          </Box>
        </CardContent>
      </Card>
    );
  }

  const isConnected = mqttStatus.connected;
  const isConfigured = mqttStatus.configured;

  return (
    <Card sx={{ height: '100%' }}>
      <CardContent>
        <Box display="flex" alignItems="center" justifyContent="space-between" mb={2}>
          <Typography variant="h6">MQTT Status</Typography>
          <Tooltip title="Refresh status">
            <IconButton
              size="small"
              onClick={handleRefresh}
              disabled={refreshing}
              sx={{
                animation: refreshing ? 'spin 1s linear infinite' : 'none',
                '@keyframes spin': {
                  '0%': { transform: 'rotate(0deg)' },
                  '100%': { transform: 'rotate(360deg)' },
                },
              }}
            >
              <RefreshIcon />
            </IconButton>
          </Tooltip>
        </Box>

        <Box display="flex" flexDirection="column" gap={2}>
          {/* Connection Status */}
          <Box>
            <Typography variant="caption" color="text.secondary" display="block" mb={1}>
              Connection
            </Typography>
            <Box display="flex" alignItems="center" gap={1}>
              {isConnected ? (
                <CloudIcon color="success" fontSize="large" />
              ) : (
                <CloudOffIcon color="disabled" fontSize="large" />
              )}
              <Chip
                label={isConnected ? 'Connected' : 'Disconnected'}
                color={isConnected ? 'success' : 'default'}
                size="small"
              />
            </Box>
          </Box>

          {/* Configuration Status */}
          {!isConfigured && (
            <Box>
              <Chip label="Not Configured" color="warning" size="small" />
              <Typography variant="caption" color="text.secondary" display="block" mt={1}>
                Configure MQTT in the Configuration tab
              </Typography>
            </Box>
          )}

          {/* Broker URL */}
          {isConfigured && mqttStatus.broker_url && (
            <Box>
              <Typography variant="caption" color="text.secondary" display="block" mb={0.5}>
                Broker
              </Typography>
              <Typography variant="body2" fontWeight={500} sx={{ wordBreak: 'break-all' }}>
                {mqttStatus.broker_url}
              </Typography>
            </Box>
          )}

          {/* Info message */}
          <Box mt={1}>
            <Typography variant="caption" color="text.secondary">
              {isConnected
                ? 'Sensor data is being published to MQTT broker'
                : isConfigured
                ? 'Attempting to connect to broker...'
                : 'MQTT publishing is disabled'}
            </Typography>
          </Box>
        </Box>
      </CardContent>
    </Card>
  );
}
