import Alert from '@mui/material/Alert';
import Box from '@mui/material/Box';
import CircularProgress from '@mui/material/CircularProgress';
import Typography from '@mui/material/Typography';
import { Route, Switch } from 'wouter';
import { lazy, Suspense, useCallback, useState } from 'react';
import { useAtomValue } from 'jotai';
import { AppBar } from './AppBar';
import { NavDrawer } from './NavDrawer';
import { bootstrapErrorAtom } from '../../store/atoms';
import { Dashboard } from '../Dashboard/Dashboard';
import { HealthDashboard } from '../Health/HealthDashboard';
import { ConfigView } from '../Config/ConfigView';
import { PowerDashboard } from '../Power/PowerDashboard';
import { OTADashboard } from '../OTA/OTADashboard';
import { ConsoleDashboard } from '../Console/ConsoleDashboard';

// Only lazy-load ChartContainer to keep @mui/x-charts out of initial bundle
const ChartContainer = lazy(() => import('../Charts/ChartContainer').then(m => ({ default: m.ChartContainer })));

export function AppShell() {
  const [drawerOpen, setDrawerOpen] = useState(false);
  const bootstrapError = useAtomValue(bootstrapErrorAtom);

  const toggleDrawer = useCallback(() => setDrawerOpen(v => !v), []);
  const handleDrawerClose = useCallback(() => setDrawerOpen(false), []);

  return (
    <Box sx={{ display: 'flex', minHeight: '100vh' }}>
      <AppBar onMenuClick={toggleDrawer} />
      <NavDrawer open={drawerOpen} onClose={handleDrawerClose} />
      <Box component="main" sx={{ flexGrow: 1, mt: 8, ml: { md: '240px' }, width: { xs: '100%', md: `calc(100% - 240px)` } }}>
        {/* Bootstrap error banner */}
        {bootstrapError && (
          <Alert severity="error" sx={{ m: 2, mb: 0 }}>
            {bootstrapError}
          </Alert>
        )}
        <Suspense
          fallback={
            <Box display="flex" justifyContent="center" alignItems="center" minHeight={400}>
              <CircularProgress />
            </Box>
          }
        >
          <Switch>
            <Route path="/" component={Dashboard} />
            <Route path="/charts" component={ChartContainer} />
            <Route path="/config" component={ConfigView} />
            <Route path="/health" component={HealthDashboard} />
            <Route path="/power" component={PowerDashboard} />
            <Route path="/update" component={OTADashboard} />
            <Route path="/console" component={ConsoleDashboard} />
            <Route>
              <Box p={3}>
                <Typography variant="h4" gutterBottom>404 - Not Found</Typography>
                <Typography variant="body1" color="text.secondary">The page you're looking for doesn't exist.</Typography>
              </Box>
            </Route>
          </Switch>
        </Suspense>
      </Box>
    </Box>
  );
}
