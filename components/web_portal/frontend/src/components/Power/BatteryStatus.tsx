import { useAtomValue } from 'jotai';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import CircularProgress from '@mui/material/CircularProgress';
import Grid from '@mui/material/Grid';
import LinearProgress from '@mui/material/LinearProgress';
import Skeleton from '@mui/material/Skeleton';
import Typography from '@mui/material/Typography';
import {
  Bolt as ChargingIcon,
  TrendingDown as DischargingIcon,
  Remove as IdleIcon,
  Thermostat as TempIcon,
  Loop as CyclesIcon,
  Schedule as TimeIcon,
  Favorite as HealthIcon,
  ElectricBolt as VoltageIcon,
  ElectricalServices as CurrentIcon,
} from '@mui/icons-material';
import { powerAtom } from '../../store/atoms';

/**
 * Get battery level color based on charge percentage
 */
const getBatteryColor = (pct: number): 'success' | 'warning' | 'error' => {
  if (pct >= 50) return 'success';
  if (pct >= 20) return 'warning';
  return 'error';
};

/**
 * Format time remaining in hours and minutes
 */
const formatTimeRemaining = (minutes: number): string => {
  if (minutes <= 0) return '--';
  const hours = Math.floor(minutes / 60);
  const mins = minutes % 60;
  if (hours > 0 && mins > 0) return `${hours}h ${mins}m`;
  if (hours > 0) return `${hours}h`;
  return `${mins}m`;
};

/**
 * Battery status card displaying charge level, health, and metrics
 * Horizontal layout: gauge on left, metrics on right (stacks on mobile)
 */
