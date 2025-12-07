import { useState } from 'react';
import { useAtomValue } from 'jotai';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import CircularProgress from '@mui/material/CircularProgress';
import InputAdornment from '@mui/material/InputAdornment';
import Skeleton from '@mui/material/Skeleton';
import Switch from '@mui/material/Switch';
import TextField from '@mui/material/TextField';
import Typography from '@mui/material/Typography';
import { powerAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import { useNotification } from '../../contexts/SnackbarContext';
import { logger } from '../../utils/logger';

/**
 * Charging configuration card with compact inline current/change settings
 */
export function ChargingConfig() {
  const power = useAtomValue(powerAtom);
  const { showNotification } = useNotification();

  // Local form state (not synced from server - user edits only)
  const [chargeLimit, setChargeLimit] = useState('');
  const [maintainVoltage, setMaintainVoltage] = useState('');

  // Loading states
  const [togglingCharging, setTogglingCharging] = useState(false);
  const [applying, setApplying] = useState(false);

  // Check if there's anything to apply
  const hasInput = chargeLimit.trim() !== '' || maintainVoltage.trim() !== '';

  const handleToggleCharging = async (enabled: boolean) => {
    setTogglingCharging(true);
    try {
      await apiClient.setCharger({ enable: enabled });
      showNotification({
        message: `Charging ${enabled ? 'enabled' : 'disabled'}`,
        severity: 'success',
      });
    } catch (error) {
      logger.error('Failed to toggle charging:', error);
      showNotification({
        message: error instanceof Error ? error.message : 'Failed to toggle charging',
        severity: 'error',
      });
    } finally {
      setTogglingCharging(false);
    }
  };

  const handleApply = async () => {
    const config: { limit_ma?: number; maintain_mv?: number } = {};

    // Only include values that were entered
    if (chargeLimit.trim() !== '') {
      const limitMa = parseInt(chargeLimit, 10);
      if (isNaN(limitMa) || limitMa < 0 || limitMa > 2000) {
        showNotification({
          message: 'Charge limit must be between 0 and 2000 mA',
          severity: 'error',
        });
        return;
      }
      config.limit_ma = limitMa;
    }

    if (maintainVoltage.trim() !== '') {
      const maintainMv = parseInt(maintainVoltage, 10);
      if (isNaN(maintainMv) || maintainMv < 0) {
        showNotification({
          message: 'Maintain voltage must be a positive number',
          severity: 'error',
        });
        return;
      }
      config.maintain_mv = maintainMv;
    }

    if (Object.keys(config).length === 0) {
      return;
    }

    setApplying(true);
    try {
      await apiClient.setCharger(config);
      // Clear inputs after successful apply
      setChargeLimit('');
      setMaintainVoltage('');
      showNotification({
        message: 'Charging configuration updated',
        severity: 'success',
      });
    } catch (error) {
      logger.error('Failed to apply charging config:', error);
      showNotification({
        message: error instanceof Error ? error.message : 'Failed to apply configuration',
        severity: 'error',
      });
    } finally {
      setApplying(false);
    }
  };

  if (!power) {
    return (
      <Card sx={{ height: '100%' }}>
        <CardContent>
          <Typography variant="h6" gutterBottom>
            Charging Configuration
          </Typography>
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
        <Typography variant="h6" gutterBottom>
          Charging Configuration
        </Typography>

        {/* Charging enable toggle */}
        <Box sx={{ mb: 2, display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <Box>
            <Typography variant="body2" fontWeight={500}>
              Enable Charging
            </Typography>
            <Typography variant="caption" color="text.secondary">
              Allow battery charging when supply is connected
            </Typography>
          </Box>
          {togglingCharging ? (
            <CircularProgress size={24} />
          ) : (
            <Switch
              checked={power.charging_on}
              onChange={(e) => handleToggleCharging(e.target.checked)}
              color="primary"
            />
          )}
        </Box>

        {/* Charge Limit - inline current value + input */}
        <Box sx={{ mb: 2 }}>
          <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 0.5 }}>
            <Typography variant="body2" fontWeight={500}>
              Charge Limit
            </Typography>
            <Typography variant="body2" color="text.secondary">
              Current: <strong>{power.charge_limit_ma} mA</strong>
            </Typography>
          </Box>
          <TextField
            fullWidth
            type="number"
            value={chargeLimit}
            onChange={(e) => setChargeLimit(e.target.value)}
            placeholder="Enter new value"
            InputProps={{
              endAdornment: <InputAdornment position="end">mA</InputAdornment>,
            }}
            inputProps={{
              min: 0,
              max: 2000,
              step: 50,
            }}
            size="small"
          />
          <Typography variant="caption" color="text.secondary">
            Maximum charging current (0-2000 mA)
          </Typography>
        </Box>

        {/* Maintain Voltage - inline current value + input */}
        <Box sx={{ mb: 2 }}>
          <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', mb: 0.5 }}>
            <Typography variant="body2" fontWeight={500}>
              Maintain Voltage (MPP)
            </Typography>
            <Typography variant="body2" color="text.secondary">
              Current: <strong>{power.maintain_mv} mV</strong>
            </Typography>
          </Box>
          <TextField
            fullWidth
            type="number"
            value={maintainVoltage}
            onChange={(e) => setMaintainVoltage(e.target.value)}
            placeholder="Enter new value"
            InputProps={{
              endAdornment: <InputAdornment position="end">mV</InputAdornment>,
            }}
            inputProps={{
              min: 0,
              step: 100,
            }}
            size="small"
          />
          <Typography variant="caption" color="text.secondary">
            Maximum Power Point tracking voltage
          </Typography>
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
