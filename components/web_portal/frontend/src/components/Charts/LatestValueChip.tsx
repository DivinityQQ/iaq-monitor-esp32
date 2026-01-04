import { useMemo } from 'react';
import Chip from '@mui/material/Chip';
import { useAtomValue } from 'jotai';
import { buffersVersionAtom } from '../../store/atoms';
import { METRICS, type MetricKey } from './config/chartConfig';
import { getLatest } from '../../utils/streamBuffers';

interface LatestValueChipProps {
  metric: MetricKey;
  decimals?: number;
  unitOverride?: string;
}

export function LatestValueChip({ metric, decimals, unitOverride }: LatestValueChipProps) {
  const config = METRICS[metric];
  const precision = decimals ?? config.decimals;
  const unit = unitOverride ?? config.unit;

  // Subscribe to buffersVersionAtom to trigger re-renders when new data arrives
  const version = useAtomValue(buffersVersionAtom);

  // Memoize the value lookup based on version
  const value = useMemo(() => getLatest(metric), [metric, version]);

  const text = value == null || Number.isNaN(value)
    ? '--'
    : `${value.toFixed(precision)}${unit ? ` ${unit}` : ''}`;

  return (
    <Chip
      size="small"
      label={text}
      sx={{ bgcolor: 'action.hover' }}
    />
  );
}
