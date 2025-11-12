import { useState } from 'react';
import { useColorScheme } from '@mui/material/styles';
import { IconButton, Menu, MenuItem, ListItemIcon, ListItemText, Tooltip } from '@mui/material';
import {
  LightMode as LightModeIcon,
  DarkMode as DarkModeIcon,
  SettingsBrightness as SystemIcon,
  Check as CheckIcon,
} from '@mui/icons-material';

/**
 * Theme toggle component with dropdown menu
 * Uses MUI v7's built-in useColorScheme hook for theme management
 * Provides automatic system preference detection and localStorage persistence
 */
export function ThemeToggle() {
  const { mode, setMode, systemMode } = useColorScheme();
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);
  const open = Boolean(anchorEl);

  const handleClick = (event: React.MouseEvent<HTMLElement>) => {
    setAnchorEl(event.currentTarget);
  };

  const handleClose = () => {
    setAnchorEl(null);
  };

  const handleThemeChange = (newMode: 'light' | 'dark' | 'system') => {
    setMode(newMode);
    handleClose();
  };

  // Determine which icon to show based on resolved mode (considering system preference)
  const resolvedMode = mode === 'system' ? systemMode : mode;
  const ThemeIcon = resolvedMode === 'dark' ? DarkModeIcon : LightModeIcon;

  return (
    <>
      <Tooltip title="Theme Settings">
        <IconButton
          onClick={handleClick}
          color="inherit"
          aria-label="theme settings"
          aria-controls={open ? 'theme-menu' : undefined}
          aria-haspopup="true"
          aria-expanded={open ? 'true' : undefined}
        >
          <ThemeIcon />
        </IconButton>
      </Tooltip>
      <Menu
        id="theme-menu"
        anchorEl={anchorEl}
        open={open}
        onClose={handleClose}
        MenuListProps={{
          'aria-labelledby': 'theme-button',
        }}
      >
        <MenuItem onClick={() => handleThemeChange('light')}>
          <ListItemIcon>
            {mode === 'light' ? <CheckIcon fontSize="small" /> : <LightModeIcon fontSize="small" />}
          </ListItemIcon>
          <ListItemText>Light</ListItemText>
        </MenuItem>
        <MenuItem onClick={() => handleThemeChange('dark')}>
          <ListItemIcon>
            {mode === 'dark' ? <CheckIcon fontSize="small" /> : <DarkModeIcon fontSize="small" />}
          </ListItemIcon>
          <ListItemText>Dark</ListItemText>
        </MenuItem>
        <MenuItem onClick={() => handleThemeChange('system')}>
          <ListItemIcon>
            {mode === 'system' ? <CheckIcon fontSize="small" /> : <SystemIcon fontSize="small" />}
          </ListItemIcon>
          <ListItemText>System</ListItemText>
        </MenuItem>
      </Menu>
    </>
  );
}
