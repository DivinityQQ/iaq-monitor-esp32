import { useState, useRef } from 'react';
import { useAtomValue, useSetAtom } from 'jotai';
import Box from '@mui/material/Box';
import Button from '@mui/material/Button';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Typography from '@mui/material/Typography';
import {
  CloudUpload as UploadIcon,
  Refresh as RefreshIcon,
  Description as FileIcon,
} from '@mui/icons-material';
import { otaProgressAtom, refreshOTAInfoAtom } from '../../store/atoms';
import { apiClient } from '../../api/client';
import { useNotification } from '../../contexts/SnackbarContext';
import { UpdateProgress } from './UpdateProgress';

/**
 * Frontend update card with file upload
 */
export function FrontendUpdate() {
  const progress = useAtomValue(otaProgressAtom);
  const refreshInfo = useSetAtom(refreshOTAInfoAtom);
  const { showNotification } = useNotification();
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [isUploading, setIsUploading] = useState(false);
  const [uploadComplete, setUploadComplete] = useState(false);
  const [localProgress, setLocalProgress] = useState(0);

  const isOtaBusy = !!(progress && (progress.state === 'receiving' || progress.state === 'validating'));
  const isFrontendUpdate = progress?.update_type === 'frontend';

  const handleFileSelect = (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (file) {
      // Validate file extension
      if (!file.name.endsWith('.bin')) {
        showNotification({ message: 'Please select a .bin LittleFS image file', severity: 'warning' });
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
      const result = await apiClient.uploadFrontend(selectedFile, (pct) => {
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

  const handleRefresh = () => {
    window.location.reload();
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
  const showWsProgress = !isUploading && isFrontendUpdate && isOtaBusy;

  return (
    <Card>
      <CardContent>
        <Typography variant="h6" gutterBottom>
          Frontend Update
        </Typography>
        <Typography variant="body2" color="text.secondary" sx={{ mb: 2 }}>
          Upload a new frontend LittleFS image (www.bin) to update the web interface.
          No reboot required - just refresh the page.
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
                update_type: 'frontend',
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
              Select Frontend Image
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
              Upload Frontend
            </Button>
          )}

          {uploadComplete && (
            <>
              <Button
                variant="contained"
                color="success"
                startIcon={<RefreshIcon />}
                onClick={handleRefresh}
              >
                Refresh Page
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