export function BatteryStatus() {
  const power = useAtomValue(powerAtom);

  if (!power) {
    return (
      <Card sx={{ height: '100%' }}>
        <CardContent>
          <Typography variant="h6" gutterBottom>
            Battery Status
          </Typography>

          {/* Horizontal skeleton layout */}
          <Box
            sx={{
              display: 'flex',
              flexDirection: { xs: 'column', sm: 'row' },
              gap: 3,
              alignItems: { xs: 'center', sm: 'flex-start' },
            }}
          >
            {/* Gauge skeleton */}
            <Box
              sx={{
                display: 'flex',
                flexDirection: 'column',
                alignItems: 'center',
                minWidth: { sm: 120 },
              }}
            >
              <Skeleton variant="circular" width={100} height={100} />
              <Skeleton variant="rounded" width={100} height={28} sx={{ mt: 2 }} />
            </Box>

            {/* Metrics skeleton */}
            <Box sx={{ flex: 1, width: '100%' }}>
              <Grid container spacing={2}>
                {[...Array(6)].map((_, i) => (
                  <Grid size={{ xs: 6 }} key={i}>
                    <Box display="flex" alignItems="center" gap={1}>
                      <Skeleton variant="circular" width={20} height={20} />
                      <Box>
                        <Skeleton variant="text" width={60} height={16} />
                        <Skeleton variant="text" width={80} height={20} />
                      </Box>
                    </Box>
                  </Grid>
                ))}
              </Grid>
            </Box>
          </Box>
        </CardContent>
      </Card>
    );
  }

  const batteryColor = getBatteryColor(power.charge_pct);
  const isCharging = power.charging_on && power.batt_ma > 0;
  const isDischarging = power.batt_ma < 0;

  return (
    <Card sx={{ height: '100%' }}>
      <CardContent>
        <Typography variant="h6" gutterBottom>
          Battery Status
        </Typography>

        {/* Horizontal layout: gauge left, metrics right */}
        <Box
          sx={{
            display: 'flex',
            flexDirection: { xs: 'column', sm: 'row' },
            gap: 3,
            alignItems: { xs: 'center', sm: 'flex-start' },
          }}
        >
          {/* Left: Gauge + Status chip */}
          <Box
            sx={{
              display: 'flex',
              flexDirection: 'column',
              alignItems: 'center',
              minWidth: { sm: 120 },
            }}
          >
            {/* Circular gauge with percentage */}
            <Box position="relative" display="inline-flex">
              <CircularProgress
                variant="determinate"
                value={power.charge_pct}
                size={100}
                thickness={4}
                color={batteryColor}
                sx={{
                  '& .MuiCircularProgress-circle': {
                    strokeLinecap: 'round',
                  },
                }}
              />
              {/* Background circle */}
              <CircularProgress
                variant="determinate"
                value={100}
                size={100}
                thickness={4}
                sx={{
                  position: 'absolute',
                  left: 0,
                  color: (theme) => theme.palette.action.disabledBackground,
                }}
              />
              {/* Center content */}
              <Box
                sx={{
                  top: 0,
                  left: 0,
                  bottom: 0,
                  right: 0,
                  position: 'absolute',
                  display: 'flex',
                  flexDirection: 'column',
                  alignItems: 'center',
                  justifyContent: 'center',
                }}
              >
                <Typography variant="h5" fontWeight={700} color={`${batteryColor}.main`}>
                  {Math.round(power.charge_pct)}%
                </Typography>
              </Box>
            </Box>

            {/* Charging status chip */}
            <Box sx={{ mt: 2 }}>
              {isCharging ? (
                <Chip
                  icon={<ChargingIcon />}
                  label="Charging"
                  color="success"
                  variant="filled"
                  size="small"
                />
              ) : isDischarging ? (
                <Chip
                  icon={<DischargingIcon />}
                  label="Discharging"
                  color="warning"
                  variant="filled"
                  size="small"
                />
              ) : (
                <Chip
                  icon={<IdleIcon />}
                  label="Not Charging"
                  variant="outlined"
                  size="small"
                />
              )}
            </Box>
          </Box>

          {/* Right: Metrics grid */}
          <Box sx={{ flex: 1, width: '100%' }}>
            <Grid container spacing={2}>
              {/* Voltage */}
              <Grid size={{ xs: 6 }}>
                <Box display="flex" alignItems="center" gap={1}>
                  <VoltageIcon fontSize="small" color="action" />
                  <Box>
                    <Typography variant="caption" color="text.secondary">
                      Voltage
                    </Typography>
                    <Typography variant="body2" fontWeight={500}>
                      {(power.batt_mv / 1000).toFixed(2)}V
                    </Typography>
                  </Box>
                </Box>
              </Grid>

              {/* Current */}
              <Grid size={{ xs: 6 }}>
                <Box display="flex" alignItems="center" gap={1}>
                  <CurrentIcon fontSize="small" color="action" />
                  <Box>
                    <Typography variant="caption" color="text.secondary">
                      Current
                    </Typography>
                    <Typography variant="body2" fontWeight={500}>
                      {power.batt_ma > 0 ? '+' : ''}{power.batt_ma}mA
                    </Typography>
                  </Box>
                </Box>
              </Grid>

              {/* Health */}
              <Grid size={{ xs: 6 }}>
                <Box display="flex" alignItems="center" gap={1}>
                  <HealthIcon fontSize="small" color="action" />
                  <Box flex={1}>
                    <Typography variant="caption" color="text.secondary">
                      Health
                    </Typography>
                    <Typography variant="body2" fontWeight={500}>
                      {power.health_pct}%
                    </Typography>
                    <LinearProgress
                      variant="determinate"
                      value={power.health_pct}
                      sx={{ mt: 0.5, height: 4, borderRadius: 2 }}
                      color={power.health_pct >= 80 ? 'success' : power.health_pct >= 50 ? 'warning' : 'error'}
                    />
                  </Box>
                </Box>
              </Grid>

              {/* Temperature */}
              <Grid size={{ xs: 6 }}>
                <Box display="flex" alignItems="center" gap={1}>
                  <TempIcon fontSize="small" color="action" />
                  <Box>
                    <Typography variant="caption" color="text.secondary">
                      Temperature
                    </Typography>
                    <Typography variant="body2" fontWeight={500}>
                      {power.batt_temp_c.toFixed(1)}°C
                    </Typography>
                  </Box>
                </Box>
              </Grid>

              {/* Cycles */}
              <Grid size={{ xs: 6 }}>
                <Box display="flex" alignItems="center" gap={1}>
                  <CyclesIcon fontSize="small" color="action" />
                  <Box>
                    <Typography variant="caption" color="text.secondary">
                      Cycles
                    </Typography>
                    <Typography variant="body2" fontWeight={500}>
                      {power.cycles}
                    </Typography>
                  </Box>
                </Box>
              </Grid>

              {/* Time Remaining */}
              <Grid size={{ xs: 6 }}>
                <Box display="flex" alignItems="center" gap={1}>
                  <TimeIcon fontSize="small" color="action" />
                  <Box>
                    <Typography variant="caption" color="text.secondary">
                      Time Left
                    </Typography>
                    <Typography variant="body2" fontWeight={500}>
                      {formatTimeRemaining(power.time_left_min)}
                    </Typography>
                  </Box>
                </Box>
              </Grid>
            </Grid>
          </Box>
        </Box>
      </CardContent>
    </Card>
  );
}
