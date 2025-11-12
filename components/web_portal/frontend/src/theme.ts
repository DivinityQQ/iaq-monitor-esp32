import { createTheme, type Theme } from '@mui/material/styles';
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
      yellow: {
        main: string;
        light: string;
        dark: string;
      };
      purple: {
        main: string;
        light: string;
        dark: string;
      };
    };
  }

  interface PaletteOptions {
    custom?: {
      yellow?: {
        main?: string;
        light?: string;
        dark?: string;
      };
      purple?: {
        main?: string;
        light?: string;
        dark?: string;
      };
    };
  }
}

/**
 * MUI v7 theme with colorSchemes API for optimal dark mode support
 * Uses CSS variables and automatic system preference detection
 */
export const theme = createTheme({
  // Enable CSS variables for better performance and dynamic theming
  cssVariables: {
    colorSchemeSelector: 'class',
  },

  // Define color schemes for light and dark modes
  colorSchemes: {
    light: {
      palette: {
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
    },
    dark: {
      palette: {
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
          default: '#121212',
          paper: '#1e1e1e',
        },
        custom: {
          yellow: {
            main: '#fdd835',
            light: '#ffeb3b',
            dark: '#f9a825',
          },
          purple: {
            main: '#ab47bc',
            light: '#ba68c8',
            dark: '#8e24aa',
          },
        },
      },
    },
  },

  // Shared configuration (applies to both light and dark modes)
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
    MuiAppBar: {
      defaultProps: {
        enableColorOnDark: true, // Use primary color in dark mode
      },
    },
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

// Helpers that return CSS variable references for palette colors.
// Using CSS vars ensures instant, repaint-only theme switching without rerenders.

const cssVar = (name: string) => `var(${name})`;
const muiPaletteVar = (...parts: string[]) => cssVar(`--mui-palette-${parts.join('-')}`);

// Internal: choose palette token by value thresholds
const aqiToken = (aqi: number) => {
  if (aqi <= 50) return ['success', 'light'] as const;                 // Good
  if (aqi <= 100) return ['custom', 'yellow', 'main'] as const;        // Moderate
  if (aqi <= 150) return ['warning', 'light'] as const;                // Unhealthy (sensitive)
  if (aqi <= 200) return ['error', 'main'] as const;                   // Unhealthy
  if (aqi <= 300) return ['custom', 'purple', 'main'] as const;        // Very Unhealthy
  return ['custom', 'purple', 'dark'] as const;                        // Hazardous
};

const comfortToken = (score: number) => {
  if (score >= 80) return ['success', 'light'] as const;               // Comfortable
  if (score >= 60) return ['custom', 'yellow', 'main'] as const;       // Acceptable
  if (score >= 40) return ['warning', 'light'] as const;               // Uncomfortable
  return ['error', 'main'] as const;                                   // Very Uncomfortable
};

const iaqToken = (score: number) => {
  if (score >= 80) return ['success', 'light'] as const;               // Excellent
  if (score >= 60) return ['custom', 'yellow', 'main'] as const;       // Good
  if (score >= 40) return ['warning', 'light'] as const;               // Fair
  return ['error', 'main'] as const;                                   // Poor / Very Poor
};

// Public helpers (CSS var strings)
export const getAQIColorVar = (aqi: number | null | undefined, currentTheme: Theme = theme): string => {
  if (aqi === null || aqi === undefined) return muiPaletteVar('grey', '500');
  const t = aqiToken(aqi);
  // Handle custom palette safely with fallbacks
  if (t[0] === 'custom') {
    const [, sub, shade] = t;
    // Prefer theme.vars if present; otherwise fallback to a close standard token
    const anyVars: any = (currentTheme as any).vars?.palette;
    const v = anyVars?.custom?.[sub]?.[shade];
    return v ?? (shade === 'dark' ? muiPaletteVar('secondary', 'dark') : muiPaletteVar('secondary', 'main'));
  }
  return muiPaletteVar(t[0], t[1]);
};

export const getComfortColorVar = (score: number | null | undefined, currentTheme: Theme = theme): string => {
  if (score === null || score === undefined) return muiPaletteVar('grey', '500');
  const t = comfortToken(score);
  if (t[0] === 'custom') {
    const [, sub, shade] = t;
    const anyVars: any = (currentTheme as any).vars?.palette;
    const v = anyVars?.custom?.[sub]?.[shade];
    return v ?? muiPaletteVar('warning', 'main');
  }
  return muiPaletteVar(t[0], t[1]);
};

export const getIAQColorVar = (score: number | null | undefined, currentTheme: Theme = theme): string => {
  if (score === null || score === undefined) return muiPaletteVar('grey', '500');
  const t = iaqToken(score);
  if (t[0] === 'custom') {
    const [, sub, shade] = t;
    const anyVars: any = (currentTheme as any).vars?.palette;
    const v = anyVars?.custom?.[sub]?.[shade];
    return v ?? muiPaletteVar('warning', 'main');
  }
  return muiPaletteVar(t[0], t[1]);
};

export const getSensorStateColor = (state: SensorState): string => {
  switch (state) {
    case 'READY':
      return muiPaletteVar('success', 'main');
    case 'WARMING':
      return muiPaletteVar('warning', 'light');
    case 'INIT':
      return muiPaletteVar('info', 'light');
    case 'ERROR':
      return muiPaletteVar('error', 'main');
    case 'DISABLED':
      return muiPaletteVar('grey', '500');
    case 'UNINIT':
      return muiPaletteVar('grey', '600');
    default:
      return muiPaletteVar('grey', '500');
  }
};

// Backwards-compatible named exports for code that expects hex.
// These return CSS vars as well (safer for theme switching). Keep signatures.
export const getAQIColor = (aqi: number | null | undefined, currentTheme: Theme = theme) => getAQIColorVar(aqi, currentTheme);
export const getComfortColor = (score: number | null | undefined, currentTheme: Theme = theme) => getComfortColorVar(score, currentTheme);
export const getIAQColor = (score: number | null | undefined, currentTheme: Theme = theme) => getIAQColorVar(score, currentTheme);
