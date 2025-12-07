import { useState } from 'react';
import { useAtomValue } from 'jotai';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import CircularProgress from '@mui/material/CircularProgress';
import InputAdornment from '@mui/material/InputAdornment';
import Skeleton from '@mui/material/Skeleton';
import TextField from '@mui/material/TextField';
import Typography from '@mui/material/Typography';
import { NotificationsActive as AlarmIcon } from '@mui/icons-material';
import { powerAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import { useNotification } from '../../contexts/SnackbarContext';
import { logger } from '../../utils/logger';

/**
 * Battery alarms configuration card with compact inline current/change settings
 */
export function BatteryAlarms() {
  const power = useAtomValue(powerAtom);
  const { showNotification } = useNotification();

  // Local form state (not synced from server - user edits only)
  const [lowVoltage, setLowVoltage] = useState('');
  const [highVoltage, setHighVoltage] = useState('');
  const [lowPercent, setLowPercent] = useState('');

  // Loading state
  const [applying, setApplying] = useState(false);

  // Check if there's anything to apply
  const hasInput = lowVoltage.trim() !== '' || highVoltage.trim() !== '' || lowPercent.trim() !== '';

  const handleApply = async () => {
    const config: { low_v_mv?: number; high_v_mv?: number; low_pct?: number } = {};

    // Only include values that were entered
    if (lowVoltage.trim() !== '') {
      const lowV = parseInt(lowVoltage, 10);
      if (isNaN(lowV) || lowV < 0) {
        showNotification({
          message: 'Low voltage must be 0 or a positive number',
          severity: 'error',
        });
        return;
      }
      config.low_v_mv = lowV;
    }

    if (highVoltage.trim() !== '') {
      const highV = parseInt(highVoltage, 10);
      if (isNaN(highV) || highV < 0) {
        showNotification({
          message: 'High voltage must be 0 or a positive number',
          severity: 'error',
        });
        return;
      }
      config.high_v_mv = highV;
    }

    if (lowPercent.trim() !== '') {
      const lowPct = parseInt(lowPercent, 10);
      if (isNaN(lowPct) || lowPct < 0 || lowPct > 100) {
        showNotification({
          message: 'Low percentage must be between 0 and 100',
          severity: 'error',
        });
        return;
      }
      config.low_pct = lowPct;
    }

    if (Object.keys(config).length === 0) {
      return;
    }

    setApplying(true);
    try {
      await apiClient.setPowerAlarms(config);
      // Clear inputs after successful apply
      setLowVoltage('');
      setHighVoltage('');
      setLowPercent('');
      showNotification({
        message: 'Battery alarms updated',
        severity: 'success',
      });
    } catch (error) {
      logger.error('Failed to apply battery alarms:', error);
      showNotification({
        message: error instanceof Error ? error.message : 'Failed to apply alarms',
        severity: 'error',
      });
    } finally {
      setApplying(false);
    }
  };

  // Format alarm value for display
  const formatAlarmValue = (value: number, unit: string) => {
    return value === 0 ? 'Disabled' : `${value} ${unit}`;
  };

  if (!power) {
    return (
      <Card sx={{ height: '100%' }}>
        <CardContent>
          <Box display="flex" alignItems="center" gap={1} sx={{ mb: 2 }}>
            <AlarmIcon color="action" />
            <Typography variant="h6">Battery Alarms</Typography>
          </Box>
          <Box sx={{ py: 1 }}>
            <Skeleton variant="rectangular" height={40} sx={{ mb: 2, borderRadius: 1 }} />
            <Skeleton variant="rectangular" height={40} sx={{ mb: 2, borderRadius: 1 }} />
            <Skeleton variant="rectangular" height={40} sx={{ mb: 2, borderRadius: 1 }} />
          </Box>
        </CardContent>
      </Card>
    );
  }

  return (
    <Card sx={{ height: '100%' }}>
      <CardContent>
        <Box display="flex" alignItems="center" gap={1} sx={{ mb: 1 }}>
          <AlarmIcon color="action" />
          <Typography variant="h6">Battery Alarms</Typography>
        </Box>
        <Typography variant="caption" color="text.secondary" sx={{ display: 'block', mb: 2 }}>
          Set to 0 to disable an alarm
        </Typography>

        {/* Low Voltage - inline current value + input */}
        <Box sx={{ mb: 2 }}>
          <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 0.5 }}>
            <Typography variant="body2" fontWeight={500}>
              Low Voltage Alarm
            </Typography>
            <Typography variant="body2" color="text.secondary">
              Current: <strong>{formatAlarmValue(power.alarm_low_v_mv, 'mV')}</strong>
            </Typography>
          </Box>
          <TextField
            fullWidth
            type="number"
            value={lowVoltage}
            onChange={(e) => setLowVoltage(e.target.value)}
            placeholder="Enter new value"
            slotProps={{
              input: {
                endAdornment: <InputAdornment position="end">mV</InputAdornment>,
              },
              htmlInput: {
                min: 0,
                step: 100,
              },
            }}
            size="small"
          />
        </Box>

        {/* High Voltage - inline current value + input */}
        <Box sx={{ mb: 2 }}>
          <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 0.5 }}>
            <Typography variant="body2" fontWeight={500}>
              High Voltage Alarm
            </Typography>
            <Typography variant="body2" color="text.secondary">
              Current: <strong>{formatAlarmValue(power.alarm_high_v_mv, 'mV')}</strong>
            </Typography>
          </Box>
          <TextField
            fullWidth
            type="number"
            value={highVoltage}
            onChange={(e) => setHighVoltage(e.target.value)}
            placeholder="Enter new value"
            slotProps={{
              input: {
                endAdornment: <InputAdornment position="end">mV</InputAdornment>,
              },
              htmlInput: {
                min: 0,
                step: 100,
              },
            }}
            size="small"
          />
        </Box>

        {/* Low Percentage - inline current value + input */}
        <Box sx={{ mb: 2 }}>
          <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 0.5 }}>
            <Typography variant="body2" fontWeight={500}>
              Low Percentage Alarm
            </Typography>
            <Typography variant="body2" color="text.secondary">
              Current: <strong>{formatAlarmValue(power.alarm_low_pct, '%')}</strong>
            </Typography>
          </Box>
          <TextField
            fullWidth
            type="number"
            value={lowPercent}
            onChange={(e) => setLowPercent(e.target.value)}
            placeholder="Enter new value"
            slotProps={{
              input: {
                endAdornment: <InputAdornment position="end">%</InputAdornment>,
              },
              htmlInput: {
                min: 0,
                max: 100,
                step: 5,
              },
            }}
            size="small"
          />
        </Box>

        {/* Apply button */}
        <Button
          variant="contained"
          onClick={handleApply}
          disabled={applying || !hasInput}
          size="small"
        >
          {applying ? <CircularProgress size={16} color="inherit" sx={{ mr: 1 }} /> : null}
          Apply
        </Button>
      </CardContent>
    </Card>
  );
}
