import { useAtomValue } from 'jotai';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import Grid from '@mui/material/Grid';
import LinearProgress from '@mui/material/LinearProgress';
import Skeleton from '@mui/material/Skeleton';
import Typography from '@mui/material/Typography';
import {
  Memory as MemoryIcon,
  Thermostat as ThermostatIcon,
  AccessTime as UptimeIcon,
  Wifi as WifiIcon,
  Schedule as TimeIcon,
  Router as NetworkIcon,
} from '@mui/icons-material';
import { healthAtom, stateAtom, deviceInfoAtom } from '../../store/atoms';
import { formatUptime, formatBytes, formatEpoch, calculatePercentage } from '../../utils/formatting';
import { getSignalStrength, getSignalStrengthLabel, getSignalStrengthColor } from '../../utils/validation';

/**
 * System health card displaying system metrics
 */
export function SystemHealth() {
  const health = useAtomValue(healthAtom);
  const state = useAtomValue(stateAtom);
  const deviceInfo = useAtomValue(deviceInfoAtom);

  if (!health || !deviceInfo) {
    return (
      <Card sx={{ height: '100%' }}>
        <CardContent>
          <Typography variant="h6" gutterBottom>
            System Health
          </Typography>
          <Box mt={2}>
            <Skeleton variant="rectangular" height={200} />
          </Box>
        </CardContent>
      </Card>
    );
  }

  // Calculate heap usage percentage (assuming some reasonable total heap size)
  const heapPercentage = calculatePercentage(health.free_heap, 327680, 1); // 320KB typical ESP32

  // Get signal strength category
  const signalStrength = getSignalStrength(health.wifi_rssi);
  const signalLabel = getSignalStrengthLabel(signalStrength);
  const signalColor = getSignalStrengthColor(signalStrength);

  return (
    <Card sx={{ height: '100%' }}>
      <CardContent>
        <Typography variant="h6" gutterBottom>
          System Health
        </Typography>

        <Grid container spacing={2} mt={1}>
          {/* Uptime */}
          <Grid size={{ xs: 6 }}>
            <Box display="flex" alignItems="center" gap={1}>
              <UptimeIcon fontSize="small" color="action" />
              <Box>
                <Typography variant="caption" color="text.secondary">
                  Uptime
                </Typography>
                <Typography variant="body2" fontWeight={500}>
                  {formatUptime(health.uptime)}
                </Typography>
              </Box>
            </Box>
          </Grid>

          {/* Free Heap */}
          <Grid size={{ xs: 6 }}>
            <Box display="flex" alignItems="center" gap={1}>
              <MemoryIcon fontSize="small" color="action" />
              <Box flex={1}>
                <Typography variant="caption" color="text.secondary">
                  Free Heap
                </Typography>
                <Typography variant="body2" fontWeight={500}>
                  {formatBytes(health.free_heap)} ({heapPercentage}%)
                </Typography>
                <LinearProgress
                  variant="determinate"
                  value={heapPercentage}
                  sx={{ mt: 0.5, height: 4, borderRadius: 2 }}
                  color={heapPercentage > 30 ? 'success' : heapPercentage > 15 ? 'warning' : 'error'}
                />
              </Box>
            </Box>
          </Grid>

          {/* MCU Temperature */}
          {state?.mcu_temp_c != null && (
            <Grid size={{ xs: 6 }}>
              <Box display="flex" alignItems="center" gap={1}>
                <ThermostatIcon fontSize="small" color="action" />
                <Box>
                  <Typography variant="caption" color="text.secondary">
                    MCU Temp
                  </Typography>
                  <Typography variant="body2" fontWeight={500}>
                    {state.mcu_temp_c.toFixed(1)}°C
                  </Typography>
                </Box>
              </Box>
            </Grid>
          )}

          {/* WiFi RSSI */}
          <Grid size={{ xs: 6 }}>
            <Box display="flex" alignItems="center" gap={1}>
              <WifiIcon fontSize="small" color="action" />
              <Box>
                <Typography variant="caption" color="text.secondary">
                  WiFi Signal
                </Typography>
                <Box display="flex" alignItems="center" gap={1}>
                  <Typography variant="body2" fontWeight={500}>
                    {health.wifi_rssi} dBm
                  </Typography>
                  <Chip
                    label={signalLabel}
                    size="small"
                    color={signalColor}
                    sx={{ height: 20, fontSize: '0.7rem' }}
                  />
                </Box>
              </Box>
            </Box>
          </Grid>

          {/* Time Sync Status */}
          <Grid size={{ xs: 12 }}>
            <Box display="flex" alignItems="center" gap={1}>
              <TimeIcon fontSize="small" color="action" />
              <Box flex={1}>
                <Typography variant="caption" color="text.secondary">
                  Time Sync
                </Typography>
                <Box display="flex" alignItems="center" gap={1} mt={0.5}>
                  <Chip
                    label={health.time_synced ? 'Synced' : 'Not Synced'}
                    size="small"
                    color={health.time_synced ? 'success' : 'warning'}
                  />
                  {health.time_synced && health.epoch && (
                    <Typography variant="caption" color="text.secondary">
                      {formatEpoch(health.epoch)}
                    </Typography>
                  )}
                </Box>
              </Box>
            </Box>
          </Grid>

          {/* Network Info */}
          <Grid size={{ xs: 12 }}>
            <Box display="flex" alignItems="flex-start" gap={1}>
              <NetworkIcon fontSize="small" color="action" sx={{ mt: 0.5 }} />
              <Box flex={1}>
                <Typography variant="caption" color="text.secondary">
                  Network
                </Typography>
                <Box display="flex" flexDirection="column" gap={0.5} mt={0.5}>
                  <Typography variant="body2">
                    Mode: <strong>{deviceInfo.network.mode}</strong>
                  </Typography>
                  {deviceInfo.network.sta_ip && (
                    <Typography variant="body2" fontSize="0.85rem">
                      STA IP: {deviceInfo.network.sta_ip}
                    </Typography>
                  )}
                  {deviceInfo.network.ap_ip && (
                    <Typography variant="body2" fontSize="0.85rem">
                      AP IP: {deviceInfo.network.ap_ip}
                    </Typography>
                  )}
                </Box>
              </Box>
            </Box>
          </Grid>
        </Grid>
      </CardContent>
    </Card>
  );
}
