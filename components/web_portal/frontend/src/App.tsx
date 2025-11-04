import Alert from '@mui/material/Alert';
import Box from '@mui/material/Box';
import CircularProgress from '@mui/material/CircularProgress';
import CssBaseline from '@mui/material/CssBaseline';
import { ThemeProvider } from '@mui/material/styles';
import Typography from '@mui/material/Typography';
import { Provider as JotaiProvider, useSetAtom, useAtomValue } from 'jotai';
import { Route, Switch } from 'wouter';
import { useState, useEffect, lazy, Suspense } from 'react';
import { theme } from './theme';
import { AppBar } from './components/Layout/AppBar';
import { NavDrawer } from './components/Layout/NavDrawer';
import { Dashboard } from './components/Dashboard/Dashboard';
import { ConfigView } from './components/Config/ConfigView';
import { LoadingSpinner } from './components/Common/LoadingSkeleton';
import { ErrorBoundary } from './components/Common/ErrorBoundary';
import { useWebSocketConnection } from './hooks/useWebSocket';
import { deviceInfoAtom, mqttStatusAtom, appReadyAtom } from './store/atoms';
import { apiClient } from './api/client';
import { SnackbarProvider } from './contexts/SnackbarContext';
import { ChartBufferStream } from './components/Charts/ChartBufferStream';
import { logger } from './utils/logger';

// Lazy-load chart component for better bundle splitting
const ChartContainer = lazy(() =>
  import('./components/Charts/ChartContainer').then(module => ({
    default: module.ChartContainer,
  }))
);

// Lazy-load HealthDashboard component
const HealthDashboard = lazy(() =>
  import('./components/Health/HealthDashboard').then(module => ({
    default: module.HealthDashboard,
  }))
);

// Lazy-loaded ChartsView with Suspense fallback
const ChartsView = () => (
  <Box p={3}>
    <Suspense
      fallback={
        <Box display="flex" justifyContent="center" alignItems="center" minHeight={400}>
          <CircularProgress />
        </Box>
      }
    >
      <ChartContainer />
    </Suspense>
  </Box>
);

// Lazy-loaded HealthView with Suspense fallback
const HealthView = () => (
  <Box p={3}>
    <Suspense
      fallback={
        <Box display="flex" justifyContent="center" alignItems="center" minHeight={400}>
          <CircularProgress />
        </Box>
      }
    >
      <HealthDashboard />
    </Suspense>
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
        logger.error('Failed to fetch initial data:', err);
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
    <ErrorBoundary>
      <JotaiProvider>
        <ThemeProvider theme={theme}>
          <CssBaseline />
          <SnackbarProvider>
            {/* Keep chart buffers in sync app-wide so history persists across routes */}
            <ChartBufferStream />
            <AppContent />
          </SnackbarProvider>
        </ThemeProvider>
      </JotaiProvider>
    </ErrorBoundary>
  );
}

export default App;
