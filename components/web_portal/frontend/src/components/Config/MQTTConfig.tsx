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
  Alert,
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
import { useAtomValue } from 'jotai';
import { deviceInfoAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import type { MQTTStatus } from '../../api/types';
import { validateMQTTUrl } from '../../utils/validation';

export function MQTTConfig() {
  const deviceInfo = useAtomValue(deviceInfoAtom);

  // MQTT status
  const [mqttStatus, setMqttStatus] = useState<MQTTStatus | null>(null);
  const [loadingStatus, setLoadingStatus] = useState(true);
  const [statusError, setStatusError] = useState<string | null>(null);

  // Form state
  const [brokerUrl, setBrokerUrl] = useState('');
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [showPassword, setShowPassword] = useState(false);
  const [restartAfterSave, setRestartAfterSave] = useState(true);

  // Form submission state
  const [saving, setSaving] = useState(false);
  const [saveError, setSaveError] = useState<string | null>(null);
  const [saveSuccess, setSaveSuccess] = useState(false);

  // Restart state
  const [restarting, setRestarting] = useState(false);
  const [restartError, setRestartError] = useState<string | null>(null);

  // Validation errors
  const [urlError, setUrlError] = useState<string | null>(null);

  const mqttConnected = deviceInfo?.network?.mqtt_connected || false;

  // Load MQTT status on mount
  useEffect(() => {
    loadMQTTStatus();
  }, []);

  const loadMQTTStatus = async () => {
    setLoadingStatus(true);
    setStatusError(null);

    try {
      const status = await apiClient.getMQTTStatus();
      setMqttStatus(status);

      // Pre-fill form with current configuration
      if (status.broker_url) {
        setBrokerUrl(status.broker_url);
      }
    } catch (error) {
      console.error('Failed to load MQTT status:', error);
      setStatusError(error instanceof Error ? error.message : 'Failed to load MQTT status');
    } finally {
      setLoadingStatus(false);
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
    setSaveError(null);
    setSaveSuccess(false);

    try {
      await apiClient.setMQTT({
        broker_url: brokerUrl,
        username: username || undefined,
        password: password || undefined,
        restart: restartAfterSave,
      });

      setSaveSuccess(true);
      setSaveError(null);

      // Clear password field after successful save
      setPassword('');

      // Reload status
      await loadMQTTStatus();

      if (restartAfterSave) {
        setSaveSuccess(false);
        // Show restarting message
        setTimeout(() => {
          setSaveError('Device is restarting MQTT. Connection will be re-established shortly.');
        }, 1000);
      }
    } catch (error) {
      console.error('Failed to save MQTT config:', error);
      setSaveError(error instanceof Error ? error.message : 'Failed to save MQTT configuration');
      setSaveSuccess(false);
    } finally {
      setSaving(false);
    }
  };

  const handleRestart = async () => {
    setRestarting(true);
    setRestartError(null);

    try {
      await apiClient.restartMQTT();
      setRestartError('MQTT restarted successfully. Reconnecting...');

      // Reload status after a delay
      setTimeout(() => {
        loadMQTTStatus();
      }, 2000);
    } catch (error) {
      console.error('MQTT restart failed:', error);
      setRestartError(error instanceof Error ? error.message : 'Failed to restart MQTT');
    } finally {
      setRestarting(false);
    }
  };

  if (loadingStatus) {
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

          {statusError && (
            <Alert severity="error" sx={{ mb: 2 }}>
              {statusError}
            </Alert>
          )}

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
              onClick={loadMQTTStatus}
            >
              Refresh Status
            </Button>
          </Box>

          {restartError && (
            <Alert
              severity={restartError.includes('success') ? 'info' : 'error'}
              sx={{ mt: 2 }}
            >
              {restartError}
            </Alert>
          )}
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

          {saveSuccess && (
            <Alert severity="success" sx={{ mb: 2 }}>
              MQTT configuration saved successfully!
              {restartAfterSave && ' MQTT is restarting...'}
            </Alert>
          )}

          {saveError && (
            <Alert severity="error" sx={{ mb: 2 }}>
              {saveError}
            </Alert>
          )}

          <Box sx={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <TextField
              label="Broker URL"
              value={brokerUrl}
              onChange={(e) => {
                setBrokerUrl(e.target.value);
                setUrlError(null);
                setSaveError(null);
                setSaveSuccess(false);
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
              onChange={(e) => {
                setUsername(e.target.value);
                setSaveError(null);
                setSaveSuccess(false);
              }}
              helperText="Optional - leave empty if not required"
              fullWidth
              disabled={saving}
            />

            <TextField
              label="Password"
              type={showPassword ? 'text' : 'password'}
              value={password}
              onChange={(e) => {
                setPassword(e.target.value);
                setSaveError(null);
                setSaveSuccess(false);
              }}
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
              label="Restart MQTT after saving"
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
