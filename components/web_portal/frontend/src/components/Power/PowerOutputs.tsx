import { useState } from 'react';
import { useAtomValue } from 'jotai';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import CircularProgress from '@mui/material/CircularProgress';
import Skeleton from '@mui/material/Skeleton';
import Switch from '@mui/material/Switch';
import Typography from '@mui/material/Typography';
import { powerAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import { useNotification } from '../../contexts/SnackbarContext';
import { ConfirmDialog } from '../Common/ConfirmDialog';
import { logger } from '../../utils/logger';

type OutputKey = 'en' | 'v3v_on' | 'vsqt_on' | 'stat_on';

interface OutputConfig {
  key: OutputKey;
  label: string;
  description: string;
  dangerous: boolean;
  warningMessage?: string;
}

const OUTPUT_CONFIGS: OutputConfig[] = [
  {
    key: 'en',
    label: 'EN (Enable)',
    description: 'Main power enable',
    dangerous: true,
    warningMessage: 'Disabling EN will turn off the main power rail. This may cause the device to shut down or lose functionality.',
  },
  {
    key: 'v3v_on',
    label: '3V3 Rail',
    description: '3.3V power rail',
    dangerous: true,
    warningMessage: 'Toggling the 3.3V rail may affect connected peripherals and sensors.',
  },
  {
    key: 'vsqt_on',
    label: 'VSQT',
    description: 'VSQT power output',
    dangerous: true,
    warningMessage: 'Toggling VSQT may affect connected peripherals.',
  },
  {
    key: 'stat_on',
    label: 'STAT LED',
    description: 'Status LED indicator',
    dangerous: false,
  },
];

/**
 * Power outputs card with toggles for power rails
 */
export function PowerOutputs() {
  const power = useAtomValue(powerAtom);
  const { showNotification } = useNotification();

  // Loading states for each output
  const [loading, setLoading] = useState<Record<OutputKey, boolean>>({
    en: false,
    v3v_on: false,
    vsqt_on: false,
    stat_on: false,
  });

  // Confirmation dialog state
  const [confirmDialog, setConfirmDialog] = useState<{
    open: boolean;
    config: OutputConfig | null;
    newValue: boolean;
  }>({ open: false, config: null, newValue: false });

  const handleToggle = async (key: OutputKey, newValue: boolean) => {
    const config = OUTPUT_CONFIGS.find((c) => c.key === key);
    if (!config) return;

    // If dangerous, show confirmation dialog
    if (config.dangerous) {
      setConfirmDialog({ open: true, config, newValue });
      return;
    }

    // Otherwise, toggle directly
    await performToggle(key, newValue);
  };

  const performToggle = async (key: OutputKey, newValue: boolean) => {
    setLoading((prev) => ({ ...prev, [key]: true }));

    try {
      await apiClient.setPowerOutputs({ [key]: newValue });
      showNotification({
        message: `${OUTPUT_CONFIGS.find((c) => c.key === key)?.label} ${newValue ? 'enabled' : 'disabled'}`,
        severity: 'success',
      });
    } catch (error) {
      logger.error(`Failed to toggle ${key}:`, error);
      showNotification({
        message: error instanceof Error ? error.message : `Failed to toggle ${key}`,
        severity: 'error',
      });
    } finally {
      setLoading((prev) => ({ ...prev, [key]: false }));
    }
  };

  const handleConfirm = async () => {
    if (!confirmDialog.config) return;
    setConfirmDialog((prev) => ({ ...prev, open: false }));
    await performToggle(confirmDialog.config.key, confirmDialog.newValue);
  };

  const handleCancel = () => {
    setConfirmDialog({ open: false, config: null, newValue: false });
  };

  if (!power) {
    return (
      <Card sx={{ height: '100%' }}>
        <CardContent>
          <Typography variant="h6" gutterBottom>
            Power Outputs
          </Typography>
          {OUTPUT_CONFIGS.map((_, i) => (
            <Box key={i} sx={{ py: 1 }}>
              <Skeleton variant="rectangular" height={40} sx={{ borderRadius: 1 }} />
            </Box>
          ))}
        </CardContent>
      </Card>
    );
  }

  return (
    <Card sx={{ height: '100%' }}>
      <CardContent>
        <Typography variant="h6" gutterBottom>
          Power Outputs
        </Typography>

        {OUTPUT_CONFIGS.map((config) => (
          <Box
            key={config.key}
            sx={{
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'space-between',
              py: 1,
              borderBottom: 1,
              borderColor: 'divider',
              '&:last-child': { borderBottom: 0 },
            }}
          >
            <Box>
              <Typography variant="body2" fontWeight={500}>
                {config.label}
              </Typography>
              <Typography variant="caption" color="text.secondary">
                {config.description}
              </Typography>
            </Box>
            <Box sx={{ display: 'flex', alignItems: 'center' }}>
              {loading[config.key] ? (
                <CircularProgress size={24} sx={{ mr: 1.5 }} />
              ) : (
                <Switch
                  checked={power[config.key]}
                  onChange={(e) => handleToggle(config.key, e.target.checked)}
                  color="primary"
                />
              )}
            </Box>
          </Box>
        ))}

        {/* Confirmation dialog */}
        <ConfirmDialog
          open={confirmDialog.open}
          title={`${confirmDialog.newValue ? 'Enable' : 'Disable'} ${confirmDialog.config?.label}`}
          message={confirmDialog.config?.warningMessage || 'Are you sure you want to change this setting?'}
          confirmLabel={confirmDialog.newValue ? 'Enable' : 'Disable'}
          cancelLabel="Cancel"
          severity="warning"
          onConfirm={handleConfirm}
          onCancel={handleCancel}
        />
      </CardContent>
    </Card>
  );
}
