import Box from '@mui/material/Box';
import Chip from '@mui/material/Chip';
import CircularProgress from '@mui/material/CircularProgress';
import { useAtomValue } from 'jotai';
import WifiIcon from '@mui/icons-material/Wifi';
import CloudIcon from '@mui/icons-material/Cloud';
import CableIcon from '@mui/icons-material/Cable';
import { connectionStatusAtom, wsReconnectingAtom } from '../../store/atoms';

export function ConnectionStatus() {
  const connectionStatus = useAtomValue(connectionStatusAtom);
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
      <Chip
        icon={<CableIcon />}
        label="WS"
        size="small"
        color={connectionStatus.websocket ? 'success' : 'error'}
        variant={connectionStatus.websocket ? 'filled' : 'outlined'}
        sx={{ display: { xs: 'none', md: 'flex' } }}
      />

      {/* WiFi status */}
      <Chip
        icon={<WifiIcon />}
        label="WiFi"
        size="small"
        color={connectionStatus.wifi ? 'success' : 'error'}
        variant={connectionStatus.wifi ? 'filled' : 'outlined'}
        sx={{ display: { xs: 'none', md: 'flex' } }}
      />

      {/* MQTT status */}
      <Chip
        icon={<CloudIcon />}
        label="MQTT"
        size="small"
        color={connectionStatus.mqtt ? 'success' : 'error'}
        variant={connectionStatus.mqtt ? 'filled' : 'outlined'}
        sx={{ display: { xs: 'none', md: 'flex' } }}
      />
    </Box>
  );
}
