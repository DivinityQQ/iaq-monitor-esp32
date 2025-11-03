import { ThemeProvider, CssBaseline, Box, Typography, Alert } from '@mui/material';
import { Provider as JotaiProvider, useSetAtom, useAtomValue } from 'jotai';
import { Route, Switch } from 'wouter';
import { useState, useEffect } from 'react';
import { theme } from './theme';
import { AppBar } from './components/Layout/AppBar';
import { NavDrawer } from './components/Layout/NavDrawer';
import { Dashboard } from './components/Dashboard/Dashboard';
import { ConfigView } from './components/Config/ConfigView';
import { LoadingSpinner } from './components/Common/LoadingSkeleton';
import { useWebSocketConnection } from './hooks/useWebSocket';
import { deviceInfoAtom, mqttStatusAtom, appReadyAtom } from './store/atoms';
import { apiClient } from './api/client';

// Placeholder views - will be replaced in future weeks
const ChartsView = () => (
  <Box p={3}>
    <Typography variant="h4" gutterBottom>
      Charts
    </Typography>
    <Typography variant="body1" color="text.secondary">
      Historical charts will be implemented in Week 4.
    </Typography>
  </Box>
);

const HealthView = () => (
  <Box p={3}>
    <Typography variant="h4" gutterBottom>
      System Health
    </Typography>
    <Typography variant="body1" color="text.secondary">
      System health and sensor status will be implemented in Week 4.
    </Typography>
  </Box>
);

/**
 * App content component
 * Handles WebSocket connection and device info fetching
 * Separated from App to have access to Jotai context
 */
function AppContent() {
  const [drawerOpen, setDrawerOpen] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const setDeviceInfo = useSetAtom(deviceInfoAtom);
  const setMqttStatus = useSetAtom(mqttStatusAtom);
  const appReady = useAtomValue(appReadyAtom);

  // Establish WebSocket connection
  useWebSocketConnection();

  // Fetch device info and MQTT status on mount; state/metrics/health arrive via WS
  useEffect(() => {
    const fetchInitialData = async () => {
      try {
        const [info, mqttStatus] = await Promise.all([
          apiClient.getInfo(),
          apiClient.getMQTTStatus(),
        ]);
        setDeviceInfo(info);
        setMqttStatus(mqttStatus);
      } catch (err) {
        console.error('Failed to fetch initial data:', err);
        setError('Failed to connect to device. Please check your connection.');
      }
    };

    fetchInitialData();
  }, [setDeviceInfo, setMqttStatus]);

  const toggleDrawer = () => {
    setDrawerOpen(!drawerOpen);
  };

  // Clear connection error once data is available (via WS or REST)
  useEffect(() => {
    if (appReady && error) {
      setError(null);
    }
  }, [appReady, error]);

  // Show loading state while fetching initial data
  if (!appReady) {
    return (
      <Box sx={{ display: 'flex', minHeight: '100vh' }}>
        <AppBar onMenuClick={toggleDrawer} />
        <NavDrawer open={drawerOpen} onClose={() => setDrawerOpen(false)} />
        <Box
          component="main"
          sx={{
            flexGrow: 1,
            mt: 8,
            ml: { sm: '240px' },
            width: { xs: '100%', sm: `calc(100% - 240px)` },
          }}
        >
          {error ? (
            <Box p={3}>
              <Alert severity="error">{error}</Alert>
            </Box>
          ) : (
            <LoadingSpinner />
          )}
        </Box>
      </Box>
    );
  }

  return (
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
        {error && (
          <Box p={3}>
            <Alert severity="warning">{error}</Alert>
          </Box>
        )}
        <Switch>
          <Route path="/" component={Dashboard} />
          <Route path="/charts" component={ChartsView} />
          <Route path="/config" component={ConfigView} />
          <Route path="/health" component={HealthView} />
          <Route>
            <Box p={3}>
              <Typography variant="h4" gutterBottom>
                404 - Not Found
              </Typography>
              <Typography variant="body1" color="text.secondary">
                The page you're looking for doesn't exist.
              </Typography>
            </Box>
          </Route>
        </Switch>
      </Box>
    </Box>
  );
}

function App() {
  return (
    <JotaiProvider>
      <ThemeProvider theme={theme}>
        <CssBaseline />
        <AppContent />
      </ThemeProvider>
    </JotaiProvider>
  );
}

export default App;
