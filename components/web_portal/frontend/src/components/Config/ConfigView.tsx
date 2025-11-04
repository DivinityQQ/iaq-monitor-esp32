import { useState } from 'react';
import Box from '@mui/material/Box';
import Container from '@mui/material/Container';
import Tab from '@mui/material/Tab';
import Tabs from '@mui/material/Tabs';
import Typography from '@mui/material/Typography';
import {
  Wifi as WifiIcon,
  CloudQueue as MQTTIcon,
  Sensors as SensorsIcon,
} from '@mui/icons-material';
import { WiFiConfig } from './WiFiConfig';
import { MQTTConfig } from './MQTTConfig';
import { SensorControl } from './SensorControl';

interface TabPanelProps {
  children?: React.ReactNode;
  index: number;
  value: number;
}

function TabPanel({ children, value, index }: TabPanelProps) {
  return (
    <div
      role="tabpanel"
      hidden={value !== index}
      id={`config-tabpanel-${index}`}
      aria-labelledby={`config-tab-${index}`}
    >
      {value === index && <Box sx={{ py: 3 }}>{children}</Box>}
    </div>
  );
}

/**
 * Configuration view with tabbed navigation
 *
 * Features:
 * - WiFi configuration (scan, connect, status)
 * - MQTT configuration (broker setup, credentials)
 * - Sensor control (enable/disable, cadence, actions)
 * - Responsive tab layout (scrollable on mobile)
 * - Lazy rendering (only active tab content rendered)
 */
export function ConfigView() {
  const [activeTab, setActiveTab] = useState(0);

  const handleTabChange = (_event: React.SyntheticEvent, newValue: number) => {
    setActiveTab(newValue);
  };

  return (
    <Container maxWidth="xl" sx={{ py: 3 }}>
      {/* Configuration Header */}
      <Box sx={{ mb: 4 }}>
        <Typography variant="h4" component="h1" fontWeight={600} gutterBottom>
          Configuration
        </Typography>
        <Typography variant="body1" color="text.secondary">
          Configure WiFi, MQTT, and sensor settings
        </Typography>
      </Box>

      {/* Tabs Navigation */}
      <Box sx={{ borderBottom: 1, borderColor: 'divider', mb: 3 }}>
        <Tabs
          value={activeTab}
          onChange={handleTabChange}
          aria-label="configuration tabs"
          variant="scrollable"
          scrollButtons="auto"
          sx={{
            '& .MuiTab-root': {
              minHeight: 48,
              textTransform: 'none',
              fontSize: '1rem',
              fontWeight: 500,
            },
          }}
        >
          <Tab
            icon={<WifiIcon />}
            iconPosition="start"
            label="WiFi"
            id="config-tab-0"
            aria-controls="config-tabpanel-0"
          />
          <Tab
            icon={<MQTTIcon />}
            iconPosition="start"
            label="MQTT"
            id="config-tab-1"
            aria-controls="config-tabpanel-1"
          />
          <Tab
            icon={<SensorsIcon />}
            iconPosition="start"
            label="Sensors"
            id="config-tab-2"
            aria-controls="config-tabpanel-2"
          />
        </Tabs>
      </Box>

      {/* Tab Panels */}
      <TabPanel value={activeTab} index={0}>
        <WiFiConfig />
      </TabPanel>

      <TabPanel value={activeTab} index={1}>
        <MQTTConfig />
      </TabPanel>

      <TabPanel value={activeTab} index={2}>
        <SensorControl />
      </TabPanel>
    </Container>
  );
}
