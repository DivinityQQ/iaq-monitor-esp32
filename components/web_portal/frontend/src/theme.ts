import { createTheme } from '@mui/material/styles';
import type { SensorState } from './api/types';

export const theme = createTheme({
  palette: {
    mode: 'light',
    primary: {
      main: '#1976d2',
      light: '#42a5f5',
      dark: '#1565c0',
    },
    secondary: {
      main: '#dc004e',
      light: '#f50057',
      dark: '#c51162',
    },
    success: {
      main: '#2e7d32',
      light: '#4caf50',
      dark: '#1b5e20',
    },
    warning: {
      main: '#ed6c02',
      light: '#ff9800',
      dark: '#e65100',
    },
    error: {
      main: '#d32f2f',
      light: '#ef5350',
      dark: '#c62828',
    },
    info: {
      main: '#0288d1',
      light: '#03a9f4',
      dark: '#01579b',
    },
    background: {
      default: '#f5f5f5',
      paper: '#ffffff',
    },
  },

  typography: {
    fontFamily: [
      '-apple-system',
      'BlinkMacSystemFont',
      '"Segoe UI"',
      'Roboto',
      '"Helvetica Neue"',
      'Arial',
      'sans-serif',
    ].join(','),
    h1: {
      fontSize: '2.5rem',
      fontWeight: 500,
    },
    h2: {
      fontSize: '2rem',
      fontWeight: 500,
    },
    h3: {
      fontSize: '1.75rem',
      fontWeight: 500,
    },
    h4: {
      fontSize: '1.5rem',
      fontWeight: 500,
    },
    h5: {
      fontSize: '1.25rem',
      fontWeight: 500,
    },
    h6: {
      fontSize: '1rem',
      fontWeight: 500,
    },
  },

  shape: {
    borderRadius: 12,
  },

  components: {
    MuiCard: {
      styleOverrides: {
        root: {
          borderRadius: 12,
          boxShadow: '0 2px 8px rgba(0,0,0,0.1)',
        },
      },
    },
    MuiButton: {
      styleOverrides: {
        root: {
          borderRadius: 8,
          textTransform: 'none',
          fontWeight: 500,
        },
      },
    },
    MuiChip: {
      styleOverrides: {
        root: {
          borderRadius: 8,
          fontWeight: 500,
        },
      },
    },
  },
});

// ============================================================================
// COLOR HELPER FUNCTIONS
// ============================================================================

// AQI color mapping
export const getAQIColor = (aqi: number | null | undefined): string => {
  if (aqi === null || aqi === undefined) return '#9e9e9e'; // Gray
  if (aqi <= 50) return '#4caf50';   // Green - Good
  if (aqi <= 100) return '#ffeb3b';  // Yellow - Moderate
  if (aqi <= 150) return '#ff9800';  // Orange - Unhealthy for Sensitive
  if (aqi <= 200) return '#f44336';  // Red - Unhealthy
  if (aqi <= 300) return '#9c27b0';  // Purple - Very Unhealthy
  return '#7b1fa2';                  // Dark Purple - Hazardous
};

// Comfort score color mapping
export const getComfortColor = (score: number | null | undefined): string => {
  if (score === null || score === undefined) return '#9e9e9e';
  if (score >= 80) return '#4caf50';  // Green - Comfortable
  if (score >= 60) return '#ffeb3b';  // Yellow - Acceptable
  if (score >= 40) return '#ff9800';  // Orange - Uncomfortable
  return '#f44336';                   // Red - Very Uncomfortable
};

// Sensor state color mapping
export const getSensorStateColor = (state: SensorState): string => {
  switch (state) {
    case 'READY': return '#4caf50';     // Green
    case 'WARMING': return '#ff9800';   // Orange
    case 'INIT': return '#2196f3';      // Blue
    case 'ERROR': return '#f44336';     // Red
    case 'DISABLED': return '#9e9e9e';  // Gray
    case 'UNINIT': return '#757575';    // Dark Gray
    default: return '#9e9e9e';
  }
};
