import { Box, Chip } from '@mui/material';
import WifiIcon from '@mui/icons-material/Wifi';
import CloudIcon from '@mui/icons-material/Cloud';
import CableIcon from '@mui/icons-material/Cable';

export function ConnectionStatus() {
  // TODO: Connect to Jotai atoms for real connection status
  // For now, showing placeholder status
  const wsConnected = true;
  const wifiConnected = true;
  const mqttConnected = false;

  return (
    <Box sx={{ display: 'flex', gap: 1 }}>
      <Chip
        icon={<CableIcon />}
        label="WS"
        size="small"
        color={wsConnected ? 'success' : 'error'}
        variant={wsConnected ? 'filled' : 'outlined'}
        sx={{ display: { xs: 'none', md: 'flex' } }}
      />
      <Chip
        icon={<WifiIcon />}
        label="WiFi"
        size="small"
        color={wifiConnected ? 'success' : 'error'}
        variant={wifiConnected ? 'filled' : 'outlined'}
        sx={{ display: { xs: 'none', md: 'flex' } }}
      />
      <Chip
        icon={<CloudIcon />}
        label="MQTT"
        size="small"
        color={mqttConnected ? 'success' : 'error'}
        variant={mqttConnected ? 'filled' : 'outlined'}
        sx={{ display: { xs: 'none', md: 'flex' } }}
      />
    </Box>
  );
}
