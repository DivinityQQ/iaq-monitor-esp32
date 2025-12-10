import { useState, useCallback, useEffect, useRef } from 'react';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Container from '@mui/material/Container';
import Typography from '@mui/material/Typography';
import useWebSocket, { ReadyState } from 'react-use-websocket';
import { useAtom } from 'jotai';
import { consoleTokenAtom, consoleEnabledAtom } from '../../store/atoms';
import { useNotification } from '../../contexts/SnackbarContext';
import { buildWsUrl } from '../../utils/constants';
import { TokenDialog } from './TokenDialog';
import { ConsoleControls } from './ConsoleControls';
import { LogViewer } from './LogViewer';
import { CommandInput } from './CommandInput';
import { parseANSILine } from './utils/ansiParser';
import type { LogEntry } from './utils/types';

const MAX_LINES = 1000;

/**
 * Web Console Dashboard
 * Connects to /ws/log for log streaming and optionally /ws/console for interactive shell
 */
export function ConsoleDashboard() {
  const [token, setToken] = useAtom(consoleTokenAtom);
  const [consoleEnabled, setConsoleEnabled] = useAtom(consoleEnabledAtom);
  const { showNotification } = useNotification();

  const [tokenDialogOpen, setTokenDialogOpen] = useState(false);
  const [entries, setEntries] = useState<LogEntry[]>([]);
  const nextId = useRef(0);
  const [autoScroll, setAutoScroll] = useState(true);
  const [consoleBusy, setConsoleBusy] = useState(false);

  // Track if we should connect based on token availability
  const logWsUrl = token ? buildWsUrl('/ws/log', token) : null;
  const consoleWsUrl = consoleEnabled && token ? buildWsUrl('/ws/console', token) : null;

  // Log WebSocket - always connected when token is available
  const {
    lastMessage: logMessage,
    readyState: logReadyState,
    getWebSocket: getLogSocket,
  } = useWebSocket(logWsUrl, {
    shouldReconnect: () => true,
    reconnectAttempts: 10,
    reconnectInterval: (attemptNumber) =>
      Math.min(1000 * Math.pow(2, attemptNumber), 10000),
  });

  // Console WebSocket - connected only when enabled
  const {
    lastMessage: consoleMessage,
    readyState: consoleReadyState,
    sendMessage,
    getWebSocket: getConsoleSocket,
  } = useWebSocket(consoleWsUrl, {
    shouldReconnect: () => false, // manual reconnect only via toggle
    onClose: (event) => {
      const busy = event.reason === 'Console busy' || event.code === 1013;
      setConsoleBusy(busy);
      if (busy) {
        // Flip the switch back to off so the user can retry manually
        setConsoleEnabled(false);
      }
      showNotification({
        message: busy
          ? 'Console already in use by another client. Toggle again to retry.'
          : 'Console disconnected.',
        severity: busy ? 'warning' : 'info',
        duration: 5000,
      });
    },
    onOpen: () => {
      setConsoleBusy(false);
    },
    onError: () => {
      setConsoleBusy(false);
    },
  });

  const appendLines = useCallback((payload: string) => {
    const newLines = payload.split(/\r?\n/).filter((line) => line.length > 0);
    if (newLines.length === 0) return;

    setEntries((prev) => {
      let id = nextId.current;
      const parsedEntries = newLines.map<LogEntry>((line) => ({
        id: id++,
        parsed: parseANSILine(line),
      }));
      nextId.current = id;

      const combined = [...prev, ...parsedEntries];
      if (combined.length > MAX_LINES) {
        return combined.slice(combined.length - MAX_LINES);
      }
      return combined;
    });
  }, []);

  // Process log messages
  useEffect(() => {
    if (!logMessage?.data) return;

    appendLines(logMessage.data as string);
  }, [appendLines, logMessage]);

  // Process console messages (prompts)
  useEffect(() => {
    if (!consoleMessage?.data) return;

    appendLines(consoleMessage.data as string);
  }, [appendLines, consoleMessage]);

  // Open token dialog only when no stored token exists
  // Check localStorage directly because atomWithStorage hydrates async
  useEffect(() => {
    const stored = window.localStorage.getItem('iaq-console-token');
    let storedToken = '';
    if (stored) {
      try {
        storedToken = JSON.parse(stored) as string;
      } catch {
        storedToken = '';
      }
    }
    if (!token && !storedToken) {
      setTokenDialogOpen(true);
    }
  }, [token]);

  // Reset consoleBusy when console is disabled
  useEffect(() => {
    if (!consoleEnabled) {
      setConsoleBusy(false);
    }
  }, [consoleEnabled]);

  // Handlers
  const handleTokenSave = useCallback((newToken: string) => {
    setToken(newToken);
    // Clear logs when token changes (new session)
    setEntries([]);
    nextId.current = 0;
    getLogSocket()?.close(1000, 'token_updated');
    getConsoleSocket()?.close(1000, 'token_updated');
  }, [getConsoleSocket, getLogSocket, setToken]);

  const handleConsoleToggle = useCallback((enabled: boolean) => {
    setConsoleEnabled(enabled);
    if (enabled) {
      setConsoleBusy(false);
    } else {
      setConsoleBusy(false);
      getConsoleSocket()?.close(1000, 'console_disabled');
    }
  }, [getConsoleSocket, setConsoleEnabled]);

  const handleCommandSubmit = useCallback((command: string) => {
    if (consoleReadyState === ReadyState.OPEN) {
      sendMessage(command);
    }
  }, [consoleReadyState, sendMessage]);

  const handleAutoScrollChange = useCallback((enabled: boolean) => {
    setAutoScroll(enabled);
  }, []);

  // Close sockets when leaving the view to free backend slots
  useEffect(() => () => {
    getLogSocket()?.close(1000, 'console_view_exit');
    getConsoleSocket()?.close(1000, 'console_view_exit');
    setConsoleEnabled(false);
  }, [getConsoleSocket, getLogSocket, setConsoleEnabled]);

  return (
    <Container maxWidth="xl" sx={{ py: 3, px: { xs: 2, sm: 3 } }}>
      {/* Header */}
      <Box sx={{ mb: 4 }}>
        <Typography variant="h4" component="h1" fontWeight={600} gutterBottom>
          Web Console
        </Typography>
        <Typography variant="body1" color="text.secondary">
          Real-time device logs and interactive shell
        </Typography>
      </Box>

      {/* Console Card */}
      <Card>
        <CardContent sx={{ p: 2, '&:last-child': { pb: 2 } }}>
          {/* Controls */}
          <ConsoleControls
            logReadyState={logReadyState}
            consoleReadyState={consoleReadyState}
            consoleEnabled={consoleEnabled}
            onConsoleToggle={handleConsoleToggle}
            onTokenClick={() => setTokenDialogOpen(true)}
            consoleBusy={consoleBusy}
          />

          {/* Log viewer */}
          <Box sx={{ mt: 2 }}>
            <LogViewer
              entries={entries}
              autoScroll={autoScroll}
              onAutoScrollChange={handleAutoScrollChange}
            />
          </Box>

          {/* Command input */}
          <Box sx={{ mt: 2 }}>
            <CommandInput
              disabled={consoleReadyState !== ReadyState.OPEN}
              onSubmit={handleCommandSubmit}
            />
          </Box>
        </CardContent>
      </Card>

      {/* Token dialog */}
      <TokenDialog
        open={tokenDialogOpen}
        onClose={() => setTokenDialogOpen(false)}
        token={token}
        onSave={handleTokenSave}
      />
    </Container>
  );
}
