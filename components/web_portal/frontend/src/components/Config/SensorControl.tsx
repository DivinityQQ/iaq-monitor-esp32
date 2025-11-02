import { useState, useEffect, useMemo } from 'react';
import type { ReactElement } from 'react';
import {
  Box,
  Card,
  CardContent,
  Typography,
  Button,
  Chip,
  Badge,
  Slider,
  Alert,
  CircularProgress,
  Skeleton,
} from '@mui/material';
import Grid from '@mui/material/Grid';
import {
  Thermostat as ThermostatIcon,
  Cloud as CloudIcon,
  Co2 as Co2Icon,
  Grain as GrainIcon,
  Science as ScienceIcon,
  Memory as MemoryIcon,
  PlayArrow as ReadIcon,
  Refresh as ResetIcon,
  PowerSettingsNew as PowerIcon,
} from '@mui/icons-material';
import { useAtom, useAtomValue } from 'jotai';
import { healthAtom, cadencesAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import type { SensorId } from '../../api/types';
import { getSensorStateColor } from '../../theme';
import { formatRelativeTime, formatDuration } from '../../utils/validation';

// Sensor metadata mapping
const SENSOR_INFO: Record<SensorId, { name: string; icon: ReactElement; description: string }> = {
  mcu: {
    name: 'MCU Temperature',
    icon: <MemoryIcon />,
    description: 'ESP32 internal temperature sensor',
  },
  sht45: {
    name: 'SHT45',
    icon: <ThermostatIcon />,
    description: 'Temperature & Humidity sensor',
  },
  bmp280: {
    name: 'BMP280',
    icon: <CloudIcon />,
    description: 'Barometric Pressure sensor',
  },
  sgp41: {
    name: 'SGP41',
    icon: <ScienceIcon />,
    description: 'VOC & NOx Gas sensor',
  },
  pms5003: {
    name: 'PMS5003',
    icon: <GrainIcon />,
    description: 'Particulate Matter sensor',
  },
  s8: {
    name: 'S8',
    icon: <Co2Icon />,
    description: 'CO₂ sensor',
  },
};

// Cadence slider marks (in milliseconds)
const CADENCE_MARKS = [
  { value: 0, label: '0' },
  { value: 1000, label: '1s' },
  { value: 5000, label: '5s' },
  { value: 10000, label: '10s' },
  { value: 30000, label: '30s' },
  { value: 60000, label: '60s' },
];

interface SensorCardControlProps {
  sensorId: SensorId;
}

function SensorCardControl({ sensorId }: SensorCardControlProps) {
  const health = useAtomValue(healthAtom);
  const sensorStatus = health?.sensors[sensorId];

  // Cadence state
  const [cadences, setCadences] = useAtom(cadencesAtom);
  const [cadence, setCadence] = useState<number>(5000);
  const cadenceLoading = cadences == null;
  const [cadenceUpdating, setCadenceUpdating] = useState(false);

  // Action states
  const [reading, setReading] = useState(false);
  const [resetting, setResetting] = useState(false);
  const [toggling, setToggling] = useState(false);

  // Feedback
  const [actionError, setActionError] = useState<string | null>(null);
  const [actionSuccess, setActionSuccess] = useState<string | null>(null);

  const info = SENSOR_INFO[sensorId];

  // Sync local slider value from shared cadences atom
  useEffect(() => {
    const ms = cadences?.[sensorId]?.ms;
    if (typeof ms === 'number') {
      setCadence(ms);
    }
  }, [cadences, sensorId]);

  // Debounced cadence update
  const handleCadenceChange = useMemo(() => {
    let timeoutId: number;
    return (value: number) => {
      setCadence(value);
      clearTimeout(timeoutId);
      timeoutId = setTimeout(async () => {
        setCadenceUpdating(true);
        setActionError(null);
        const prevMs = cadences?.[sensorId]?.ms;
        try {
          await apiClient.setSensorCadence(sensorId, value);
          // Update shared cadences atom on success
          setCadences((prev) => {
            if (!prev) return prev;
            const prevEntry = prev[sensorId] || { ms: value, from_nvs: false };
            return { ...prev, [sensorId]: { ...prevEntry, ms: value } };
          });
          setActionSuccess(`Cadence updated to ${formatDuration(value)}`);
          setTimeout(() => setActionSuccess(null), 3000);
        } catch (error) {
          console.error(`Failed to update cadence for ${sensorId}:`, error);
          setActionError(error instanceof Error ? error.message : 'Failed to update cadence');
          // Revert local slider value if request failed
          if (typeof prevMs === 'number') {
            setCadence(prevMs);
          }
        } finally {
          setCadenceUpdating(false);
        }
      }, 500);
    };
  }, [sensorId, cadences, setCadences]);

  const handleRead = async () => {
    setReading(true);
    setActionError(null);
    setActionSuccess(null);
    try {
      await apiClient.readSensor(sensorId);
      setActionSuccess('Sensor read triggered successfully');
      setTimeout(() => setActionSuccess(null), 3000);
    } catch (error) {
      console.error(`Failed to read sensor ${sensorId}:`, error);
      setActionError(error instanceof Error ? error.message : 'Failed to read sensor');
    } finally {
      setReading(false);
    }
  };

  const handleReset = async () => {
    setResetting(true);
    setActionError(null);
    setActionSuccess(null);
    try {
      await apiClient.resetSensor(sensorId);
      setActionSuccess('Sensor reset successfully');
      setTimeout(() => setActionSuccess(null), 3000);
    } catch (error) {
      console.error(`Failed to reset sensor ${sensorId}:`, error);
      setActionError(error instanceof Error ? error.message : 'Failed to reset sensor');
    } finally {
      setResetting(false);
    }
  };

  const handleToggle = async () => {
    const isEnabled = sensorStatus?.state !== 'DISABLED';
    setToggling(true);
    setActionError(null);
    setActionSuccess(null);

    try {
      if (isEnabled) {
        await apiClient.disableSensor(sensorId);
        setActionSuccess('Sensor disabled successfully');
      } else {
        await apiClient.enableSensor(sensorId);
        setActionSuccess('Sensor enabled successfully');
      }
      setTimeout(() => setActionSuccess(null), 3000);
    } catch (error) {
      console.error(`Failed to toggle sensor ${sensorId}:`, error);
      setActionError(error instanceof Error ? error.message : 'Failed to toggle sensor');
    } finally {
      setToggling(false);
    }
  };

  if (!sensorStatus) {
    return (
      <Card sx={{ height: '100%' }}>
        <CardContent>
          {/* Header skeleton */}
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
            <Skeleton variant="circular" width={24} height={24} />
            <Box sx={{ flex: 1 }}>
              <Skeleton variant="text" width="40%" height={28} sx={{ mb: 0.5 }} />
              <Skeleton variant="text" width="60%" height={20} />
            </Box>
          </Box>

          {/* Status chip skeleton */}
          <Skeleton variant="rectangular" height={32} sx={{ mb: 2, borderRadius: 2 }} />

          {/* Status details skeleton */}
          <Box sx={{ mb: 2 }}>
            <Skeleton variant="text" width="70%" height={20} sx={{ mb: 0.5 }} />
            <Skeleton variant="text" width="50%" height={20} />
          </Box>

          {/* Action buttons skeleton */}
          <Box sx={{ display: 'flex', gap: 1, mb: 2 }}>
            <Skeleton variant="rectangular" height={32} sx={{ flex: 1, borderRadius: 1 }} />
            <Skeleton variant="rectangular" height={32} sx={{ flex: 1, borderRadius: 1 }} />
            <Skeleton variant="rectangular" height={32} sx={{ flex: 1, borderRadius: 1 }} />
          </Box>

          {/* Cadence slider skeleton */}
          <Box sx={{ mb: 1 }}>
            <Box sx={{ display: 'flex', justifyContent: 'space-between', mb: 1 }}>
              <Skeleton variant="text" width="35%" height={20} />
              <Skeleton variant="text" width="15%" height={20} />
            </Box>
            <Skeleton variant="rectangular" height={24} sx={{ borderRadius: 1 }} />
          </Box>
        </CardContent>
      </Card>
    );
  }

  const stateColor = getSensorStateColor(sensorStatus.state);
  const isEnabled = sensorStatus.state !== 'DISABLED';

  return (
    <Card
      sx={{
        height: '100%',
        transition: 'transform 0.2s, box-shadow 0.2s',
        '&:hover': {
          transform: 'translateY(-4px)',
          boxShadow: (theme) => theme.shadows[8],
        },
      }}
    >
      <CardContent>
        {/* Header */}
        <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 2 }}>
          <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
            <Box sx={{ color: stateColor, display: 'flex', alignItems: 'center' }}>
              {info.icon}
            </Box>
            <Box>
              <Typography variant="h6">{info.name}</Typography>
              <Typography variant="caption" color="text.secondary">
                {info.description}
              </Typography>
            </Box>
          </Box>
        </Box>

        {/* Status Chip */}
        <Box sx={{ mb: 2 }}>
          <Badge
            badgeContent={sensorStatus.errors > 0 ? sensorStatus.errors : undefined}
            color="error"
            sx={{ width: '100%' }}
          >
            <Chip
              label={sensorStatus.state}
              sx={{
                backgroundColor: stateColor,
                color: '#fff',
                fontWeight: 600,
                width: '100%',
              }}
            />
          </Badge>
        </Box>

        {/* Status Details */}
        <Box sx={{ mb: 2 }}>
          {sensorStatus.last_read_s !== undefined && (
            <Typography variant="body2" color="text.secondary">
              Last read: {formatRelativeTime(sensorStatus.last_read_s)}
            </Typography>
          )}
          {sensorStatus.warmup_remaining_s !== undefined && sensorStatus.warmup_remaining_s > 0 && (
            <Typography variant="body2" color="warning.main" fontWeight={500}>
              Warming up: {sensorStatus.warmup_remaining_s}s remaining
            </Typography>
          )}
          {sensorStatus.errors > 0 && (
            <Typography variant="body2" color="error.main">
              Error count: {sensorStatus.errors}
            </Typography>
          )}
        </Box>

        {/* Action Buttons */}
        <Box sx={{ display: 'flex', gap: 1, mb: 2, flexWrap: 'wrap' }}>
          <Button
            variant="outlined"
            size="small"
            startIcon={reading ? <CircularProgress size={14} /> : <ReadIcon />}
            onClick={handleRead}
            disabled={reading || !isEnabled}
            sx={{ flex: 1, minWidth: 80 }}
          >
            Read
          </Button>
          <Button
            variant="outlined"
            size="small"
            startIcon={resetting ? <CircularProgress size={14} /> : <ResetIcon />}
            onClick={handleReset}
            disabled={resetting || !isEnabled}
            sx={{ flex: 1, minWidth: 80 }}
          >
            Reset
          </Button>
          <Button
            variant={isEnabled ? 'outlined' : 'contained'}
            size="small"
            color={isEnabled ? 'error' : 'success'}
            startIcon={toggling ? <CircularProgress size={14} /> : <PowerIcon />}
            onClick={handleToggle}
            disabled={toggling}
            sx={{ flex: 1, minWidth: 80 }}
          >
            {isEnabled ? 'Disable' : 'Enable'}
          </Button>
        </Box>

        {/* Cadence Slider */}
        <Box sx={{ mb: 1 }}>
          <Box sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', mb: 1 }}>
            <Typography variant="body2" color="text.secondary">
              Read Cadence
            </Typography>
            <Typography variant="caption" fontWeight={500}>
              {cadenceUpdating && <CircularProgress size={12} sx={{ mr: 0.5 }} />}
              {formatDuration(cadence)}
            </Typography>
          </Box>
          <Slider
            value={cadence}
            onChange={(_, value) => handleCadenceChange(value as number)}
            min={0}
            max={60000}
            step={1000}
            marks={CADENCE_MARKS}
            disabled={cadenceLoading || !isEnabled}
            sx={{
              '& .MuiSlider-markLabel': {
                fontSize: '0.7rem',
              },
            }}
          />
        </Box>

        {/* Feedback Messages */}
        {actionSuccess && (
          <Alert severity="success" sx={{ mt: 2 }}>
            {actionSuccess}
          </Alert>
        )}
        {actionError && (
          <Alert severity="error" sx={{ mt: 2 }}>
            {actionError}
          </Alert>
        )}
      </CardContent>
    </Card>
  );
}

