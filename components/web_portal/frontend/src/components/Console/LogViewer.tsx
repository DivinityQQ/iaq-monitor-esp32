import { useRef, useEffect, useCallback, memo } from 'react';
import Box from '@mui/material/Box';
import Typography from '@mui/material/Typography';
import Chip from '@mui/material/Chip';
import PauseIcon from '@mui/icons-material/Pause';
import { MONOSPACE_FONT } from '../../utils/constants';
import { hasStyle } from './utils/ansiParser';
import type { LogViewerProps, ParsedLine } from './utils/types';

/**
 * Render a single parsed line with ANSI styling
 */
const LogLine = memo(function LogLine({ segments }: { segments: ParsedLine }) {
  return (
    <Box component="span" sx={{ display: 'block', minHeight: '1.4em' }}>
      {segments.map((segment, i) => {
        if (!hasStyle(segment.style)) {
          return <span key={i}>{segment.text}</span>;
        }
        return (
          <Box
            key={i}
            component="span"
            sx={{
              color: segment.style.color,
              fontWeight: segment.style.bold ? 'bold' : undefined,
            }}
          >
            {segment.text}
          </Box>
        );
      })}
    </Box>
  );
});

/**
 * Log viewer component with ANSI color rendering and smart auto-scroll
 * - Parses ANSI escape sequences and renders with MUI theme colors
 * - Also colors ESP_LOG prefix lines (I/W/E/D/V) when no ANSI codes present
 * - Auto-scrolls to bottom unless user scrolls up
 * - Shows indicator when auto-scroll is paused
 */
export const LogViewer = memo(function LogViewer({
  entries,
  autoScroll,
  onAutoScrollChange,
}: LogViewerProps) {
  const containerRef = useRef<HTMLDivElement>(null);

  // Handle scroll events to detect user scrolling
  const handleScroll = useCallback(() => {
    const el = containerRef.current;
    if (!el) return;

    const distanceFromBottom = el.scrollHeight - el.scrollTop - el.clientHeight;
    const pauseThreshold = 12;   // px beyond bottom to pause
    const resumeThreshold = 4;   // px tolerance to resume

    if (distanceFromBottom > pauseThreshold && autoScroll) {
      onAutoScrollChange(false);
    } else if (distanceFromBottom <= resumeThreshold && !autoScroll) {
      onAutoScrollChange(true);
    }
  }, [autoScroll, onAutoScrollChange]);

  // Auto-scroll to bottom when new lines arrive (if enabled)
  useEffect(() => {
    if (autoScroll && containerRef.current) {
      containerRef.current.scrollTop = containerRef.current.scrollHeight;
    }
  }, [entries, autoScroll]);

  // Click handler to resume auto-scroll
  const handleResumeClick = useCallback(() => {
    onAutoScrollChange(true);
    if (containerRef.current) {
      containerRef.current.scrollTop = containerRef.current.scrollHeight;
    }
  }, [onAutoScrollChange]);

  return (
    <Box
      sx={{
        position: 'relative',
        display: 'flex',
        flexDirection: 'column',
        height: '60vh',
        minHeight: 300,
        border: 1,
        borderColor: 'divider',
        borderRadius: 1,
        overflow: 'hidden',
      }}
    >
      {/* Header */}
      <Box
        sx={{
          px: 2,
          py: 1,
          borderBottom: 1,
          borderColor: 'divider',
          bgcolor: 'action.hover',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
        }}
      >
        <Typography variant="subtitle2" color="text.secondary">
          Log Stream
        </Typography>
        {!autoScroll && (
          <Chip
            icon={<PauseIcon />}
            label="Auto-scroll paused"
            size="small"
            color="warning"
            variant="outlined"
            onClick={handleResumeClick}
            sx={{ cursor: 'pointer' }}
          />
        )}
      </Box>

      {/* Log content */}
      <Box
        ref={containerRef}
        onScroll={handleScroll}
        sx={{
          flex: 1,
          overflow: 'auto',
          p: 1.5,
          fontFamily: MONOSPACE_FONT,
          fontSize: '0.8125rem',
          lineHeight: 1.4,
          whiteSpace: 'pre-wrap',
          wordBreak: 'break-all',
          bgcolor: 'background.default',
          // Custom scrollbar
          '&::-webkit-scrollbar': {
            width: 8,
          },
          '&::-webkit-scrollbar-track': {
            bgcolor: 'action.hover',
          },
          '&::-webkit-scrollbar-thumb': {
            bgcolor: 'action.disabled',
            borderRadius: 4,
          },
        }}
      >
        {entries.length === 0 ? (
          <Typography
            variant="body2"
            color="text.disabled"
            sx={{ fontFamily: MONOSPACE_FONT }}
          >
            No logs yet...
          </Typography>
        ) : (
          entries.map((entry) => (
            <LogLine key={entry.id} segments={entry.parsed} />
          ))
        )}
      </Box>
    </Box>
  );
});
