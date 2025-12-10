import Drawer from '@mui/material/Drawer';
import List from '@mui/material/List';
import ListItem from '@mui/material/ListItem';
import ListItemButton from '@mui/material/ListItemButton';
import ListItemIcon from '@mui/material/ListItemIcon';
import ListItemText from '@mui/material/ListItemText';
import Toolbar from '@mui/material/Toolbar';
import DashboardIcon from '@mui/icons-material/Dashboard';
import ShowChartIcon from '@mui/icons-material/ShowChart';
import SettingsIcon from '@mui/icons-material/Settings';
import HealthAndSafetyIcon from '@mui/icons-material/HealthAndSafety';
import BatteryChargingFullIcon from '@mui/icons-material/BatteryChargingFull';
import SystemUpdateIcon from '@mui/icons-material/SystemUpdate';
import TerminalIcon from '@mui/icons-material/Terminal';
import { useLocation } from 'wouter';

interface NavDrawerProps {
  open: boolean;
  onClose: () => void;
}

const drawerWidth = 240;

const navItems = [
  { label: 'Dashboard', icon: <DashboardIcon />, path: '/' },
  { label: 'Charts', icon: <ShowChartIcon />, path: '/charts' },
  { label: 'Configuration', icon: <SettingsIcon />, path: '/config' },
  { label: 'Health', icon: <HealthAndSafetyIcon />, path: '/health' },
  { label: 'Power', icon: <BatteryChargingFullIcon />, path: '/power' },
  { label: 'Update', icon: <SystemUpdateIcon />, path: '/update' },
  { label: 'Console', icon: <TerminalIcon />, path: '/console' },
];

export function NavDrawer({ open, onClose }: NavDrawerProps) {
  const [location, setLocation] = useLocation();

  const handleNavClick = (path: string) => {
    setLocation(path);
    onClose(); // Close drawer on mobile after navigation
  };

  const drawer = (
    <>
      <Toolbar /> {/* Spacer for AppBar */}
      <List>
        {navItems.map((item) => (
          <ListItem key={item.path} disablePadding>
            <ListItemButton
              selected={location === item.path}
              onClick={() => handleNavClick(item.path)}
            >
              <ListItemIcon>{item.icon}</ListItemIcon>
              <ListItemText primary={item.label} />
            </ListItemButton>
          </ListItem>
        ))}
      </List>
    </>
  );

  return (
    <>
      {/* Mobile drawer (temporary) */}
      <Drawer
        variant="temporary"
        open={open}
        onClose={onClose}
        ModalProps={{
          keepMounted: true, // Better mobile performance
        }}
        sx={{
          display: { xs: 'block', md: 'none' },
          '& .MuiDrawer-paper': {
            boxSizing: 'border-box',
            width: drawerWidth,
          },
        }}
      >
        {drawer}
      </Drawer>

      {/* Desktop drawer (persistent) */}
      <Drawer
        variant="permanent"
        sx={{
          display: { xs: 'none', md: 'block' },
          '& .MuiDrawer-paper': {
            boxSizing: 'border-box',
            width: drawerWidth,
          },
        }}
        open
      >
        {drawer}
      </Drawer>
    </>
  );
}
