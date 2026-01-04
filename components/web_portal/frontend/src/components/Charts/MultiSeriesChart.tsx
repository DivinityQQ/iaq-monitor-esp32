import { useMemo } from 'react';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Typography from '@mui/material/Typography';
import { useTheme } from '@mui/material/styles';
import { LineChart } from '@mui/x-charts/LineChart';
import type { AxisValueFormatterContext } from '@mui/x-charts/models';
import GrainIcon from '@mui/icons-material/Grain';
import { LatestValueChip } from './LatestValueChip';
import { METRICS, PM_SERIES, RANGES, type MetricConfig, type RangeKey } from './config/chartConfig';
import { useChartData } from './hooks/useChartData';
import {
  buildXAxisTicks,
  CHART_LAYOUT,
  computeYAxisBounds,
  formatAxisTick,
  createRelativeTimeFormatter,
  resolvePaletteColor,
} from './utils/chartUtils';

interface MultiSeriesChartProps {
  range: RangeKey;
  height?: number;
  title?: string;
}

export function MultiSeriesChart({
  range,
  height = 220,
  title = 'PM',
}: MultiSeriesChartProps) {
  const theme = useTheme();
  const pmConfig = METRICS.pm25_ugm3;
  const rangeConfig = RANGES[range];

  // Fetch all PM series data
  const pm1 = useChartData(PM_SERIES[0], range);
  const pm25 = useChartData(PM_SERIES[1], range);
  const pm10 = useChartData(PM_SERIES[2], range);

  const seriesConfigs = useMemo(() => [
    { metric: PM_SERIES[0], config: METRICS[PM_SERIES[0]], chart: pm1 },
    { metric: PM_SERIES[1], config: METRICS[PM_SERIES[1]], chart: pm25 },
    { metric: PM_SERIES[2], config: METRICS[PM_SERIES[2]], chart: pm10 },
  ], [pm1, pm25, pm10]);

  // Use PM2.5 as the base for x-axis (most common reference)
  const baseData = pm25.data;
  const hasData = baseData.some((point) => point.avg != null);
  const windowEnd = pm25.windowEnd;
  const isLoading = seriesConfigs.some((entry) => entry.chart.isLoading);

  // Relative time formatter for fixed axis labels
  const axisFormatter = useMemo(
    () => createRelativeTimeFormatter(rangeConfig.seconds),
    [rangeConfig.seconds]
  );

  const combinedData = useMemo(
    () => seriesConfigs.flatMap((entry) => entry.chart.data),
    [seriesConfigs]
  );
  const pmYAxisConfig = useMemo<MetricConfig>(() => ({
    id: 'pm25_ugm3',
    label: 'PM',
    unit: 'µg/m³',
    color: 'info.main',
    decimals: 1,
    yMin: 0,
    softYMax: 50,
    source: 'state',
    sourceField: 'pm25_ugm3',
  }), []);
  const { min, max } = useMemo(
    () => computeYAxisBounds(combinedData, pmYAxisConfig),
    [combinedData, pmYAxisConfig]
  );

  // Use relative time values so the axis stays fixed at -range..0
  const dataset = useMemo(() => {
    const rows = new Array(baseData.length);
    for (let i = 0; i < baseData.length; i++) {
      const row: Record<string, number | null> & { t: number } = {
        t: baseData[i].time - windowEnd,
      };
      for (const entry of seriesConfigs) {
        row[entry.metric] = entry.chart.data[i]?.avg ?? null;
      }
      rows[i] = row;
    }
    return rows;
  }, [baseData, seriesConfigs, windowEnd]);
  const xTicks = useMemo(() => buildXAxisTicks(rangeConfig.seconds), [rangeConfig.seconds]);

  // Build series configuration
  const curve: 'catmullRom' = 'catmullRom';

  const series = useMemo(() => {
    return seriesConfigs.map((entry) => ({
      dataKey: entry.metric,
      label: entry.config.label,
      color: resolvePaletteColor(theme, entry.config.color),
      curve,
      showMark: false,
    } as const));
  }, [seriesConfigs, theme, curve]);

  return (
    <Card>
      <CardContent>
        <Box display="flex" justifyContent="space-between" alignItems="center" mb={1}>
          <Box display="flex" alignItems="center" gap={1}>
            <GrainIcon sx={{ color: resolvePaletteColor(theme, pmConfig.color), fontSize: 18 }} />
            <Typography variant="subtitle1">{title}</Typography>
          </Box>
          <LatestValueChip metric="pm25_ugm3" />
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
              tickInterval: xTicks,
              valueFormatter: axisFormatter,
            }]}
            yAxis={[{
              ...CHART_LAYOUT.yAxis,
              min,
              max,
              valueFormatter: (value: number, context: AxisValueFormatterContext) => {
                if (context.location === 'tick') {
                  return formatAxisTick(value, context.defaultTickLabel);
                }
                return Number.isFinite(value) ? Number(value).toFixed(pmConfig.decimals) : '';
              },
            }]}
            series={series}
            height={height}
            margin={CHART_LAYOUT.margin}
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
