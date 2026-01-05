import { useMemo } from 'react';
import { useAtomValue } from 'jotai';
import { buffersVersionAtom } from '../../../store/atoms';
import { getBuffers, getLatestTimestamp } from '../../../utils/streamBuffers';
import { RANGES, type MetricKey, type RangeKey } from '../config/chartConfig';
import { useHistoryQuery } from './useHistoryQuery';
import type { ChartDataPoint } from '../types';
import { windowToRange } from '../utils/chartUtils';

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

function buildLiveData(metric: MetricKey, rangeSeconds: number): ChartDataPoint[] {
  const { x, y } = getBuffers(metric);
  if (x.length === 0) return [];
  const end = x[x.length - 1];
  const start = end - rangeSeconds;
  const startIdx = binarySearchStart(x, start);

  const points: ChartDataPoint[] = [];
  for (let i = startIdx; i < x.length; i++) {
    points.push({
      time: x[i],
      avg: y[i] == null || Number.isNaN(y[i]) ? null : (y[i] as number),
    });
  }
  return points;
}

export function useChartData(metric: MetricKey, range: RangeKey): {
  data: ChartDataPoint[];
  isLoading: boolean;
  windowEnd: number;
} {
  const rangeConfig = RANGES[range];
  const mergeLiveTail = rangeConfig.useHistory && (rangeConfig.mergeLiveTail ?? true);
  const buffersVersion = useAtomValue(buffersVersionAtom);
  const fallbackNow = useMemo(() => Math.floor(Date.now() / 1000), [buffersVersion]);

  const liveData = useMemo(
    () => buildLiveData(metric, rangeConfig.seconds),
    [buffersVersion, metric, rangeConfig.seconds]
  );

  const latestLiveTime = useMemo(() => getLatestTimestamp(), [buffersVersion]);

  const { data: historyResponse, isLoading } = useHistoryQuery(
    rangeConfig.useHistory ? range : null,
    latestLiveTime
  );

  const historyData = useMemo(() => {
    if (!historyResponse) return null;
    const metricHistory = historyResponse.metrics[metric];
    if (!metricHistory) return null;

    const { scale, offset, buckets } = metricHistory;
    const historyEndTime = historyResponse.end_time;

    const points: ChartDataPoint[] = buckets.map((bucket, index) => {
      const time = historyEndTime - (buckets.length - index - 1) * historyResponse.resolution_s;
      if (!bucket) {
        return { time, avg: null, min: null, max: null };
      }
      const [min, max, avg] = bucket;
      return {
        time,
        avg: (avg - offset) / scale,
        min: (min - offset) / scale,
        max: (max - offset) / scale,
      };
    });

    return { points, historyEndTime };
  }, [historyResponse, metric]);

  const windowEnd = useMemo(() => {
    const baseEnd = latestLiveTime ?? fallbackNow;
    if (!rangeConfig.useHistory) return baseEnd;
    const historyEndTime = historyData?.historyEndTime;
    if (historyEndTime == null) return baseEnd;
    if (!mergeLiveTail) return historyEndTime;
    return Math.max(historyEndTime, latestLiveTime ?? historyEndTime);
  }, [rangeConfig.useHistory, historyData, latestLiveTime, fallbackNow, mergeLiveTail]);

  const data = useMemo(() => {
    if (!rangeConfig.useHistory) {
      return liveData;
    }
    if (!historyResponse) return [];

    if (!historyData) {
      return windowToRange(liveData, windowEnd, rangeConfig.seconds);
    }

    if (!mergeLiveTail) {
      return windowToRange(historyData.points, windowEnd, rangeConfig.seconds);
    }

    // Use >= to avoid 1-point gap at boundary between history and live data
    const liveTail = liveData.filter((point) => point.time >= historyData.historyEndTime);
    const combined = [...historyData.points, ...liveTail];
    return windowToRange(combined, windowEnd, rangeConfig.seconds);
  }, [
    historyResponse,
    historyData,
    liveData,
    rangeConfig.useHistory,
    rangeConfig.seconds,
    latestLiveTime,
    mergeLiveTail,
    windowEnd,
  ]);

  return { data, isLoading, windowEnd };
}
