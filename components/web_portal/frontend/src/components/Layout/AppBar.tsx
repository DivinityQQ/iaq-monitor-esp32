import { useState, useCallback, memo } from 'react';
import MuiAppBar from '@mui/material/AppBar';
import Box from '@mui/material/Box';
import IconButton from '@mui/material/IconButton';
import Toolbar from '@mui/material/Toolbar';
import Typography from '@mui/material/Typography';
import MenuIcon from '@mui/icons-material/Menu';
import InfoIcon from '@mui/icons-material/Info';
import { ConnectionStatus } from './ConnectionStatus';
import { InfoDialog } from '../Common/InfoDialog';

interface AppBarProps {
  onMenuClick: () => void;
}

function AppBarComponent({ onMenuClick }: AppBarProps) {
  const [infoOpen, setInfoOpen] = useState(false);

  // Memoize callbacks to prevent InfoDialog from receiving new props on every render
  const handleInfoOpen = useCallback(() => setInfoOpen(true), []);
  const handleInfoClose = useCallback(() => setInfoOpen(false), []);

  return (
    <>
      <MuiAppBar
        position="fixed"
        sx={{
          zIndex: (theme) => theme.zIndex.drawer + 1,
        }}
      >
        <Toolbar>
          <IconButton
            color="inherit"
            aria-label="open drawer"
            edge="start"
            onClick={onMenuClick}
            sx={{ mr: 2, display: { md: 'none' } }}
          >
            <MenuIcon />
          </IconButton>

          <Typography variant="h6" noWrap component="div" sx={{ flexGrow: 1 }}>
            IAQ Monitor
          </Typography>

          <Box sx={{ display: 'flex', alignItems: 'center', gap: 2 }}>
            <ConnectionStatus />
            <IconButton color="inherit" aria-label="device info" onClick={handleInfoOpen}>
              <InfoIcon />
            </IconButton>
          </Box>
        </Toolbar>
      </MuiAppBar>

      <InfoDialog open={infoOpen} onClose={handleInfoClose} />
    </>
  );
}

export const AppBar = memo(AppBarComponent);
