import { ReactNode } from 'react';
import { Provider as JotaiProvider } from 'jotai';
import { ThemeProvider } from '@mui/material/styles';
import CssBaseline from '@mui/material/CssBaseline';
import { theme } from '../../theme';
import { SnackbarProvider } from '../../contexts/SnackbarContext';

interface ProvidersProps { children: ReactNode }

/**
 * Application providers wrapper
 *
 * MUI v7 colorSchemes API handles theme switching automatically:
 * - System preference detection built-in
 * - localStorage persistence built-in
 * - No manual theme state management needed
 */
export function Providers({ children }: ProvidersProps) {
  return (
    <JotaiProvider>
      <ThemeProvider theme={theme} disableTransitionOnChange>
        <CssBaseline />
        <SnackbarProvider>
          {children}
        </SnackbarProvider>
      </ThemeProvider>
    </JotaiProvider>
  );
}
