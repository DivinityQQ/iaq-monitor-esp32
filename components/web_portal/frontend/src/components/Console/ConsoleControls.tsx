import { useCallback, memo } from 'react';
import Box from '@mui/material/Box';
import Chip from '@mui/material/Chip';
import Switch from '@mui/material/Switch';
import FormControlLabel from '@mui/material/FormControlLabel';
import IconButton from '@mui/material/IconButton';
import Tooltip from '@mui/material/Tooltip';
import KeyIcon from '@mui/icons-material/Key';
import { ReadyState } from 'react-use-websocket';
import type { ConsoleControlsProps } from './utils/types';

/**
 * Get status label and color for WebSocket ready state
 */
function getStatusProps(
  readyState: ReadyState,
  isBusy: boolean = false
): { label: string; color: 'success' | 'error' | 'warning' | 'default' } {
  if (isBusy) {
    return { label: 'Busy', color: 'warning' };
  }

  switch (readyState) {
    case ReadyState.CONNECTING:
      return { label: 'Connecting', color: 'default' };
    case ReadyState.OPEN:
      return { label: 'Connected', color: 'success' };
    case ReadyState.CLOSING:
      return { label: 'Closing', color: 'warning' };
    case ReadyState.CLOSED:
      return { label: 'Disconnected', color: 'error' };
    case ReadyState.UNINSTANTIATED:
    default:
      return { label: 'Off', color: 'default' };
  }
}

/**
 * Console controls: token button, status chips, and console toggle
 */
export const ConsoleControls = memo(function ConsoleControls({
  logReadyState,
  consoleReadyState,
  consoleEnabled,
  onConsoleToggle,
  onTokenClick,
  consoleBusy,
}: ConsoleControlsProps) {
  const logStatus = getStatusProps(logReadyState);
  const consoleStatus = getStatusProps(consoleReadyState, consoleBusy);

  const handleToggle = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      onConsoleToggle(e.target.checked);
    },
    [onConsoleToggle]
  );

  return (
    <Box
      sx={{
        display: 'flex',
        alignItems: 'center',
        gap: 2,
        flexWrap: 'wrap',
      }}
    >
      <Tooltip title="Change token">
        <IconButton onClick={onTokenClick} size="small">
          <KeyIcon />
        </IconButton>
      </Tooltip>

      <Box sx={{ display: 'flex', alignItems: 'center', gap: 1 }}>
        <Chip
          label={`Log: ${logStatus.label}`}
          color={logStatus.color}
          size="small"
          variant="outlined"
        />
        <Chip
          label={`Console: ${consoleStatus.label}`}
          color={consoleStatus.color}
          size="small"
          variant="outlined"
        />
      </Box>

      <FormControlLabel
        control={
          <Switch
            checked={consoleEnabled}
            onChange={handleToggle}
            size="small"
          />
        }
        label="Enable console"
        sx={{ ml: 'auto' }}
      />
    </Box>
  );
});
