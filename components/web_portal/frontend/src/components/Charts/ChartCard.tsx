import { useMemo, type ElementType } from 'react';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Typography from '@mui/material/Typography';
import { alpha, useTheme } from '@mui/material/styles';
import { LineChart, type LineSeries } from '@mui/x-charts/LineChart';
import type { AxisValueFormatterContext } from '@mui/x-charts/models';
import ThermostatIcon from '@mui/icons-material/Thermostat';
import WaterDropIcon from '@mui/icons-material/WaterDrop';
import Co2Icon from '@mui/icons-material/Co2';
import GrainIcon from '@mui/icons-material/Grain';
import CloudIcon from '@mui/icons-material/Cloud';
import ScienceIcon from '@mui/icons-material/Science';
import GrassIcon from '@mui/icons-material/Grass';
import AirIcon from '@mui/icons-material/Air';
import SentimentSatisfiedAltIcon from '@mui/icons-material/SentimentSatisfiedAlt';
import HealthAndSafetyIcon from '@mui/icons-material/HealthAndSafety';
import { LatestValueChip } from './LatestValueChip';
import { METRICS, RANGES, type MetricKey, type RangeKey } from './config/chartConfig';
import { useChartData } from './hooks/useChartData';
import {
  buildXAxisTicks,
  CHART_LAYOUT,
  computeYAxisBounds,
  formatAxisTick,
  createRelativeTimeFormatter,
  resolvePaletteColor,
} from './utils/chartUtils';

const METRIC_ICON_MAP: Record<MetricKey, ElementType> = {
  temp_c: ThermostatIcon,
  rh_pct: WaterDropIcon,
  co2_ppm: Co2Icon,
  pressure_hpa: CloudIcon,
  voc_index: ScienceIcon,
  nox_index: ScienceIcon,
  mold_risk: GrassIcon,
  aqi: AirIcon,
  comfort_score: SentimentSatisfiedAltIcon,
  iaq_score: HealthAndSafetyIcon,
  pm25_ugm3: GrainIcon,
  pm1_ugm3: GrainIcon,
  pm10_ugm3: GrainIcon,
};

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
  const showMinMax = rangeConfig.useHistory && rangeConfig.showMinMax === true;
  const hasMinMax = showMinMax && data.some((point) => point.min != null && point.max != null);

  // Relative time formatter for fixed axis labels
  const axisFormatter = useMemo(
    () => createRelativeTimeFormatter(rangeConfig.seconds),
    [rangeConfig.seconds]
  );

  const { min, max } = useMemo(
    () => computeYAxisBounds(data, config, hasMinMax),
    [data, config, hasMinMax]
  );
  // Use relative time values so the axis stays fixed at -range..0
  const dataset = useMemo(
    () => data.map((point) => {
      const row: { t: number; avg: number | null; min?: number | null; band?: number | null } = {
        t: point.time - windowEnd,
        avg: point.avg,
      };
      if (hasMinMax) {
        const minValue = typeof point.min === 'number' && Number.isFinite(point.min) ? point.min : null;
        const maxValue = typeof point.max === 'number' && Number.isFinite(point.max) ? point.max : null;
        row.min = minValue;
        if (minValue != null && maxValue != null) {
          const diff = maxValue - minValue;
          row.band = Number.isFinite(diff) && diff >= 0 ? diff : null;
        } else {
          row.band = null;
        }
      }
      return row;
    }),
    [data, hasMinMax, windowEnd]
  );
  const xTicks = useMemo(() => buildXAxisTicks(rangeConfig.seconds), [rangeConfig.seconds]);

  const curve: 'monotoneX' = 'monotoneX';
  const seriesColor = resolvePaletteColor(theme, config.color);
  const bandColor = useMemo(() => alpha(seriesColor, 0.12), [seriesColor]);
  const Icon = METRIC_ICON_MAP[metric];
  const series = useMemo<LineSeries[]>(() => {
    const formatValue = (value: number | null) => {
      if (value == null || !Number.isFinite(value)) return null;
      return value.toFixed(config.decimals);
    };
    const baseSeries: LineSeries[] = [{
      dataKey: 'avg',
      label: 'Avg',
      color: seriesColor,
      curve,
      showMark: false,
      disableHighlight: false,
      valueFormatter: (value: number | null) => formatValue(value),
    }];
    if (!hasMinMax) return baseSeries;
    const bandSeries: LineSeries = {
      dataKey: 'band',
      stack: 'range',
      stackOrder: 'reverse',
      area: true,
      color: bandColor,
      curve,
      showMark: false,
      disableHighlight: true,
      label: 'Max',
      valueFormatter: (_value: number | null, context: { dataIndex: number }) => {
        const row = dataset[context.dataIndex];
        if (!row || row.min == null || row.band == null) return null;
        const maxValue = row.min + row.band;
        return formatValue(maxValue);
      },
    };
    const minSeries: LineSeries = {
      dataKey: 'min',
      stack: 'range',
      color: bandColor,
      curve,
      showMark: false,
      disableHighlight: true,
      label: 'Min',
      valueFormatter: (value: number | null) => formatValue(value),
    };
    return [bandSeries, ...baseSeries, minSeries];
  }, [bandColor, config.decimals, curve, dataset, hasMinMax, seriesColor]);

  const yAxis = useMemo(() => [{
    ...CHART_LAYOUT.yAxis,
    min,
    max,
    valueFormatter: (value: number, context: AxisValueFormatterContext) => {
      if (context.location === 'tick') {
        return formatAxisTick(value, context.defaultTickLabel);
      }
      return Number.isFinite(value) ? value.toFixed(config.decimals) : '';
    },
  }], [min, max, config.decimals]);

  return (
    <Card>
      <CardContent>
        <Box display="flex" justifyContent="space-between" alignItems="center" mb={1}>
          <Box display="flex" alignItems="center" gap={1}>
            <Icon sx={{ color: seriesColor, fontSize: 18 }} />
            <Typography variant="subtitle1">{config.label}</Typography>
          </Box>
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
              tickInterval: xTicks,
              valueFormatter: axisFormatter,
            }]}
            yAxis={yAxis}
            series={series}
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
