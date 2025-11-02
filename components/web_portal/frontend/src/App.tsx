import { ThemeProvider, CssBaseline } from '@mui/material';
import { Provider as JotaiProvider } from 'jotai';
import { Route, Switch } from 'wouter';
import { theme } from './theme';
import { AppBar } from './components/Layout/AppBar';
import { NavDrawer } from './components/Layout/NavDrawer';
import { Box } from '@mui/material';
import { useState } from 'react';

// Placeholder views - will be replaced with actual components
const DashboardView = () => (
  <Box p={3}>
    <h1>Dashboard</h1>
    <p>Real-time sensor data visualization will appear here.</p>
  </Box>
);

const ChartsView = () => (
  <Box p={3}>
    <h1>Charts</h1>
    <p>Historical charts will appear here.</p>
  </Box>
);

const ConfigView = () => (
  <Box p={3}>
    <h1>Configuration</h1>
    <p>WiFi, MQTT, and sensor configuration will appear here.</p>
  </Box>
);

const HealthView = () => (
  <Box p={3}>
    <h1>System Health</h1>
    <p>System health and sensor status will appear here.</p>
  </Box>
);

function App() {
  const [drawerOpen, setDrawerOpen] = useState(false);

  const toggleDrawer = () => {
    setDrawerOpen(!drawerOpen);
  };

  return (
    <JotaiProvider>
      <ThemeProvider theme={theme}>
        <CssBaseline />
        <Box sx={{ display: 'flex', minHeight: '100vh' }}>
          <AppBar onMenuClick={toggleDrawer} />
          <NavDrawer open={drawerOpen} onClose={() => setDrawerOpen(false)} />

          <Box
            component="main"
            sx={{
              flexGrow: 1,
              mt: 8, // Account for AppBar height
              ml: { sm: '240px' }, // Account for permanent drawer on desktop
              width: { xs: '100%', sm: `calc(100% - 240px)` },
            }}
          >
            <Switch>
              <Route path="/" component={DashboardView} />
              <Route path="/charts" component={ChartsView} />
              <Route path="/config" component={ConfigView} />
              <Route path="/health" component={HealthView} />
              <Route>
                <Box p={3}>
                  <h1>404 - Not Found</h1>
                  <p>The page you're looking for doesn't exist.</p>
                </Box>
              </Route>
            </Switch>
          </Box>
        </Box>
      </ThemeProvider>
    </JotaiProvider>
  );
}

export default App;
