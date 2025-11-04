import Box from '@mui/material/Box';
import Chip from '@mui/material/Chip';
import CircularProgress from '@mui/material/CircularProgress';
import { useAtomValue } from 'jotai';
import WifiIcon from '@mui/icons-material/Wifi';
import CloudIcon from '@mui/icons-material/Cloud';
import CableIcon from '@mui/icons-material/Cable';
import { wsConnectedAtom, wsReconnectingAtom, wifiConnectedAtom, mqttConnectedAtom } from '../../store/atoms';
import { memo } from 'react';

const WSStatusChip = memo(function WSStatusChip() {
  const wsConnected = useAtomValue(wsConnectedAtom);
  return (
    <Chip icon={<CableIcon />} label="WS" size="small" color={wsConnected ? 'success' : 'error'} variant={wsConnected ? 'filled' : 'outlined'} sx={{ display: { xs: 'none', tablet: 'flex' } }} />
  );
});

const WiFiStatusChip = memo(function WiFiStatusChip() {
  const wifiConnected = useAtomValue(wifiConnectedAtom);
  return (
    <Chip icon={<WifiIcon />} label="WiFi" size="small" color={wifiConnected ? 'success' : 'error'} variant={wifiConnected ? 'filled' : 'outlined'} sx={{ display: { xs: 'none', tablet: 'flex' } }} />
  );
});

const MQTTStatusChip = memo(function MQTTStatusChip() {
  const mqttConnected = useAtomValue(mqttConnectedAtom);
  return (
    <Chip icon={<CloudIcon />} label="MQTT" size="small" color={mqttConnected ? 'success' : 'error'} variant={mqttConnected ? 'filled' : 'outlined'} sx={{ display: { xs: 'none', tablet: 'flex' } }} />
  );
});

export function ConnectionStatus() {
  const isReconnecting = useAtomValue(wsReconnectingAtom);

  return (
    <Box sx={{ display: 'flex', gap: 1, alignItems: 'center' }}>
      {/* Reconnecting indicator */}
      {isReconnecting && (
        <Chip
          icon={<CircularProgress size={14} sx={{ color: 'inherit' }} />}
          label="Reconnecting"
          size="small"
          color="warning"
          variant="outlined"
          sx={{ display: { xs: 'none', sm: 'flex' } }}
        />
      )}

      {/* WebSocket status */}
      <WSStatusChip />

      {/* WiFi status */}
      <WiFiStatusChip />

      {/* MQTT status */}
      <MQTTStatusChip />
    </Box>
  );
}
