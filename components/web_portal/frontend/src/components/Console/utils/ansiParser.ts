import type { ANSIStyle, ParsedSegment, ParsedLine } from './types';

/**
 * Map ANSI color codes to MUI theme color paths
 * Standard colors (30-37) and bright variants (90-97)
 */
const ANSI_COLORS: Record<number, string> = {
  // Standard foreground colors (30-37)
  30: 'text.primary',       // Black → use primary text (theme-aware)
  31: 'error.main',         // Red
  32: 'success.main',       // Green
  33: 'warning.main',       // Yellow
  34: 'info.main',          // Blue
  35: 'secondary.main',     // Magenta
  36: 'info.light',         // Cyan
  37: 'text.secondary',     // White → use secondary text (theme-aware)

  // Bright foreground colors (90-97)
  90: 'text.disabled',      // Bright Black (gray)
  91: 'error.light',        // Bright Red
  92: 'success.light',      // Bright Green
  93: 'warning.light',      // Bright Yellow
  94: 'info.light',         // Bright Blue
  95: 'secondary.light',    // Bright Magenta
  96: 'info.light',         // Bright Cyan
  97: 'common.white',       // Bright White
};

/**
 * Parse ANSI SGR (Select Graphic Rendition) parameters
 * Returns the resulting style after applying all parameters
 */
function parseANSIParams(params: string, currentStyle: ANSIStyle): ANSIStyle {
  if (!params || params === '0') {
    // Reset all attributes
    return {};
  }

  const codes = params.split(';').map(Number);
  let newStyle = { ...currentStyle };

  for (const code of codes) {
    if (code === 0) {
      // Reset - clear style but continue processing remaining codes
      newStyle = {};
    } else if (code === 1) {
      // Bold
      newStyle.bold = true;
    } else if (code === 22) {
      // Normal intensity (not bold)
      newStyle.bold = false;
    } else if (code >= 30 && code <= 37) {
      // Standard foreground color
      newStyle.color = ANSI_COLORS[code];
    } else if (code === 39) {
      // Default foreground color
      delete newStyle.color;
    } else if (code >= 90 && code <= 97) {
      // Bright foreground color
      newStyle.color = ANSI_COLORS[code];
    }
    // Ignore background colors (40-47, 100-107) and other codes
  }

  return newStyle;
}

/**
 * Parse a single line of text containing ANSI escape sequences
 * Returns an array of segments with their associated styles
 */
export function parseANSILine(line: string): ParsedLine {
  const segments: ParsedSegment[] = [];
  let currentStyle: ANSIStyle = {};
  let lastIndex = 0;

  // Create fresh regex for each call to avoid lastIndex issues
  const ansiRegex = /\x1b\[([0-9;]*)m/g;

  let match: RegExpExecArray | null;
  while ((match = ansiRegex.exec(line)) !== null) {
    // Add text before this escape sequence
    if (match.index > lastIndex) {
      const text = line.slice(lastIndex, match.index);
      if (text) {
        segments.push({ text, style: { ...currentStyle } });
      }
    }

    // Update style based on escape sequence
    currentStyle = parseANSIParams(match[1], currentStyle);
    lastIndex = match.index + match[0].length;
  }

  // Add remaining text after last escape sequence
  if (lastIndex < line.length) {
    const text = line.slice(lastIndex);
    if (text) {
      segments.push({ text, style: { ...currentStyle } });
    }
  }

  // If line is empty or only escape sequences, return empty segment
  if (segments.length === 0) {
    segments.push({ text: line || '', style: {} });
  }

  return segments;
}

/**
 * Check if a style object has any active styles
 */
export function hasStyle(style: ANSIStyle): boolean {
  return !!(style.color || style.bold);
}

/**
 * Strip all ANSI escape sequences from a string
 * Useful for plain text operations (search, copy)
 */
export function stripANSI(text: string): string {
  return text.replace(/\x1b\[([0-9;]*)m/g, '');
}
