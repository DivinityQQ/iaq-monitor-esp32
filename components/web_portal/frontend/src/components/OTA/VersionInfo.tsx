import { useState } from 'react';
import { useAtomValue } from 'jotai';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import Grid from '@mui/material/Grid';
import Skeleton from '@mui/material/Skeleton';
import Typography from '@mui/material/Typography';
import {
  Memory as ChipIcon,
  Restore as RollbackIcon,
  CheckCircle as ValidIcon,
  Warning as PendingIcon,
  Info as InfoIcon,
} from '@mui/icons-material';
import { otaVersionInfoAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import { useNotification } from '../../contexts/SnackbarContext';
import { ConfirmDialog } from '../Common/ConfirmDialog';

/**
 * Version info card displaying firmware version and OTA status
 */
export function VersionInfo() {
  const info = useAtomValue(otaVersionInfoAtom);
  const { showNotification } = useNotification();
  const [rollbackDialogOpen, setRollbackDialogOpen] = useState(false);
  const [isRollingBack, setIsRollingBack] = useState(false);

  const handleRollback = async () => {
    setIsRollingBack(true);
    try {
      await apiClient.rollbackFirmware();
      showNotification({ message: 'Rolling back to previous firmware...', severity: 'info' });
      // Device will reboot, connection will be lost
    } catch (err) {
      showNotification({ message: err instanceof Error ? err.message : 'Rollback failed', severity: 'error' });
    } finally {
      setIsRollingBack(false);
      setRollbackDialogOpen(false);
    }
  };

  if (!info) {
    return (
      <Card>
        <CardContent>
          <Typography variant="h6" gutterBottom>
            Version Information
          </Typography>
          <Grid container spacing={2}>
            {[...Array(4)].map((_, i) => (
              <Grid size={{ xs: 6 }} key={i}>
                <Skeleton variant="text" width={80} height={16} />
                <Skeleton variant="text" width={120} height={24} />
              </Grid>
            ))}
          </Grid>
        </CardContent>
      </Card>
    );
  }

  return (
    <>
      <Card>
        <CardContent>
          <Box sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', mb: 2 }}>
            <Typography variant="h6">Version Information</Typography>
            {info.ota.pending_verify && (
              <Chip
                icon={<PendingIcon />}
                label="Pending Verification"
                color="warning"
                size="small"
              />
            )}
          </Box>

          <Grid container spacing={2}>
            {/* Firmware Version */}
            <Grid size={{ xs: 6, sm: 3 }}>
              <Typography variant="caption" color="text.secondary">
                Firmware
              </Typography>
              <Typography variant="body1" fontWeight={500}>
                v{info.firmware.version}
              </Typography>
            </Grid>

            {/* Build Date */}
            <Grid size={{ xs: 6, sm: 3 }}>
              <Typography variant="caption" color="text.secondary">
                Build Date
              </Typography>
              <Typography variant="body1" fontWeight={500}>
                {info.firmware.build_date} {info.firmware.build_time}
              </Typography>
            </Grid>

            {/* IDF Version */}
            <Grid size={{ xs: 6, sm: 3 }}>
              <Typography variant="caption" color="text.secondary">
                ESP-IDF
              </Typography>
              <Typography variant="body1" fontWeight={500}>
                {info.firmware.idf_version}
              </Typography>
            </Grid>

            {/* Active Slot */}
            <Grid size={{ xs: 6, sm: 3 }}>
              <Box sx={{ display: 'flex', alignItems: 'center', gap: 0.5 }}>
                <ChipIcon fontSize="small" color="action" />
                <Typography variant="caption" color="text.secondary">
                  OTA Slot
                </Typography>
              </Box>
              <Chip
                label={`ota_${info.ota.active_slot}`}
                size="small"
                color="primary"
                variant="outlined"
                sx={{ mt: 0.5 }}
              />
            </Grid>

            {/* Frontend Version */}
            <Grid size={{ xs: 6, sm: 3 }}>
              <Typography variant="caption" color="text.secondary">
                Frontend
              </Typography>
              <Typography variant="body1" fontWeight={500}>
                {info.frontend.version || '-'}
              </Typography>
            </Grid>
          </Grid>

          {/* Rollback Section */}
          <Box sx={{ mt: 3, pt: 2, borderTop: 1, borderColor: 'divider' }}>
            <Box sx={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
              <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
                {info.ota.rollback_available ? (
                  <ValidIcon fontSize="small" color="success" />
                ) : (
                  <InfoIcon fontSize="small" color="disabled" />
                )}
                <Typography variant="body2" color="text.secondary">
                  {info.ota.rollback_available
                    ? 'Previous firmware available for rollback'
                    : 'No rollback available'}
                </Typography>
              </Box>

              <Button
                variant="outlined"
                color="warning"
                size="small"
                startIcon={<RollbackIcon />}
                disabled={!info.ota.rollback_available || isRollingBack}
                onClick={() => setRollbackDialogOpen(true)}
              >
                Rollback
              </Button>
            </Box>
          </Box>
        </CardContent>
      </Card>

      <ConfirmDialog
        open={rollbackDialogOpen}
        title="Rollback Firmware"
        message="This will revert to the previous firmware version. The device will reboot and you will lose connection temporarily."
        confirmLabel="Rollback"
        severity="warning"
        onConfirm={handleRollback}
        onCancel={() => setRollbackDialogOpen(false)}
        loading={isRollingBack}
      />
    </>
  );
}
