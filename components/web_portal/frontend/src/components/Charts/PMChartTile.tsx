import { useEffect, useMemo, useRef } from 'react';
import { useAtomValue } from 'jotai';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import Typography from '@mui/material/Typography';
import { useMediaQuery, useTheme } from '@mui/material';
import { getBuffers, getLatest } from '../../utils/streamBuffers';
import { buffersVersionAtom } from '../../store/atoms';
import type { RangeSeconds } from './ChartTile';

/**
 * Binary search to find first index where x[i] >= target
 * O(log n) instead of O(n) linear scan
 */
function binarySearchStart(arr: number[], target: number): number {
  let left = 0;
  let right = arr.length;
  while (left < right) {
    const mid = (left + right) >>> 1;
    if (arr[mid] < target) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return left;
}

interface PMChartTileProps {
  range: RangeSeconds;
  height?: number;
  yMin?: number;
  softYMax?: number; // expand if exceeded
}

export function PMChartTile({ range, height = 220, yMin, softYMax }: PMChartTileProps) {
  const theme = useTheme();
  const isMobile = useMediaQuery(theme.breakpoints.down('sm'));
  const buffersVersion = useAtomValue(buffersVersionAtom);

  const containerRef = useRef<HTMLDivElement>(null);
  const plotRef = useRef<uPlot | null>(null);
  const yScaleRef = useRef<{ min?: number; max?: number }>({});
  const prevRangeRef = useRef(range);

  // Get buffers once on mount, refresh in update effect
  const buffersRef = useRef({
    x: getBuffers('pm25_ugm3').x,
    pm1: getBuffers('pm1_ugm3').y,
    pm25: getBuffers('pm25_ugm3').y,
    pm10: getBuffers('pm10_ugm3').y,
  });

  const options = useMemo<uPlot.Options>(() => {
    const axisGrid = { show: true, stroke: theme.palette.divider, width: 1 } as const;
    return {
      width: containerRef.current?.clientWidth || 800,
      height,
      cursor: { sync: { key: 'realtime' } },
      scales: {
        x: {
          range(_u, dataMin, dataMax) {
            // Provide default range when no data
            if (dataMin == null || dataMax == null) {
              const now = Date.now() / 1000;
              return [now - range, now];
            }
            return [dataMin, dataMax];
          },
        },
        y: {
          auto: yMin === undefined && softYMax === undefined,
          range(_u, dataMin, dataMax) {
            // Provide default range when no data
            if (dataMin == null || dataMax == null) {
              return [yMin ?? 0, softYMax ?? 50];
            }
            return [dataMin, dataMax];
          },
        },
      },
      series: [
        { label: 'Time' },
        { label: 'PM1.0', stroke: theme.palette.info.main, width: 2, points: { show: false }, paths: range >= 300 && uPlot.paths.spline ? uPlot.paths.spline() : undefined },
        { label: 'PM2.5', stroke: theme.palette.error.main, width: 2, points: { show: false }, paths: range >= 300 && uPlot.paths.spline ? uPlot.paths.spline() : undefined },
        { label: 'PM10', stroke: theme.palette.warning.main, width: 2, points: { show: false }, paths: range >= 300 && uPlot.paths.spline ? uPlot.paths.spline() : undefined },
      ],
      axes: [
        {
          grid: axisGrid,
          space: (_u, _axisIdx, _min, _max, _plotDim) => (range <= 90 ? (isMobile ? 64 : 40) : range <= 600 ? (isMobile ? 80 : 56) : (isMobile ? 72 : 48)),
          incrs: range <= 90
            ? [1, 2, 5, 10, 15, 20, 30]
            : range <= 600
            ? [5, 10, 15, 30, 60, 120, 300]
            : [30, 60, 120, 300, 600, 900],
          splits: (_u, _axisIdx, _min, max, foundIncr) => {
            const end = max as number;
            const start = end - range;
            const step = foundIncr || (range <= 90 ? 5 : range <= 600 ? 30 : 300);
            const ticks: number[] = [];
            for (let v = end; v >= start - 1e-6; v -= step) ticks.push(v);
            return ticks.reverse();
          },
          values: (u, values) => {
            const end = (u.scales.x.max as number) ?? 0; const fmt = (d: number) => {
              const s = Math.max(0, Math.round(d));
              if (range <= 90) return `-${s}s`;
              if (range <= 600) { const m = Math.floor(s / 60); const ss = String(s % 60).padStart(2, '0'); return `-${m}:${ss}`; }
              const m = Math.round(s / 60); return `-${m}m`;
            };
            return values.map(v => fmt(end - (v as number)));
          },
        },
        { scale: 'y', side: 3, grid: axisGrid, values: (_u, vals) => vals.map(v => (Number.isFinite(v) ? v.toFixed(0) : '')) },
      ],
      // Show uPlot legend (non-interactive by default)
      legend: { show: true, live: false },
    };
  }, [theme.palette.divider, theme.palette.error.main, theme.palette.info.main, theme.palette.warning.main, theme.breakpoints, isMobile, range, height, yMin, softYMax]);

  useEffect(() => {
    if (!containerRef.current) return;

    // Refresh buffers
    buffersRef.current = {
      x: getBuffers('pm25_ugm3').x,
      pm1: getBuffers('pm1_ugm3').y,
      pm25: getBuffers('pm25_ugm3').y,
      pm10: getBuffers('pm10_ugm3').y,
    };

    const plot = new uPlot(
      options,
      [buffersRef.current.x, buffersRef.current.pm1, buffersRef.current.pm25, buffersRef.current.pm10],
      containerRef.current
    );
    plotRef.current = plot;

    if (yMin !== undefined || softYMax !== undefined) {
      yScaleRef.current = { min: yMin, max: softYMax };
      const init: any = {};
      if (yMin !== undefined) init.min = yMin;
      if (softYMax !== undefined) init.max = softYMax;
      plot.setScale('y', init);
    }

    const ro = new ResizeObserver(entries => {
      const w = entries[0].contentRect.width;
      if (w && plotRef.current) {
        plotRef.current.setSize({ width: Math.round(w), height: options.height! });
      }
    });
    ro.observe(containerRef.current);

    return () => {
      ro.disconnect();
      plot.destroy();
      plotRef.current = null;
    };
  }, [options, yMin, softYMax]);

  // Reset y scale expansion when range changes
  useEffect(() => {
    if (prevRangeRef.current !== range) {
      yScaleRef.current = {};
      prevRangeRef.current = range;
    }
  }, [range]);

  // Event-driven update: only when data changes (buffersVersion) or range changes
  useEffect(() => {
    const plot = plotRef.current;
    if (!plot) return;

    // Refresh buffers
    buffersRef.current = {
      x: getBuffers('pm25_ugm3').x,
      pm1: getBuffers('pm1_ugm3').y,
      pm25: getBuffers('pm25_ugm3').y,
      pm10: getBuffers('pm10_ugm3').y,
    };

    const xs = buffersRef.current.x;

    // Even with empty data, set scales to show axes
    if (xs.length === 0) {
      const now = Date.now() / 1000;
      plot.setData([[], [], [], []], true);
      plot.setScale('x', { min: now - range, max: now });
      plot.setScale('y', { min: yMin ?? 0, max: softYMax ?? 50 });
      return;
    }

    const end = xs[xs.length - 1];
    const start = end - range;

    // Binary search for window start (O(log n) instead of O(n))
    const startIdx = binarySearchStart(xs, start);

    // Pass full arrays to uPlot and constrain via scale
    // No slice() allocations
    plot.setData([xs, buffersRef.current.pm1, buffersRef.current.pm25, buffersRef.current.pm10], true);
    plot.setScale('x', { min: start, max: end });

    // Expand y max if needed across all 3 series
    if (yMin !== undefined && softYMax !== undefined) {
      let dataMax = -Infinity;

      // Check max only over visible range
      const check = (arr: (number | null)[]) => {
        for (let k = startIdx; k < arr.length; k++) {
          const v = arr[k];
          if (v != null && !Number.isNaN(v) && v > dataMax) dataMax = v;
        }
      };

      check(buffersRef.current.pm1);
      check(buffersRef.current.pm25);
      check(buffersRef.current.pm10);

      const base = softYMax;
      const targetMax = dataMax === -Infinity ? base : Math.max(base, dataMax);
      const pad = Math.max(1, (targetMax - yMin) * 0.05);
      const finalMax = targetMax + pad;

      // Only expand, don't shrink
      if (yScaleRef.current.max === undefined || finalMax > (yScaleRef.current.max ?? 0) + 0.1) {
        plot.setScale('y', { min: yMin, max: finalMax });
        yScaleRef.current = { min: yMin, max: finalMax };
      }
    }
  }, [buffersVersion, range, yMin, softYMax]);

  const latest = getLatest('pm25_ugm3');
  const latestText = latest == null ? '--' : `${latest.toFixed(1)} µg/m³`;

  const buffers = getBuffers('pm25_ugm3');
  const hasData = buffers.x.length > 0;

  // no external toggles; legend is display-only

  return (
    <Card>
      <CardContent>
        <Box display="flex" justifyContent="space-between" alignItems="center" mb={1}>
          <Typography variant="subtitle1">PM</Typography>
          <Chip size="small" label={latestText} sx={{ bgcolor: theme.palette.action.hover }} />
        </Box>
        <Box sx={{ width: '100%', height: height + 10, position: 'relative' }}>
          <Box
            ref={containerRef}
            sx={{
              width: '100%',
              height: '100%',
              '& .u-legend': {
                pointerEvents: 'none',
                cursor: 'default',
              },
            }}
          />
          {!hasData && (
            <Box
              sx={{
                position: 'absolute',
                top: '50%',
                left: '50%',
                transform: 'translate(-50%, -50%)',
                color: 'text.disabled',
                fontSize: '0.875rem',
                pointerEvents: 'none',
              }}
            >
              No data available
            </Box>
          )}
        </Box>
      </CardContent>
    </Card>
  );
}
