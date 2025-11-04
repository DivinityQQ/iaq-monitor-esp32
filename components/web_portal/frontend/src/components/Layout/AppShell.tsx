import Alert from '@mui/material/Alert';
import Box from '@mui/material/Box';
import CircularProgress from '@mui/material/CircularProgress';
import Typography from '@mui/material/Typography';
import { Route, Switch } from 'wouter';
import { lazy, Suspense, useCallback, useState } from 'react';
import { useAtomValue } from 'jotai';
import { AppBar } from './AppBar';
import { NavDrawer } from './NavDrawer';
import { Dashboard } from '../Dashboard/Dashboard';
import { ConfigView } from '../Config/ConfigView';
import { LoadingSpinner } from '../Common/LoadingSkeleton';
import { appReadyAtom, appErrorAtom } from '../../store/atoms';

// Lazy-load chart and health containers
const ChartContainer = lazy(() =>
  import('../Charts/ChartContainer').then(module => ({ default: module.ChartContainer }))
);

const HealthDashboard = lazy(() =>
  import('../Health/HealthDashboard').then(module => ({ default: module.HealthDashboard }))
);

const ChartsView = () => (
  <Box p={3}>
    <Suspense fallback={<Box display="flex" justifyContent="center" alignItems="center" minHeight={400}><CircularProgress /></Box>}>
      <ChartContainer />
    </Suspense>
  </Box>
);

const HealthView = () => (
  <Box p={3}>
    <Suspense fallback={<Box display="flex" justifyContent="center" alignItems="center" minHeight={400}><CircularProgress /></Box>}>
      <HealthDashboard />
    </Suspense>
  </Box>
);

export function AppShell() {
  const [drawerOpen, setDrawerOpen] = useState(false);
  const appReady = useAtomValue(appReadyAtom);
  const error = useAtomValue(appErrorAtom);

  const toggleDrawer = useCallback(() => setDrawerOpen(v => !v), []);
  const handleDrawerClose = useCallback(() => setDrawerOpen(false), []);

  if (!appReady) {
    return (
      <Box sx={{ display: 'flex', minHeight: '100vh' }}>
        <AppBar onMenuClick={toggleDrawer} />
        <NavDrawer open={drawerOpen} onClose={handleDrawerClose} />
        <Box component="main" sx={{ flexGrow: 1, mt: 8, ml: { md: '240px' }, width: { xs: '100%', md: `calc(100% - 240px)` } }}>
          {error ? (
            <Box p={3}><Alert severity="error">{error}</Alert></Box>
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
      <NavDrawer open={drawerOpen} onClose={handleDrawerClose} />
      <Box component="main" sx={{ flexGrow: 1, mt: 8, ml: { md: '240px' }, width: { xs: '100%', md: `calc(100% - 240px)` } }}>
        {error && (
          <Box p={3}><Alert severity="warning">{error}</Alert></Box>
        )}
        <Switch>
          <Route path="/" component={Dashboard} />
          <Route path="/charts" component={ChartsView} />
          <Route path="/config" component={ConfigView} />
          <Route path="/health" component={HealthView} />
          <Route>
            <Box p={3}>
              <Typography variant="h4" gutterBottom>404 - Not Found</Typography>
              <Typography variant="body1" color="text.secondary">The page you're looking for doesn't exist.</Typography>
            </Box>
          </Route>
        </Switch>
      </Box>
    </Box>
  );
}

