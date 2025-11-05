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
import { getBuffers, getLatest, MetricKey } from '../../utils/streamBuffers';
import { buffersVersionAtom } from '../../store/atoms';

export type RangeSeconds = 60 | 300 | 3600;

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

interface ChartTileProps {
  title: string;
  color: string;
  unitSuffix: string; // e.g. "°C", "%", "ppm"
  metric: MetricKey;
  range: RangeSeconds;
  height?: number;
  yMin?: number;
  yMax?: number;
  /** Start with this max but expand if data exceeds it (won't shrink) */
  softYMax?: number;
  decimals?: number;
}

/**
 * Lightweight single‑series streaming tile built on uPlot.
 * Maintains up to 1h of samples in a ring buffer and renders a subset
 * according to the selected range.
 */
export function ChartTile({
  title,
  color,
  unitSuffix,
  metric,
  range,
  height = 220,
  yMin,
  yMax,
  softYMax,
  decimals = 1,
}: ChartTileProps) {
  const theme = useTheme();
  const isMobile = useMediaQuery(theme.breakpoints.down('sm'));
  const buffersVersion = useAtomValue(buffersVersionAtom);

  const containerRef = useRef<HTMLDivElement>(null);
  const plotRef = useRef<uPlot | null>(null);

  // Buffers are global and persist across route changes
  const buffersRef = useRef(getBuffers(metric));
  const yScaleRef = useRef<{ min?: number; max?: number }>({});
  const prevRangeRef = useRef(range);

  // Create plot
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
          auto: yMin === undefined || (yMax === undefined && softYMax === undefined),
          range(_u, dataMin, dataMax) {
            // Provide default range when no data
            if (dataMin == null || dataMax == null) {
              return [yMin ?? 0, yMax ?? softYMax ?? 100];
            }
            return [dataMin, dataMax];
          },
        },
      },
      series: [
        { label: 'Time' },
        {
          label: title,
          stroke: color,
          width: 2,
          points: { show: false },
          scale: 'y',
          paths: range >= 300 && uPlot.paths.spline ? uPlot.paths.spline() : undefined,
          value: (_u, v) => (v == null || Number.isNaN(v) ? '--' : `${(v as number).toFixed(decimals)} ${unitSuffix}`),
        },
      ],
      axes: [
        {
          grid: axisGrid,
          // Let uPlot compute the increment from minSpace+incrs, then anchor ticks to "now" (end)
          space: (_u, _axisIdx, _min, _max, _plotDim) => {
            // Minimum pixel spacing per label by range (labels are longer in mm:ss)
            return range <= 90 ? (isMobile ? 64 : 40) : range <= 600 ? (isMobile ? 80 : 56) : (isMobile ? 72 : 48);
          },
          incrs: range <= 90
            ? [1, 2, 5, 10, 15, 20, 30]
            : range <= 600
            ? [5, 10, 15, 30, 60, 120, 300]
            : [30, 60, 120, 300, 600, 900],
          // keep ticks anchored to the right edge ("now"); choose step by inner width
          splits: (_u, _axisIdx, _min, max, foundIncr) => {
            const end = max as number;
            const start = end - range;
            const step = foundIncr || (range <= 90 ? 5 : range <= 600 ? 30 : 300);
            const ticks: number[] = [];
            for (let v = end; v >= start - 1e-6; v -= step) ticks.push(v);
            return ticks.reverse();
          },
          values: (u, vals) => {
            const end = (u.scales.x.max as number) ?? 0;
            const fmt = (d: number) => {
              const s = Math.max(0, Math.round(d));
              if (range <= 90) return `-${s}s`;
              if (range <= 600) {
                const m = Math.floor(s / 60);
                const ss = String(s % 60).padStart(2, '0');
                return `-${m}:${ss}`;
              }
              const m = Math.round(s / 60);
              return `-${m}m`;
            };
            return vals.map(v => fmt(end - (v as number)));
          },
        },
        {
          scale: 'y',
          side: 3,
          grid: axisGrid,
          values: (_u, vals) => vals.map(v => (Number.isFinite(v) ? v.toFixed(0) : '')),
        },
      ],
      legend: { show: false },
    };
  }, [theme.palette.divider, theme.breakpoints, isMobile, color, height, title, unitSuffix, yMin, yMax, softYMax, decimals, range]);

  useEffect(() => {
    if (!containerRef.current) return;
    buffersRef.current = getBuffers(metric);
    const initialData: uPlot.AlignedData = [buffersRef.current.x, buffersRef.current.y];
    const plot = new uPlot(options, initialData, containerRef.current);
    plotRef.current = plot;

    // Fix y-range if provided
    if (yMin !== undefined) yScaleRef.current.min = yMin;
    if (yMax !== undefined) yScaleRef.current.max = yMax;
    if (yMin !== undefined && yMax !== undefined) plot.setScale('y', { min: yMin, max: yMax });

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
  }, [options, metric, yMin, yMax]);

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

    // Refresh buffers reference
    buffersRef.current = getBuffers(metric);
    const xs = buffersRef.current.x;
    const ys = buffersRef.current.y;

    // Even with empty data, set scales to show axes
    if (xs.length === 0) {
      const now = Date.now() / 1000;
      plot.setData([[], []], true);
      plot.setScale('x', { min: now - range, max: now });
      plot.setScale('y', { min: yMin ?? 0, max: yMax ?? softYMax ?? 100 });
      return;
    }

    const end = xs[xs.length - 1];
    const start = end - range;

    // Binary search for window start (O(log n) instead of O(n))
    const startIdx = binarySearchStart(xs, start);

    // Pass full arrays to uPlot and constrain via scale
    // No slice() allocation - uPlot will render only visible range
    plot.setData([xs, ys], true);
    plot.setScale('x', { min: start, max: end });

    // Y scaling logic
    if (yMin !== undefined && yMax !== undefined) {
      // Fixed min/max
      plot.setScale('y', { min: yMin, max: yMax });
    } else if (yMin !== undefined && softYMax !== undefined) {
      // Fixed min, expandable max
      // Compute max only over visible range
      let dataMax = -Infinity;
      for (let k = startIdx; k < ys.length; k++) {
        const v = ys[k] as number | null;
        if (v != null && !Number.isNaN(v) && v > dataMax) dataMax = v;
      }
      const base = softYMax;
      const targetMax = dataMax === -Infinity ? base : Math.max(base, dataMax);
      const pad = Math.max(1, (targetMax - yMin) * 0.05);
      const finalMax = targetMax + pad;

      // Only expand, don't shrink (prevents jitter)
      if (yScaleRef.current.max === undefined || finalMax > (yScaleRef.current.max ?? 0) + 0.1) {
        plot.setScale('y', { min: yMin, max: finalMax });
        yScaleRef.current = { min: yMin, max: finalMax };
      }
    }
  }, [buffersVersion, range, metric, yMin, yMax, softYMax]);

  const latest = getLatest(metric);
  const latestText = latest == null || Number.isNaN(latest)
    ? '--'
    : `${(latest as number).toFixed(decimals)} ${unitSuffix}`;

  const buffers = getBuffers(metric);
  const hasData = buffers.x.length > 0;

  return (
    <Card>
      <CardContent>
        <Box display="flex" justifyContent="space-between" alignItems="center" mb={1}>
          <Typography variant="subtitle1">{title}</Typography>
          <Chip size="small" label={latestText} sx={{ bgcolor: theme.palette.action.hover }} />
        </Box>
        <Box sx={{ width: '100%', height: height + 10, position: 'relative' }}>
          <Box ref={containerRef} sx={{ width: '100%', height: '100%' }} />
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
