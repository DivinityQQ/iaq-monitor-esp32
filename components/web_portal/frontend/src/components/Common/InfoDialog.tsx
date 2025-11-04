import Button from '@mui/material/Button';
import Dialog from '@mui/material/Dialog';
import DialogActions from '@mui/material/DialogActions';
import DialogContent from '@mui/material/DialogContent';
import DialogTitle from '@mui/material/DialogTitle';
import Divider from '@mui/material/Divider';
import Stack from '@mui/material/Stack';
import { memo } from 'react';
import { useAtomValue } from 'jotai';
import { deviceInfoAtom } from '../../store/atoms';
import { InfoRow, InfoSection } from './InfoPrimitives';

export interface InfoDialogProps {
  open: boolean;
  onClose: () => void;
}

/**
 * Device information dialog showing firmware, hardware, and device metadata
 */
function InfoDialogComponent({ open, onClose }: InfoDialogProps) {
  const deviceInfo = useAtomValue(deviceInfoAtom);

  if (!deviceInfo) {
    return null;
  }

  return (
    <Dialog open={open} onClose={onClose} maxWidth="sm" fullWidth>
      <DialogTitle>About {deviceInfo.device.name}</DialogTitle>
      <DialogContent>
        <Stack spacing={2} sx={{ mt: 1 }}>
          {/* Device Information */}
          <InfoSection title="Device">
            <InfoRow label="Model" value={deviceInfo.device.model} />
            <InfoRow label="Manufacturer" value={deviceInfo.device.manufacturer} />
            <InfoRow label="Device ID" value={deviceInfo.device_id} />
          </InfoSection>

          <Divider />

          {/* Firmware Information */}
          <InfoSection title="Firmware">
            <InfoRow label="Version" value={`v${deviceInfo.firmware.version}`} />
            <InfoRow label="Build Date" value={`${deviceInfo.firmware.build_date} ${deviceInfo.firmware.build_time}`} />
            <InfoRow label="ESP-IDF" value={deviceInfo.firmware.idf_version} />
            <InfoRow label="License" value={deviceInfo.firmware.license} />
          </InfoSection>

          <Divider />

          {/* Hardware Information */}
          <InfoSection title="Hardware">
            <InfoRow label="Chip" value={deviceInfo.hardware.chip} />
            <InfoRow label="CPU Cores" value={deviceInfo.hardware.cores} />
            <InfoRow label="Chip Revision" value={deviceInfo.hardware.revision} />
          </InfoSection>
        </Stack>
      </DialogContent>
      <DialogActions sx={{ px: 3, pb: 2 }}>
        <Button onClick={onClose} variant="contained" autoFocus>
          Close
        </Button>
      </DialogActions>
    </Dialog>
  );
}
export const InfoDialog = memo(InfoDialogComponent);
