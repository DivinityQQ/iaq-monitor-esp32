import { useState } from 'react';
import {
  Box,
  Card,
  CardContent,
  Typography,
  Button,
  TextField,
  Switch,
  FormControlLabel,
  Alert,
  Table,
  TableBody,
  TableCell,
  TableContainer,
  TableHead,
  TableRow,
  Paper,
  Chip,
  IconButton,
  InputAdornment,
  CircularProgress,
  Skeleton,
} from '@mui/material';
import {
  Wifi as WifiIcon,
  WifiOff as WifiOffIcon,
  Refresh as RefreshIcon,
  Visibility as VisibilityIcon,
  VisibilityOff as VisibilityOffIcon,
  SignalCellularAlt as SignalIcon,
} from '@mui/icons-material';
import { useAtomValue } from 'jotai';
import { deviceInfoAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import type { WiFiAP } from '../../api/types';
import { validateSSID, validatePassword, getSignalBars } from '../../utils/validation';

export function WiFiConfig() {
  const deviceInfo = useAtomValue(deviceInfoAtom);

  // Form state
  const [ssid, setSSID] = useState('');
  const [password, setPassword] = useState('');
  const [showPassword, setShowPassword] = useState(false);
  const [restartAfterConnect, setRestartAfterConnect] = useState(true);

  // Scanner state
  const [scanResults, setScanResults] = useState<WiFiAP[]>([]);
  const [scanning, setScanning] = useState(false);
  const [scanError, setScanError] = useState<string | null>(null);

  // Connection state
  const [connecting, setConnecting] = useState(false);
  const [connectionError, setConnectionError] = useState<string | null>(null);
  const [connectionSuccess, setConnectionSuccess] = useState(false);

  // Restart state
  const [restarting, setRestarting] = useState(false);
  const [restartError, setRestartError] = useState<string | null>(null);

  // Validation errors
  const [ssidError, setSSIDError] = useState<string | null>(null);
  const [passwordError, setPasswordError] = useState<string | null>(null);

  const wifiConnected = deviceInfo?.network?.wifi_connected || false;

  const handleScan = async () => {
    setScanning(true);
    setScanError(null);
    setScanResults([]);

    try {
      const result = await apiClient.scanWiFi(20);
      setScanResults(result.aps);
    } catch (error) {
      console.error('WiFi scan failed:', error);
      setScanError(error instanceof Error ? error.message : 'Failed to scan networks');
    } finally {
      setScanning(false);
    }
  };

  const handleSelectAP = (ap: WiFiAP) => {
    setSSID(ap.ssid);
    setSSIDError(null);
    // Clear password when selecting new network
    setPassword('');
    setPasswordError(null);
    setConnectionError(null);
    setConnectionSuccess(false);
  };

  const handleConnect = async () => {
    // Validate form
    const ssidValidation = validateSSID(ssid);
    const passwordValidation = validatePassword(password, 'WPA2_PSK');

    setSSIDError(ssidValidation);
    setPasswordError(passwordValidation);

    if (ssidValidation || passwordValidation) {
      return;
    }

    setConnecting(true);
    setConnectionError(null);
    setConnectionSuccess(false);

    try {
      await apiClient.setWiFi({
        ssid,
        password,
        restart: restartAfterConnect,
      });

      setConnectionSuccess(true);
      setConnectionError(null);

      // Clear form
      setPassword('');

      if (restartAfterConnect) {
        setConnectionSuccess(false);
        // Show restarting message
        setTimeout(() => {
          setConnectionError('Device is restarting. Please reconnect in a few moments.');
        }, 1000);
      }
    } catch (error) {
      console.error('WiFi connection failed:', error);
      setConnectionError(error instanceof Error ? error.message : 'Failed to connect to WiFi');
      setConnectionSuccess(false);
    } finally {
      setConnecting(false);
    }
  };

  const handleRestart = async () => {
    setRestarting(true);
    setRestartError(null);

    try {
      await apiClient.restartWiFi();
      setRestartError('WiFi restarted successfully. Reconnecting...');
    } catch (error) {
      console.error('WiFi restart failed:', error);
      setRestartError(error instanceof Error ? error.message : 'Failed to restart WiFi');
    } finally {
      setRestarting(false);
    }
  };

  const renderSignalBars = (rssi: number) => {
    const bars = getSignalBars(rssi);
    return (
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 0.5 }}>
        <SignalIcon
          sx={{
            fontSize: 20,
            color: bars >= 3 ? 'success.main' : bars === 2 ? 'warning.main' : 'error.main'
          }}
        />
        <Typography variant="caption" color="text.secondary">
          {rssi} dBm
        </Typography>
      </Box>
    );
  };

  if (!deviceInfo) {
    return (
      <Box>
        <Skeleton variant="rectangular" height={200} sx={{ borderRadius: 3, mb: 3 }} />
        <Skeleton variant="rectangular" height={300} sx={{ borderRadius: 3, mb: 3 }} />
        <Skeleton variant="rectangular" height={400} sx={{ borderRadius: 3 }} />
      </Box>
    );
  }

  return (
    <Box>
      {/* Current WiFi Status Card */}
      <Card
        sx={{
          mb: 3,
          transition: 'transform 0.2s, box-shadow 0.2s',
          '&:hover': {
            transform: 'translateY(-4px)',
            boxShadow: (theme) => theme.shadows[8],
          },
        }}
      >
        <CardContent>
          <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 2 }}>
            <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
              {wifiConnected ? (
                <WifiIcon sx={{ color: 'success.main' }} />
              ) : (
                <WifiOffIcon sx={{ color: 'error.main' }} />
              )}
              <Typography variant="h6">WiFi Status</Typography>
            </Box>
            <Chip
              label={wifiConnected ? 'Connected' : 'Disconnected'}
              color={wifiConnected ? 'success' : 'error'}
              size="small"
            />
          </Box>

          {wifiConnected && deviceInfo.network.sta_ip && (
            <Box sx={{ mb: 2 }}>
              <Typography variant="body2" color="text.secondary" gutterBottom>
                IP Address
              </Typography>
              <Typography variant="body1">{deviceInfo.network.sta_ip}</Typography>
            </Box>
          )}

          <Box sx={{ display: 'flex', gap: 2 }}>
            <Button
              variant="outlined"
              startIcon={restarting ? <CircularProgress size={16} /> : <RefreshIcon />}
              onClick={handleRestart}
              disabled={restarting}
            >
              Restart WiFi
            </Button>
          </Box>

          {restartError && (
            <Alert severity={restartError.includes('success') ? 'info' : 'error'} sx={{ mt: 2 }}>
              {restartError}
            </Alert>
          )}
        </CardContent>
      </Card>

      {/* Network Scanner Card */}
      <Card
        sx={{
          mb: 3,
          transition: 'transform 0.2s, box-shadow 0.2s',
          '&:hover': {
            transform: 'translateY(-4px)',
            boxShadow: (theme) => theme.shadows[8],
          },
        }}
      >
        <CardContent>
          <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 2 }}>
            <Typography variant="h6">Available Networks</Typography>
            <Button
              variant="contained"
              startIcon={scanning ? <CircularProgress size={16} /> : <RefreshIcon />}
              onClick={handleScan}
              disabled={scanning}
            >
              {scanning ? 'Scanning...' : 'Scan Networks'}
            </Button>
          </Box>

          {scanError && (
            <Alert severity="error" sx={{ mb: 2 }}>
              {scanError}
            </Alert>
          )}

          {scanResults.length > 0 && (
            <TableContainer component={Paper} variant="outlined">
              <Table size="small">
                <TableHead>
                  <TableRow>
                    <TableCell>SSID</TableCell>
                    <TableCell>Signal</TableCell>
                    <TableCell>Channel</TableCell>
                    <TableCell>Security</TableCell>
                  </TableRow>
                </TableHead>
                <TableBody>
                  {scanResults.map((ap, index) => (
                    <TableRow
                      key={`${ap.ssid}-${index}`}
                      hover
                      sx={{ cursor: 'pointer' }}
                      onClick={() => handleSelectAP(ap)}
                    >
                      <TableCell>
                        <Typography variant="body2" fontWeight={500}>
                          {ap.ssid || '<Hidden Network>'}
                        </Typography>
                      </TableCell>
                      <TableCell>{renderSignalBars(ap.rssi)}</TableCell>
                      <TableCell>{ap.channel}</TableCell>
                      <TableCell>
                        <Chip
                          label={ap.auth}
                          size="small"
                          variant="outlined"
                          color={ap.auth === 'OPEN' ? 'warning' : 'default'}
                        />
                      </TableCell>
                    </TableRow>
                  ))}
                </TableBody>
              </Table>
            </TableContainer>
          )}

          {!scanning && scanResults.length === 0 && !scanError && (
            <Typography variant="body2" color="text.secondary" textAlign="center" py={3}>
              Click "Scan Networks" to find available WiFi networks
            </Typography>
          )}
        </CardContent>
      </Card>

      {/* Connection Form Card */}
      <Card
        sx={{
          transition: 'transform 0.2s, box-shadow 0.2s',
          '&:hover': {
            transform: 'translateY(-4px)',
            boxShadow: (theme) => theme.shadows[8],
          },
        }}
      >
        <CardContent>
          <Typography variant="h6" gutterBottom>
            Connect to Network
          </Typography>

          {connectionSuccess && (
            <Alert severity="success" sx={{ mb: 2 }}>
              WiFi configuration updated successfully!
              {restartAfterConnect && ' Device is restarting...'}
            </Alert>
          )}

          {connectionError && (
            <Alert severity="error" sx={{ mb: 2 }}>
              {connectionError}
            </Alert>
          )}

          <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <TextField
              label="SSID"
              value={ssid}
              onChange={(e) => {
                setSSID(e.target.value);
                setSSIDError(null);
                setConnectionError(null);
                setConnectionSuccess(false);
              }}
              error={!!ssidError}
              helperText={ssidError || 'Select from scan results or enter manually'}
              fullWidth
              disabled={connecting}
            />

            <TextField
              label="Password"
              type={showPassword ? 'text' : 'password'}
              value={password}
              onChange={(e) => {
                setPassword(e.target.value);
                setPasswordError(null);
                setConnectionError(null);
                setConnectionSuccess(false);
              }}
              error={!!passwordError}
              helperText={passwordError || 'Minimum 8 characters for WPA/WPA2'}
              fullWidth
              disabled={connecting}
              InputProps={{
                endAdornment: (
                  <InputAdornment position="end">
                    <IconButton
                      onClick={() => setShowPassword(!showPassword)}
                      edge="end"
                      disabled={connecting}
                    >
                      {showPassword ? <VisibilityOffIcon /> : <VisibilityIcon />}
                    </IconButton>
                  </InputAdornment>
                ),
              }}
            />

            <FormControlLabel
              control={
                <Switch
                  checked={restartAfterConnect}
                  onChange={(e) => setRestartAfterConnect(e.target.checked)}
                  disabled={connecting}
                />
              }
              label="Restart device after connecting"
            />

            <Button
              variant="contained"
              size="large"
              onClick={handleConnect}
              disabled={connecting || !ssid}
              startIcon={connecting ? <CircularProgress size={16} /> : undefined}
            >
              {connecting ? 'Connecting...' : 'Connect'}
            </Button>
          </Box>
        </CardContent>
      </Card>
    </Box>
  );
}
