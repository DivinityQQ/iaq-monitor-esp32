import { useMemo, type ElementType } from 'react';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Typography from '@mui/material/Typography';
import { useTheme } from '@mui/material/styles';
import { LineChart } from '@mui/x-charts/LineChart';
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
  const xTicks = useMemo(() => buildXAxisTicks(rangeConfig.seconds), [rangeConfig.seconds]);

  const curve: 'catmullRom' = 'catmullRom';
  const seriesColor = resolvePaletteColor(theme, config.color);
  const Icon = METRIC_ICON_MAP[metric];

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
            yAxis={[{
              ...CHART_LAYOUT.yAxis,
              min,
              max,
              valueFormatter: (value: number, context: AxisValueFormatterContext) => {
                if (context.location === 'tick') {
                  return formatAxisTick(value, context.defaultTickLabel);
                }
                return Number.isFinite(value) ? Number(value).toFixed(config.decimals) : '';
              },
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
