import type { ReadyState } from 'react-use-websocket';

/**
 * ANSI text styling attributes parsed from escape sequences
 */
export interface ANSIStyle {
  /** MUI theme color path (e.g., 'error.main', 'success.main') */
  color?: string;
  /** Bold text */
  bold?: boolean;
}

/**
 * A segment of text with associated ANSI styling
 */
export interface ParsedSegment {
  /** The text content */
  text: string;
  /** Styling to apply */
  style: ANSIStyle;
}

/** A fully parsed log line with all ANSI sequences resolved */
export type ParsedLine = ParsedSegment[];

/**
 * Parsed log entry with stable identity for rendering
 */
export interface LogEntry {
  /** Monotonic id to keep React keys stable as we trim */
  id: number;
  /** Parsed segments for this line */
  parsed: ParsedLine;
}

/**
 * Props for the LogViewer component
 */
export interface LogViewerProps {
  /** Parsed log entries with stable ids */
  entries: LogEntry[];
  /** Whether auto-scroll is currently active */
  autoScroll: boolean;
  /** Callback when auto-scroll state changes */
  onAutoScrollChange: (enabled: boolean) => void;
}

/**
 * Props for the CommandInput component
 */
export interface CommandInputProps {
  /** Whether the input is disabled (console not connected) */
  disabled: boolean;
  /** Callback when a command is submitted */
  onSubmit: (command: string) => void;
}

/**
 * Props for the ConsoleControls component
 */
export interface ConsoleControlsProps {
  /** Current state of the log WebSocket */
  logReadyState: ReadyState;
  /** Current state of the console WebSocket */
  consoleReadyState: ReadyState;
  /** Whether console is enabled (toggle state) */
  consoleEnabled: boolean;
  /** Callback to toggle console enabled state */
  onConsoleToggle: (enabled: boolean) => void;
  /** Callback to open token dialog */
  onTokenClick: () => void;
  /** Whether console was rejected due to "busy" */
  consoleBusy: boolean;
}

/**
 * Props for the TokenDialog component
 */
export interface TokenDialogProps {
  /** Whether the dialog is open */
  open: boolean;
  /** Callback to close the dialog */
  onClose: () => void;
  /** Current token value */
  token: string;
  /** Callback when token is saved */
  onSave: (token: string) => void;
}
