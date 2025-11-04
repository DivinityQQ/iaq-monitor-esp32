import Badge from '@mui/material/Badge';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import LinearProgress from '@mui/material/LinearProgress';
import Typography from '@mui/material/Typography';
import {
  Memory as MemoryIcon,
  Thermostat as ThermostatIcon,
  Speed as SpeedIcon,
  Air as AirIcon,
  CloudQueue as CloudQueueIcon,
  Co2 as Co2Icon,
} from '@mui/icons-material';
import { SensorId, SensorStatus as SensorStatusType } from '../../api/types';
import { formatRelativeTime, formatSeconds } from '../../utils/validation';

interface SensorStatusProps {
  sensorId: SensorId;
  status: SensorStatusType;
}

const sensorConfig = {
  mcu: {
    name: 'MCU',
    icon: MemoryIcon,
  },
  sht45: {
    name: 'SHT45',
    icon: ThermostatIcon,
  },
  bmp280: {
    name: 'BMP280',
    icon: SpeedIcon,
  },
  sgp41: {
    name: 'SGP41',
    icon: AirIcon,
  },
  pms5003: {
    name: 'PMS5003',
    icon: CloudQueueIcon,
  },
  s8: {
    name: 'S8 CO2',
    icon: Co2Icon,
  },
};

const stateColors = {
  READY: 'success',
  WARMING: 'warning',
  INIT: 'info',
  ERROR: 'error',
  DISABLED: 'default',
  UNINIT: 'default',
} as const;

/**
 * Individual sensor status card component
 */
export function SensorStatus({ sensorId, status }: SensorStatusProps) {
  const config = sensorConfig[sensorId];
  const Icon = config.icon;
  const stateColor = stateColors[status.state];

  // Calculate warmup progress percentage
  const warmupProgress =
    status.state === 'WARMING' && status.warmup_remaining_s != null
      ? Math.max(0, 100 - (status.warmup_remaining_s / 300) * 100) // Assume 5min max warmup
      : null;

  return (
    <Card
      sx={{
        height: '100%',
        minHeight: 200,
        transition: 'transform 0.2s, box-shadow 0.2s',
        '&:hover': {
          transform: 'translateY(-2px)',
          boxShadow: (theme) => theme.shadows[4],
        },
      }}
    >
      <CardContent>
        {/* Header with icon and name */}
        <Box display="flex" alignItems="center" justifyContent="space-between" mb={2}>
          <Box display="flex" alignItems="center" gap={1}>
            <Icon color="action" />
            <Typography variant="h6" fontSize="1rem">
              {config.name}
            </Typography>
          </Box>
          {status.errors > 0 && (
            <Badge badgeContent={status.errors} color="error" max={99}>
              <Box width={24} height={24} />
            </Badge>
          )}
        </Box>

        {/* State chip */}
        <Box mb={2}>
          <Chip
            label={status.state}
            color={stateColor}
            sx={{
              width: '100%',
              fontWeight: 600,
              fontSize: '0.9rem',
            }}
          />
        </Box>

        {/* Warmup progress bar */}
        {status.state === 'WARMING' && warmupProgress !== null && (
          <Box mb={2}>
            <Box display="flex" justifyContent="space-between" mb={0.5}>
              <Typography variant="caption" color="text.secondary">
                Warming up...
              </Typography>
              <Typography variant="caption" color="text.secondary">
                {formatSeconds(status.warmup_remaining_s || 0)} remaining
              </Typography>
            </Box>
            <LinearProgress
              variant="determinate"
              value={warmupProgress}
              color="warning"
              sx={{ height: 6, borderRadius: 3 }}
            />
          </Box>
        )}

        {/* Last successful read */}
        <Box>
          <Typography variant="caption" color="text.secondary" display="block">
            Last Read
          </Typography>
          <Typography variant="body2" fontWeight={500}>
            {status.last_read_s != null
              ? formatRelativeTime(status.last_read_s)
              : 'Never'}
          </Typography>
        </Box>

        {/* Error count */}
        {status.errors > 0 && (
          <Box mt={1}>
            <Typography variant="caption" color="error">
              {status.errors} error{status.errors > 1 ? 's' : ''} recorded
            </Typography>
          </Box>
        )}

        {/* State-specific messages */}
        {status.state === 'DISABLED' && (
          <Box mt={1}>
            <Typography variant="caption" color="text.secondary">
              Enable in Configuration tab
            </Typography>
          </Box>
        )}
        {status.state === 'ERROR' && (
          <Box mt={1}>
            <Typography variant="caption" color="error">
              Sensor communication failed
            </Typography>
          </Box>
        )}
      </CardContent>
    </Card>
  );
}
