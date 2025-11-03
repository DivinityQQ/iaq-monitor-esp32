import { useState, useEffect } from 'react';
import {
  Box,
  Card,
  CardContent,
  Typography,
  Button,
  TextField,
  Switch,
  FormControlLabel,
  Chip,
  IconButton,
  InputAdornment,
  CircularProgress,
  Skeleton,
} from '@mui/material';
import {
  CloudQueue as MQTTIcon,
  CloudOff as MQTTOffIcon,
  Refresh as RefreshIcon,
  Visibility as VisibilityIcon,
  VisibilityOff as VisibilityOffIcon,
} from '@mui/icons-material';
import { useAtomValue, useSetAtom } from 'jotai';
import { deviceInfoAtom, mqttStatusAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import { validateMQTTUrl } from '../../utils/validation';
import { useNotification } from '../../contexts/SnackbarContext';

export function MQTTConfig() {
  const deviceInfo = useAtomValue(deviceInfoAtom);
  const mqttStatus = useAtomValue(mqttStatusAtom);
  const setMqttStatus = useSetAtom(mqttStatusAtom);
  const { showNotification } = useNotification();

  // Form state
  const [brokerUrl, setBrokerUrl] = useState('');
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [showPassword, setShowPassword] = useState(false);
  const [restartAfterSave, setRestartAfterSave] = useState(true);

  // Form submission state
  const [saving, setSaving] = useState(false);

  // Restart state
  const [restarting, setRestarting] = useState(false);

  // Validation errors
  const [urlError, setUrlError] = useState<string | null>(null);

  const mqttConnected = deviceInfo?.network?.mqtt_connected || false;

  // Pre-fill form with current MQTT configuration
  useEffect(() => {
    if (mqttStatus?.broker_url) {
      setBrokerUrl(mqttStatus.broker_url);
    }
  }, [mqttStatus]);

  const refreshStatus = async () => {
    try {
      const status = await apiClient.getMQTTStatus();
      setMqttStatus(status);
    } catch (error) {
      console.error('Failed to refresh MQTT status:', error);
    }
  };

  const handleSave = async () => {
    // Validate form
    const urlValidation = validateMQTTUrl(brokerUrl);
    setUrlError(urlValidation);

    if (urlValidation) {
      return;
    }

    setSaving(true);

    try {
      await apiClient.setMQTT({
        broker_url: brokerUrl,
        username: username || undefined,
        password: password || undefined,
        restart: restartAfterSave,
      });

      // Clear password field after successful save
      setPassword('');

      // Refresh status
      await refreshStatus();

      if (restartAfterSave) {
        // Show restarting message after a delay
        setTimeout(() => {
          showNotification({
            message: 'MQTT is restarting. Connection will be re-established shortly.',
            severity: 'info',
            duration: 5000,
          });
        }, 1000);
      } else {
        showNotification({
          message: 'MQTT configuration updated successfully!',
          severity: 'success',
        });
      }
    } catch (error) {
      console.error('Failed to save MQTT config:', error);
      showNotification({
        message: error instanceof Error ? error.message : 'Failed to save MQTT configuration',
        severity: 'error',
      });
    } finally {
      setSaving(false);
    }
  };

  const handleRestart = async () => {
    setRestarting(true);

    try {
      await apiClient.restartMQTT();
      showNotification({
        message: 'MQTT restarted successfully. Reconnecting...',
        severity: 'info',
        duration: 4000,
      });

      // Refresh status after a delay
      setTimeout(() => {
        refreshStatus();
      }, 2000);
    } catch (error) {
      console.error('MQTT restart failed:', error);
      showNotification({
        message: error instanceof Error ? error.message : 'Failed to restart MQTT',
        severity: 'error',
      });
    } finally {
      setRestarting(false);
    }
  };

  // Show loading skeleton only during initial app load
  if (!deviceInfo || !mqttStatus) {
    return (
      <Box>
        <Skeleton variant="rectangular" height={200} sx={{ borderRadius: 3, mb: 3 }} />
        <Skeleton variant="rectangular" height={400} sx={{ borderRadius: 3 }} />
      </Box>
    );
  }

  return (
    <Box>
      {/* Current MQTT Status Card */}
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
              {mqttConnected ? (
                <MQTTIcon sx={{ color: 'success.main' }} />
              ) : (
                <MQTTOffIcon sx={{ color: 'error.main' }} />
              )}
              <Typography variant="h6">MQTT Status</Typography>
            </Box>
            <Chip
              label={mqttConnected ? 'Connected' : 'Disconnected'}
              color={mqttConnected ? 'success' : 'error'}
              size="small"
            />
          </Box>

          {mqttStatus && (
            <>
              <Box sx={{ mb: 2 }}>
                <Typography variant="body2" color="text.secondary" gutterBottom>
                  Broker URL
                </Typography>
                <Typography variant="body1">
                  {mqttStatus.broker_url || 'Not configured'}
                </Typography>
              </Box>

              <Box sx={{ mb: 2 }}>
                <Typography variant="body2" color="text.secondary" gutterBottom>
                  Configuration Status
                </Typography>
                <Chip
                  label={mqttStatus.configured ? 'Configured' : 'Not Configured'}
                  color={mqttStatus.configured ? 'success' : 'warning'}
                  size="small"
                  variant="outlined"
                />
              </Box>
            </>
          )}

          <Box sx={{ display: 'flex', gap: 2 }}>
            <Button
              variant="outlined"
              startIcon={restarting ? <CircularProgress size={16} /> : <RefreshIcon />}
              onClick={handleRestart}
              disabled={restarting || !mqttStatus?.configured}
            >
              Restart MQTT
            </Button>
            <Button
              variant="outlined"
              startIcon={<RefreshIcon />}
              onClick={refreshStatus}
            >
              Refresh Status
            </Button>
          </Box>
        </CardContent>
      </Card>

      {/* MQTT Configuration Form Card */}
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
            MQTT Configuration
          </Typography>

          <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <TextField
              label="Broker URL"
              value={brokerUrl}
              onChange={(e) => {
                setBrokerUrl(e.target.value);
                setUrlError(null);
              }}
              error={!!urlError}
              helperText={urlError || 'Example: mqtt://broker.hivemq.com:1883 or mqtts://broker.hivemq.com:8883'}
              placeholder="mqtt://broker.example.com:1883"
              fullWidth
              disabled={saving}
              required
            />

            <TextField
              label="Username"
              value={username}
              onChange={(e) => setUsername(e.target.value)}
              helperText="Optional - leave empty if not required"
              fullWidth
              disabled={saving}
            />

            <TextField
              label="Password"
              type={showPassword ? 'text' : 'password'}
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              helperText="Optional - leave empty if not required"
              fullWidth
              disabled={saving}
              InputProps={{
                endAdornment: (
                  <InputAdornment position="end">
                    <IconButton
                      onClick={() => setShowPassword(!showPassword)}
                      edge="end"
                      disabled={saving}
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
                  checked={restartAfterSave}
                  onChange={(e) => setRestartAfterSave(e.target.checked)}
                  disabled={saving}
                />
              }
              label="Restart MQTT to apply changes"
            />

            <Button
              variant="contained"
              size="large"
              onClick={handleSave}
              disabled={saving || !brokerUrl}
              startIcon={saving ? <CircularProgress size={16} /> : undefined}
            >
              {saving ? 'Saving...' : 'Save Configuration'}
            </Button>
          </Box>
        </CardContent>
      </Card>
    </Box>
  );
}
