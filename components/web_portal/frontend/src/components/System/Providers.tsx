import { ReactNode } from 'react';
import { Provider as JotaiProvider } from 'jotai';
import { ThemeProvider } from '@mui/material/styles';
import CssBaseline from '@mui/material/CssBaseline';
import { theme } from '../../theme';
import { SnackbarProvider } from '../../contexts/SnackbarContext';

interface ProvidersProps { children: ReactNode }

export function Providers({ children }: ProvidersProps) {
  return (
    <JotaiProvider>
      <ThemeProvider theme={theme}>
        <CssBaseline />
        <SnackbarProvider>
          {children}
        </SnackbarProvider>
      </ThemeProvider>
    </JotaiProvider>
  );
}

