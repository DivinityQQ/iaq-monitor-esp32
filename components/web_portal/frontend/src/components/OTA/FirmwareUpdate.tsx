import { useState, useRef } from 'react';
import { useAtomValue, useSetAtom } from 'jotai';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Typography from '@mui/material/Typography';
import {
  CloudUpload as UploadIcon,
  RestartAlt as RebootIcon,
  Description as FileIcon,
} from '@mui/icons-material';
import { otaProgressAtom, refreshOTAInfoAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import { useNotification } from '../../contexts/SnackbarContext';
import { UpdateProgress } from './UpdateProgress';

/**
 * Firmware update card with file upload
 */
export function FirmwareUpdate() {
  const progress = useAtomValue(otaProgressAtom);
  const refreshInfo = useSetAtom(refreshOTAInfoAtom);
  const { showNotification } = useNotification();
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [isUploading, setIsUploading] = useState(false);
  const [uploadComplete, setUploadComplete] = useState(false);
  const [localProgress, setLocalProgress] = useState(0);

  const isOtaBusy = !!(progress && (progress.state === 'receiving' || progress.state === 'validating'));
  const isFirmwareUpdate = progress?.update_type === 'firmware';

  const handleFileSelect = (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (file) {
      // Validate file extension
      if (!file.name.endsWith('.bin')) {
        showNotification({ message: 'Please select a .bin firmware file', severity: 'warning' });
        return;
      }
      setSelectedFile(file);
      setUploadComplete(false);
    }
  };

  const handleUpload = async () => {
    if (!selectedFile) return;

    setIsUploading(true);
    setLocalProgress(0);
    setUploadComplete(false);

    try {
      const result = await apiClient.uploadFirmware(selectedFile, (pct) => {
        setLocalProgress(pct);
      });

      setUploadComplete(true);
      showNotification({ message: result.message, severity: 'success' });

      // Refresh OTA info after successful upload
      await refreshInfo();
    } catch (err) {
      showNotification({ message: err instanceof Error ? err.message : 'Upload failed', severity: 'error' });
    } finally {
      setIsUploading(false);
    }
  };

  const handleReboot = async () => {
    try {
      await apiClient.restartDevice();
      showNotification({ message: 'Rebooting device...', severity: 'info' });
    } catch (err) {
      showNotification({ message: err instanceof Error ? err.message : 'Reboot failed', severity: 'error' });
    }
  };

  const handleClear = () => {
    setSelectedFile(null);
    setUploadComplete(false);
    setLocalProgress(0);
    if (fileInputRef.current) {
      fileInputRef.current.value = '';
    }
  };

  // During upload, always show local progress (browser's upload progress).
  // Only show WebSocket progress when not actively uploading from this client.
  const showLocalProgress = isUploading;
  const showWsProgress = !isUploading && isFirmwareUpdate && isOtaBusy;

  return (
    <Card>
      <CardContent>
        <Typography variant="h6" gutterBottom>
          Firmware Update
        </Typography>
        <Typography variant="body2" color="text.secondary" sx={{ mb: 2 }}>
          Upload a new firmware binary (.bin) to update the device.
          The device will need to reboot after the update.
        </Typography>

        {/* File input (hidden) */}
        <input
          type="file"
          accept=".bin"
          ref={fileInputRef}
          onChange={handleFileSelect}
          style={{ display: 'none' }}
        />

        {/* Selected file display */}
        {selectedFile && !isUploading && (
          <Box
            sx={{
              display: 'flex',
              alignItems: 'center',
              gap: 1,
              p: 1.5,
              mb: 2,
              borderRadius: 1,
              bgcolor: 'action.hover',
            }}
          >
            <FileIcon color="action" />
            <Box sx={{ flex: 1, minWidth: 0 }}>
              <Typography variant="body2" noWrap>
                {selectedFile.name}
              </Typography>
              <Typography variant="caption" color="text.secondary">
                {(selectedFile.size / 1024 / 1024).toFixed(2)} MB
              </Typography>
            </Box>
            <Button size="small" onClick={handleClear}>
              Clear
            </Button>
          </Box>
        )}

        {/* Progress display */}
        {(showWsProgress && progress) && (
          <Box sx={{ mb: 2 }}>
            <UpdateProgress progress={progress} />
          </Box>
        )}

        {showLocalProgress && (
          <Box sx={{ mb: 2 }}>
            <UpdateProgress
              progress={{
                update_type: 'firmware',
                state: 'receiving',
                progress: localProgress,
                received: Math.round((localProgress / 100) * (selectedFile?.size || 0)),
                total: selectedFile?.size || 0,
              }}
            />
          </Box>
        )}

        {/* Action buttons */}
        <Box sx={{ display: 'flex', gap: 2, flexWrap: 'wrap' }}>
          {!selectedFile && !uploadComplete && (
            <Button
              variant="contained"
              startIcon={<UploadIcon />}
              onClick={() => fileInputRef.current?.click()}
              disabled={isOtaBusy}
            >
              Select Firmware
            </Button>
          )}

          {selectedFile && !uploadComplete && !isUploading && (
            <Button
              variant="contained"
              color="primary"
              startIcon={<UploadIcon />}
              onClick={handleUpload}
              disabled={isOtaBusy}
            >
              Upload Firmware
            </Button>
          )}

          {uploadComplete && (
            <>
              <Button
                variant="contained"
                color="success"
                startIcon={<RebootIcon />}
                onClick={handleReboot}
              >
                Reboot Now
              </Button>
              <Button variant="outlined" onClick={handleClear}>
                Upload Another
              </Button>
            </>
          )}
        </Box>
      </CardContent>
    </Card>
  );
}
