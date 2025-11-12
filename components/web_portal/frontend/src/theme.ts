import { createTheme, type Theme, type PaletteColor, type PaletteColorOptions } from '@mui/material/styles';
import type { SensorState } from './api/types';

// Extend MUI theme to include custom 'tablet' breakpoint and custom colors
declare module '@mui/material/styles' {
  interface BreakpointOverrides {
    xs: true;
    sm: true;
    tablet: true; // adds the `tablet` breakpoint
    md: true;
    lg: true;
    xl: true;
  }

  interface Palette {
    custom: {
      yellow: PaletteColor;
      purple: PaletteColor;
    };
  }

  interface PaletteOptions {
    custom?: {
      yellow?: PaletteColorOptions;
      purple?: PaletteColorOptions;
    };
  }
}

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
    custom: {
      yellow: {
        main: '#ffeb3b',
        light: '#ffee58',
        dark: '#fbc02d',
      },
      purple: {
        main: '#9c27b0',
        light: '#ab47bc',
        dark: '#7b1fa2',
      },
    },
  },

  breakpoints: {
    values: {
      xs: 0,       // Mobile portrait
      sm: 600,     // Mobile landscape
      tablet: 768, // Tablet portrait (iPad, Android tablets)
      md: 1024,    // Tablet landscape / Small laptop
      lg: 1280,    // Desktop
      xl: 1920,    // Large screens
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
// COLOR HELPER FUNCTIONS (Theme-aware)
// ============================================================================

// AQI color mapping - uses theme palette for consistency and dark mode support
export const getAQIColor = (aqi: number | null | undefined, currentTheme: Theme = theme): string => {
  if (aqi === null || aqi === undefined) return currentTheme.palette.grey[500];
  if (aqi <= 50) return currentTheme.palette.success.light;          // Green - Good
  if (aqi <= 100) return currentTheme.palette.custom.yellow.main;   // Yellow - Moderate
  if (aqi <= 150) return currentTheme.palette.warning.light;        // Orange - Unhealthy for Sensitive
  if (aqi <= 200) return currentTheme.palette.error.main;           // Red - Unhealthy
  if (aqi <= 300) return currentTheme.palette.custom.purple.main;   // Purple - Very Unhealthy
  return currentTheme.palette.custom.purple.dark;                    // Dark Purple - Hazardous
};

// Comfort score color mapping - uses theme palette
export const getComfortColor = (score: number | null | undefined, currentTheme: Theme = theme): string => {
  if (score === null || score === undefined) return currentTheme.palette.grey[500];
  if (score >= 80) return currentTheme.palette.success.light;        // Green - Comfortable
  if (score >= 60) return currentTheme.palette.custom.yellow.main;  // Yellow - Acceptable
  if (score >= 40) return currentTheme.palette.warning.light;       // Orange - Uncomfortable
  return currentTheme.palette.error.main;                            // Red - Very Uncomfortable
};

// IAQ score color mapping (0-100 scale, higher is better) - uses theme palette
export const getIAQColor = (score: number | null | undefined, currentTheme: Theme = theme): string => {
  if (score === null || score === undefined) return currentTheme.palette.grey[500];
  if (score >= 80) return currentTheme.palette.success.light;        // Green - Excellent
  if (score >= 60) return currentTheme.palette.custom.yellow.main;  // Yellow - Good
  if (score >= 40) return currentTheme.palette.warning.light;       // Orange - Fair
  return currentTheme.palette.error.main;                            // Red - Poor/Very Poor
};

// Sensor state color mapping - uses theme palette
export const getSensorStateColor = (state: SensorState, currentTheme: Theme = theme): string => {
  switch (state) {
    case 'READY': return currentTheme.palette.success.main;         // Green
    case 'WARMING': return currentTheme.palette.warning.light;      // Orange
    case 'INIT': return currentTheme.palette.info.light;            // Blue
    case 'ERROR': return currentTheme.palette.error.main;           // Red
    case 'DISABLED': return currentTheme.palette.grey[500];         // Gray
    case 'UNINIT': return currentTheme.palette.grey[600];           // Dark Gray
    default: return currentTheme.palette.grey[500];
  }
};