export function SensorControl() {
  const health = useAtomValue(healthAtom);
  const [cadences, setCadences] = useAtom(cadencesAtom);
  const [loadError, setLoadError] = useState<string | null>(null);

  // Fetch cadences once when opening the Sensors tab
  useEffect(() => {
    let cancelled = false;
    const load = async () => {
      if (cadences != null) return; // already loaded
      try {
        const response = await apiClient.getSensorCadences();
        if (!cancelled) setCadences(response.cadences);
      } catch (err) {
        console.error('Failed to load sensor cadences:', err);
        if (!cancelled) setLoadError(err instanceof Error ? err.message : 'Failed to load cadences');
      }
    };
    load();
    return () => {
      cancelled = true;
    };
  }, [cadences, setCadences]);

  if (!health) {
    return (
      <Box>
        <Grid container spacing={3}>
          {Array.from({ length: 6 }).map((_, i) => (
            <Grid key={i} size={{ xs: 12, md: 6, lg: 4 }}>
              <Card sx={{ height: '100%' }}>
                <CardContent>
                  {/* Header skeleton */}
                  <Box sx={{ display: 'flex', alignItems: 'center', gap: 1, mb: 2 }}>
                    <Skeleton variant="circular" width={24} height={24} />
                    <Box sx={{ flex: 1 }}>
                      <Skeleton variant="text" width="40%" height={28} sx={{ mb: 0.5 }} />
                      <Skeleton variant="text" width="60%" height={20} />
                    </Box>
                  </Box>

                  {/* Status chip skeleton */}
                  <Skeleton variant="rectangular" height={32} sx={{ mb: 2, borderRadius: 2 }} />

                  {/* Status details skeleton */}
                  <Box sx={{ mb: 2 }}>
                    <Skeleton variant="text" width="70%" height={20} sx={{ mb: 0.5 }} />
                    <Skeleton variant="text" width="50%" height={20} />
                  </Box>

                  {/* Action buttons skeleton */}
                  <Box sx={{ display: 'flex', gap: 1, mb: 2 }}>
                    <Skeleton variant="rectangular" height={32} sx={{ flex: 1, borderRadius: 1 }} />
                    <Skeleton variant="rectangular" height={32} sx={{ flex: 1, borderRadius: 1 }} />
                    <Skeleton variant="rectangular" height={32} sx={{ flex: 1, borderRadius: 1 }} />
                  </Box>

                  {/* Cadence slider skeleton */}
                  <Box sx={{ mb: 1 }}>
                    <Box sx={{ display: 'flex', justifyContent: 'space-between', mb: 1 }}>
                      <Skeleton variant="text" width="35%" height={20} />
                      <Skeleton variant="text" width="15%" height={20} />
                    </Box>
                    <Skeleton variant="rectangular" height={24} sx={{ borderRadius: 1 }} />
                  </Box>
                </CardContent>
              </Card>
            </Grid>
          ))}
        </Grid>
      </Box>
    );
  }

  const sensorIds: SensorId[] = ['mcu', 'sht45', 'bmp280', 'sgp41', 'pms5003', 's8'];

  return (
    <Box>
      <Box sx={{ mb: 4 }}>
        <Typography variant="h5" gutterBottom>
          Sensor Controls
        </Typography>
        <Typography variant="body2" color="text.secondary">
          Monitor and control individual sensors, adjust read cadences, and trigger manual operations
        </Typography>
      </Box>

      <Grid container spacing={3}>
        {sensorIds.map((sensorId) => (
          <Grid key={sensorId} size={{ xs: 12, md: 6, lg: 4 }}>
            <SensorCardControl sensorId={sensorId} />
          </Grid>
        ))}
      </Grid>
    </Box>
  );
}
