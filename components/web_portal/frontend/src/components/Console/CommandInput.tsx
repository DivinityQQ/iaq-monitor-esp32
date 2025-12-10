import { useState, useCallback, useRef, memo } from 'react';
import TextField from '@mui/material/TextField';
import InputAdornment from '@mui/material/InputAdornment';
import IconButton from '@mui/material/IconButton';
import SendIcon from '@mui/icons-material/Send';
import { MONOSPACE_FONT } from '../../utils/constants';
import type { CommandInputProps } from './utils/types';

const MAX_HISTORY = 50;

/**
 * Command input field with history navigation (up/down arrows)
 * Disabled when console is not connected
 */
export const CommandInput = memo(function CommandInput({ disabled, onSubmit }: CommandInputProps) {
  const [value, setValue] = useState('');
  const [history, setHistory] = useState<string[]>([]);
  const [historyIndex, setHistoryIndex] = useState(-1);
  // Store the current input when navigating history
  const savedInput = useRef('');

  const handleSubmit = useCallback(() => {
    const cmd = value.trim();
    if (!cmd) return;

    onSubmit(cmd);
    setValue('');
    setHistoryIndex(-1);
    savedInput.current = '';

    // Add to history (avoid duplicates of last command)
    setHistory((prev) => {
      if (prev[0] === cmd) return prev;
      const next = [cmd, ...prev];
      return next.slice(0, MAX_HISTORY);
    });
  }, [value, onSubmit]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      handleSubmit();
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (history.length === 0) return;

      // Save current input when starting to navigate
      if (historyIndex === -1) {
        savedInput.current = value;
      }

      const newIndex = Math.min(historyIndex + 1, history.length - 1);
      setHistoryIndex(newIndex);
      setValue(history[newIndex]);
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (historyIndex === -1) return;

      const newIndex = historyIndex - 1;
      setHistoryIndex(newIndex);

      if (newIndex === -1) {
        // Restore saved input
        setValue(savedInput.current);
      } else {
        setValue(history[newIndex]);
      }
    }
  }, [history, historyIndex, value, handleSubmit]);

  const handleChange = useCallback((e: React.ChangeEvent<HTMLInputElement>) => {
    setValue(e.target.value);
    // Reset history navigation when typing
    if (historyIndex !== -1) {
      setHistoryIndex(-1);
      savedInput.current = '';
    }
  }, [historyIndex]);

  return (
    <TextField
      fullWidth
      size="small"
      placeholder={disabled ? 'Console not connected' : 'Type command and press Enter...'}
      value={value}
      onChange={handleChange}
      onKeyDown={handleKeyDown}
      disabled={disabled}
      slotProps={{
        input: {
          sx: { fontFamily: MONOSPACE_FONT },
          endAdornment: (
            <InputAdornment position="end">
              <IconButton
                aria-label="Send command"
                onClick={handleSubmit}
                disabled={disabled || !value.trim()}
                edge="end"
                size="small"
              >
                <SendIcon />
              </IconButton>
            </InputAdornment>
          ),
        },
      }}
    />
  );
});
