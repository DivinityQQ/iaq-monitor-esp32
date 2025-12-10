import { useState, useCallback } from 'react';
import Button from '@mui/material/Button';
import Dialog from '@mui/material/Dialog';
import DialogActions from '@mui/material/DialogActions';
import DialogContent from '@mui/material/DialogContent';
import DialogContentText from '@mui/material/DialogContentText';
import DialogTitle from '@mui/material/DialogTitle';
import TextField from '@mui/material/TextField';
import InputAdornment from '@mui/material/InputAdornment';
import IconButton from '@mui/material/IconButton';
import VisibilityIcon from '@mui/icons-material/Visibility';
import VisibilityOffIcon from '@mui/icons-material/VisibilityOff';
import type { TokenDialogProps } from './utils/types';

/**
 * Dialog for entering/updating the console bearer token
 * Token is used for authentication with /ws/log and /ws/console endpoints
 */
export function TokenDialog({ open, onClose, token, onSave }: TokenDialogProps) {
  const [value, setValue] = useState(token);
  const [showToken, setShowToken] = useState(false);

  // Sync value when dialog opens with new token
  const handleEnter = useCallback(() => {
    setValue(token);
    setShowToken(false);
  }, [token]);

  const handleSave = useCallback(() => {
    onSave(value.trim());
    onClose();
  }, [value, onSave, onClose]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && value.trim()) {
      handleSave();
    }
  }, [value, handleSave]);

  const toggleVisibility = useCallback(() => {
    setShowToken((v) => !v);
  }, []);

  return (
    <Dialog
      open={open}
      onClose={onClose}
      maxWidth="sm"
      fullWidth
      TransitionProps={{ onEnter: handleEnter }}
    >
      <DialogTitle>Console Token</DialogTitle>
      <DialogContent>
        <DialogContentText sx={{ mb: 2 }}>
          Enter the bearer token configured in <code>IAQ_WEB_CONSOLE_TOKEN</code> to access the device console.
        </DialogContentText>
        <TextField
          autoFocus
          fullWidth
          label="Token"
          type={showToken ? 'text' : 'password'}
          value={value}
          onChange={(e) => setValue(e.target.value)}
          onKeyDown={handleKeyDown}
          slotProps={{
            input: {
              endAdornment: (
                <InputAdornment position="end">
                  <IconButton
                    aria-label={showToken ? 'Hide token' : 'Show token'}
                    onClick={toggleVisibility}
                    edge="end"
                  >
                    {showToken ? <VisibilityOffIcon /> : <VisibilityIcon />}
                  </IconButton>
                </InputAdornment>
              ),
              sx: { fontFamily: 'monospace' },
            },
          }}
        />
      </DialogContent>
      <DialogActions sx={{ px: 3, pb: 2 }}>
        <Button onClick={onClose}>Cancel</Button>
        <Button
          onClick={handleSave}
          variant="contained"
          disabled={!value.trim()}
        >
          Save
        </Button>
      </DialogActions>
    </Dialog>
  );
}
