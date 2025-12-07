import { useState, useRef } from 'react';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import CircularProgress from '@mui/material/CircularProgress';
import FormControlLabel from '@mui/material/FormControlLabel';
import IconButton from '@mui/material/IconButton';
import InputAdornment from '@mui/material/InputAdornment';
import Paper from '@mui/material/Paper';
import Skeleton from '@mui/material/Skeleton';
import Switch from '@mui/material/Switch';
import Table from '@mui/material/Table';
import TableBody from '@mui/material/TableBody';
import TableCell from '@mui/material/TableCell';
import TableContainer from '@mui/material/TableContainer';
import TableHead from '@mui/material/TableHead';
import TableRow from '@mui/material/TableRow';
import TextField from '@mui/material/TextField';
import Typography from '@mui/material/Typography';
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
import { useNotification } from '../../contexts/SnackbarContext';
import { logger } from '../../utils/logger';

export function WiFiConfig() {
  const deviceInfo = useAtomValue(deviceInfoAtom);
  const { showNotification } = useNotification();

  // Form state
  const [ssid, setSSID] = useState('');
  const [password, setPassword] = useState('');
  const [showPassword, setShowPassword] = useState(false);
  const [restartAfterConnect, setRestartAfterConnect] = useState(true);

  // Scanner state
  const [scanResults, setScanResults] = useState<WiFiAP[]>([]);
  const [scanning, setScanning] = useState(false);
  const scanAbortController = useRef<AbortController | null>(null);

  // Connection state
  const [connecting, setConnecting] = useState(false);

  // Restart state
  const [restarting, setRestarting] = useState(false);

  // Validation errors
  const [ssidError, setSSIDError] = useState<string | null>(null);
  const [passwordError, setPasswordError] = useState<string | null>(null);

  const wifiConnected = deviceInfo?.network?.wifi_connected || false;

  const handleScan = async () => {
    // Cancel any ongoing scan
    if (scanAbortController.current) {
      scanAbortController.current.abort();
    }

    // Create new abort controller for this scan
    const controller = new AbortController();
    scanAbortController.current = controller;

    setScanning(true);
    setScanResults([]);

    try {
      const result = await apiClient.scanWiFi(20, controller.signal);
      setScanResults(result.aps);
      showNotification({
        message: `Found ${result.aps.length} network${result.aps.length !== 1 ? 's' : ''}`,
        severity: 'success',
      });
    } catch (error) {
      // Don't show error if request was cancelled
      if (error instanceof Error && error.name === 'AbortError') {
        logger.log('WiFi scan cancelled');
        return;
      }
      logger.error('WiFi scan failed:', error);
      showNotification({
        message: error instanceof Error ? error.message : 'Failed to scan networks',
        severity: 'error',
      });
    } finally {
      setScanning(false);
      scanAbortController.current = null;
    }
  };

  const handleSelectAP = (ap: WiFiAP) => {
    setSSID(ap.ssid);
    setSSIDError(null);
    // Clear password when selecting new network
    setPassword('');
    setPasswordError(null);
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

    try {
      await apiClient.setWiFi({
        ssid,
        password,
        restart: restartAfterConnect,
      });

      // Clear form
      setPassword('');

      if (restartAfterConnect) {
        // Show restarting message after a delay
        setTimeout(() => {
          showNotification({
            message: 'WiFi is restarting. Connection will be re-established shortly.',
            severity: 'info',
            duration: 5000,
          });
        }, 1000);
      } else {
        showNotification({
          message: 'WiFi configuration updated successfully!',
          severity: 'success',
        });
      }
    } catch (error) {
      logger.error('WiFi connection failed:', error);
      showNotification({
        message: error instanceof Error ? error.message : 'Failed to connect to WiFi',
        severity: 'error',
      });
    } finally {
      setConnecting(false);
    }
  };

  const handleRestart = async () => {
    setRestarting(true);

    try {
      await apiClient.restartWiFi();
      showNotification({
        message: 'WiFi restarted successfully. Reconnecting...',
        severity: 'info',
        duration: 4000,
      });
    } catch (error) {
      logger.error('WiFi restart failed:', error);
      showNotification({
        message: error instanceof Error ? error.message : 'Failed to restart WiFi',
        severity: 'error',
      });
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
        {/* WiFi Status Card Skeleton */}
        <Card sx={{ mb: 3 }}>
          <CardContent>
            <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 2 }}>
              <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                <Skeleton variant="circular" width={24} height={24} />
                <Skeleton variant="text" width={120} height={32} />
              </Box>
              <Skeleton variant="rounded" width={90} height={24} />
            </Box>
            <Box sx={{ mb: 2 }}>
              <Skeleton variant="text" width={80} height={20} />
              <Skeleton variant="text" width={140} height={24} />
            </Box>
            <Skeleton variant="rounded" width={130} height={36} />
          </CardContent>
        </Card>

        {/* Network Scanner Card Skeleton */}
        <Card sx={{ mb: 3 }}>
          <CardContent>
            <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 2 }}>
              <Skeleton variant="text" width={160} height={32} />
              <Skeleton variant="rounded" width={150} height={36} />
            </Box>
            <TableContainer component={Paper} variant="outlined">
              <Table size="small">
                <TableHead>
                  <TableRow>
                    <TableCell><Skeleton variant="text" width={60} /></TableCell>
                    <TableCell><Skeleton variant="text" width={60} /></TableCell>
                    <TableCell><Skeleton variant="text" width={70} /></TableCell>
                    <TableCell><Skeleton variant="text" width={70} /></TableCell>
                  </TableRow>
                </TableHead>
                <TableBody>
                  {[...Array(4)].map((_, index) => (
                    <TableRow key={index}>
                      <TableCell>
                        <Skeleton variant="text" width="80%" />
                      </TableCell>
                      <TableCell>
                        <Box sx={{ display: 'flex', alignItems: 'center', gap: 0.5 }}>
                          <Skeleton variant="circular" width={20} height={20} />
                          <Skeleton variant="text" width={50} />
                        </Box>
                      </TableCell>
                      <TableCell>
                        <Skeleton variant="text" width={30} />
                      </TableCell>
                      <TableCell>
                        <Skeleton variant="rounded" width={70} height={24} />
                      </TableCell>
                    </TableRow>
                  ))}
                </TableBody>
              </Table>
            </TableContainer>
          </CardContent>
        </Card>

        {/* Connection Form Card Skeleton */}
        <Card>
          <CardContent>
            <Skeleton variant="text" width={180} height={32} sx={{ mb: 2 }} />
            <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
              <Skeleton variant="rounded" height={56} />
              <Skeleton variant="rounded" height={56} />
              <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                <Skeleton variant="rounded" width={38} height={20} />
                <Skeleton variant="text" width={200} />
              </Box>
              <Skeleton variant="rounded" height={42} />
            </Box>
          </CardContent>
        </Card>
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

          {!scanning && scanResults.length === 0 && (
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

          <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <TextField
              label="SSID"
              value={ssid}
              onChange={(e) => {
                setSSID(e.target.value);
                setSSIDError(null);
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
              }}
              error={!!passwordError}
              helperText={passwordError || 'Minimum 8 characters for WPA/WPA2'}
              fullWidth
              disabled={connecting}
              slotProps={{
                input: {
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
                },
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
              label="Restart WiFi to apply changes"
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
