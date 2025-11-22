import Box from '@mui/material/Box';
import CircularProgress from '@mui/material/CircularProgress';
import Typography from '@mui/material/Typography';
import { Route, Switch } from 'wouter';
import { lazy, Suspense, useCallback, useState } from 'react';
import { AppBar } from './AppBar';
import { NavDrawer } from './NavDrawer';
import { Dashboard } from '../Dashboard/Dashboard';

// Lazy-load chart, health, and config containers
const ChartContainer = lazy(() =>
  import('../Charts/ChartContainer').then(module => ({ default: module.ChartContainer }))
);

const HealthDashboard = lazy(() =>
  import('../Health/HealthDashboard').then(module => ({ default: module.HealthDashboard }))
);

const ConfigView = lazy(() =>
  import('../Config/ConfigView').then(module => ({ default: module.ConfigView }))
);

const ChartsView = () => (
  <Suspense fallback={<Box display="flex" justifyContent="center" alignItems="center" minHeight={400}><CircularProgress /></Box>}>
    <ChartContainer />
  </Suspense>
);

const HealthView = () => (
  <Suspense fallback={<Box display="flex" justifyContent="center" alignItems="center" minHeight={400}><CircularProgress /></Box>}>
    <HealthDashboard />
  </Suspense>
);

const ConfigViewRoute = () => (
  <Suspense fallback={<Box display="flex" justifyContent="center" alignItems="center" minHeight={400}><CircularProgress /></Box>}>
    <ConfigView />
  </Suspense>
);

export function AppShell() {
  const [drawerOpen, setDrawerOpen] = useState(false);

  const toggleDrawer = useCallback(() => setDrawerOpen(v => !v), []);
  const handleDrawerClose = useCallback(() => setDrawerOpen(false), []);

  return (
    <Box sx={{ display: 'flex', minHeight: '100vh' }}>
      <AppBar onMenuClick={toggleDrawer} />
      <NavDrawer open={drawerOpen} onClose={handleDrawerClose} />
      <Box component="main" sx={{ flexGrow: 1, mt: 8, ml: { md: '240px' }, width: { xs: '100%', md: `calc(100% - 240px)` } }}>
        <Switch>
          <Route path="/" component={Dashboard} />
          <Route path="/charts" component={ChartsView} />
          <Route path="/config" component={ConfigViewRoute} />
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

