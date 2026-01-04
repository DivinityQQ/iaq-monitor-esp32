import { useMemo } from 'react';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Typography from '@mui/material/Typography';
import { useTheme } from '@mui/material/styles';
import { LineChart } from '@mui/x-charts/LineChart';
import { LatestValueChip } from './LatestValueChip';
import { METRICS, RANGES, type MetricKey, type RangeKey } from './config/chartConfig';
import { useChartData } from './hooks/useChartData';
import { CHART_LAYOUT, computeYAxisBounds, createRelativeTimeFormatter, resolvePaletteColor } from './utils/chartUtils';

interface ChartCardProps {
  metric: MetricKey;
  range: RangeKey;
  height?: number;
}

export function ChartCard({ metric, range, height = 220 }: ChartCardProps) {
  const theme = useTheme();
  const config = METRICS[metric];
  const rangeConfig = RANGES[range];
  const { data, isLoading, windowEnd } = useChartData(metric, range);
  const hasData = data.some((point) => point.avg != null);

  // Relative time formatter for fixed axis labels
  const axisFormatter = useMemo(
    () => createRelativeTimeFormatter(rangeConfig.seconds),
    [rangeConfig.seconds]
  );

  const { min, max } = useMemo(() => computeYAxisBounds(data, config), [data, config]);
  // Use relative time values so the axis stays fixed at -range..0
  const dataset = useMemo(
    () => data.map((point) => ({
      t: point.time - windowEnd,
      avg: point.avg,
    })),
    [data, windowEnd]
  );

  const curve = rangeConfig.seconds >= 300 ? 'catmullRom' : 'linear';
  const seriesColor = resolvePaletteColor(theme, config.color);

  return (
    <Card>
      <CardContent>
        <Box display="flex" justifyContent="space-between" alignItems="center" mb={1}>
          <Typography variant="subtitle1">{config.label}</Typography>
          <LatestValueChip metric={metric} />
        </Box>
        <Box sx={{ width: '100%', height: height + 10, position: 'relative' }}>
          <LineChart
            skipAnimation
            disableAxisListener
            dataset={dataset}
            xAxis={[{
              ...CHART_LAYOUT.xAxis,
              dataKey: 't',
              min: -rangeConfig.seconds,
              max: 0,
              scaleType: 'linear',
              domainLimit: 'strict',
              valueFormatter: axisFormatter,
            }]}
            yAxis={[{
              ...CHART_LAYOUT.yAxis,
              min,
              max,
              valueFormatter: (v: number) => (Number.isFinite(v) ? Number(v).toFixed(config.decimals) : ''),
            }]}
            series={[{
              dataKey: 'avg',
              color: seriesColor,
              curve,
              showMark: false,
            }]}
            height={height}
            margin={CHART_LAYOUT.margin}
            hideLegend
          />
          {!hasData && !isLoading && (
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
