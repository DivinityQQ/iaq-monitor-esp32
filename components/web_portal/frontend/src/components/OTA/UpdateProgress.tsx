import Alert from '@mui/material/Alert';
import Box from '@mui/material/Box';
import LinearProgress from '@mui/material/LinearProgress';
import Typography from '@mui/material/Typography';
import type { OTAProgress } from '../../api/types';

interface UpdateProgressProps {
  progress: OTAProgress;
}

/**
 * Format bytes to human-readable string
 */
const formatBytes = (bytes: number): string => {
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return `${(bytes / Math.pow(k, i)).toFixed(1)} ${sizes[i]}`;
};

/**
 * Get state display text
 */
const getStateText = (state: OTAProgress['state']): string => {
  switch (state) {
    case 'receiving':
      return 'Uploading...';
    case 'validating':
      return 'Validating...';
    case 'complete':
      return 'Complete';
    case 'error':
      return 'Error';
    default:
      return 'Idle';
  }
};

/**
 * Progress display component for OTA updates
 */
export function UpdateProgress({ progress }: UpdateProgressProps) {
  const isActive = progress.state === 'receiving' || progress.state === 'validating';
  const isComplete = progress.state === 'complete';
  const isError = progress.state === 'error';

  if (progress.state === 'idle') {
    return null;
  }

  return (
    <Box sx={{ width: '100%' }}>
      {isError && progress.error && (
        <Alert severity="error" sx={{ mb: 2 }}>
          {progress.error}
        </Alert>
      )}

      {isComplete && (
        <Alert severity="success" sx={{ mb: 2 }}>
          {progress.update_type === 'firmware'
            ? 'Firmware update complete. Reboot to activate the new firmware.'
            : 'Frontend update complete. Refresh the page to see changes.'}
        </Alert>
      )}

      {(isActive || isComplete) && (
        <Box>
          <Box sx={{ display: 'flex', justifyContent: 'space-between', mb: 1 }}>
            <Typography variant="body2" color="text.secondary">
              {getStateText(progress.state)} {progress.state !== 'validating' && `${progress.progress}%`}
            </Typography>
            <Typography variant="body2" color="text.secondary">
              {progress.total > 0
                ? `${formatBytes(progress.received)} / ${formatBytes(progress.total)}`
                : formatBytes(progress.received)}
            </Typography>
          </Box>

          <LinearProgress
            variant={progress.state === 'validating' ? 'indeterminate' : 'determinate'}
            value={progress.progress}
            color={isComplete ? 'success' : isError ? 'error' : 'primary'}
            sx={{ height: 8, borderRadius: 1 }}
          />
        </Box>
      )}
    </Box>
  );
}
