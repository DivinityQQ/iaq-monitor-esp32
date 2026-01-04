import { useEffect, useState } from 'react';
import { useAtom } from 'jotai';
import { METRICS, RANGES, type MetricKey, type RangeKey } from '../config/chartConfig';
import { historyCacheAtom, historyLoadingAtom, type HistoryResponse } from '../../../store/historyCache';
import { STREAM_BUFFER_SECONDS } from '../../../utils/streamBuffers';

const inFlightRanges = new Set<RangeKey>();

/* Binary protocol constants */
const HIST_BIN_MAGIC = 0x01514149;
const SENTINEL = -32768;
const MAX_BUCKETS = 10080; /* Sanity limit, not tied to backend config */

const METRIC_ID_TO_KEY: MetricKey[] = [
  'temp_c', 'rh_pct', 'co2_ppm', 'pressure_hpa',
  'pm1_ugm3', 'pm25_ugm3', 'pm10_ugm3',
  'voc_index', 'nox_index', 'mold_risk',
  'aqi', 'comfort_score', 'iaq_score',
];

function decodeHistoryBinary(buffer: ArrayBuffer): HistoryResponse {
  const view = new DataView(buffer);
  const len = buffer.byteLength;

  if (len < 16) {
    throw new Error('Response too short for header');
  }

  const magic = view.getUint32(0, true);
  if (magic !== HIST_BIN_MAGIC) {
    throw new Error(`Invalid magic: 0x${magic.toString(16)}`);
  }

  const resolution_s = view.getUint32(4, true);
  const end_time = view.getUint32(8, true);
  const metricCount = view.getUint16(12, true);
  const bucketCount = view.getUint16(14, true);

  if (metricCount === 0 || metricCount > METRIC_ID_TO_KEY.length) {
    throw new Error(`Invalid metric count: ${metricCount}`);
  }
  if (bucketCount > MAX_BUCKETS) {
    throw new Error(`Invalid bucket count: ${bucketCount}`);
  }

  const expectedLen = 16 + 6 * metricCount + 6 * metricCount * bucketCount;
  if (!Number.isSafeInteger(expectedLen) || len !== expectedLen) {
    throw new Error(`Invalid length: got ${len}, expected ${expectedLen}`);
  }

  let offset = 16;

  /* Read metric descriptors */
  const descriptors: Array<{ key: MetricKey; scale: number; offset: number }> = [];
  for (let i = 0; i < metricCount; i++) {
    const id = view.getUint8(offset);
    if (id >= METRIC_ID_TO_KEY.length) {
      throw new Error(`Invalid metric ID: ${id}`);
    }
    offset += 2; // id + flags
    const scale = view.getInt16(offset, true); offset += 2;
    const metricOffset = view.getInt16(offset, true); offset += 2;
    descriptors.push({ key: METRIC_ID_TO_KEY[id], scale, offset: metricOffset });
  }

  /* Read bucket data */
  const metrics = {} as HistoryResponse['metrics'];
  for (const desc of descriptors) {
    const buckets: ([number, number, number] | null)[] = [];
    for (let b = 0; b < bucketCount; b++) {
      const min = view.getInt16(offset, true);
      const max = view.getInt16(offset + 2, true);
      const avg = view.getInt16(offset + 4, true);
      offset += 6;
      buckets.push(min === SENTINEL ? null : [min, max, avg]);
    }
    metrics[desc.key] = { scale: desc.scale, offset: desc.offset, buckets };
  }

  return { resolution_s, end_time, metrics };
}

interface UseHistoryQueryResult {
  data: HistoryResponse | null;
  isLoading: boolean;
  error: Error | null;
}

export function useHistoryQuery(range: RangeKey | null, latestLiveTime: number | null): UseHistoryQueryResult {
  const [cache, setCache] = useAtom(historyCacheAtom);
  const [loading, setLoading] = useAtom(historyLoadingAtom);
  const [error, setError] = useState<Error | null>(null);

  const shouldFetch = range ? RANGES[range].useHistory : false;
  const cached = range ? cache.get(range) : null;
  const isStale = !!(
    cached &&
    cached.response.end_time > 0 &&
    latestLiveTime != null &&
    Number.isFinite(latestLiveTime) &&
    (latestLiveTime - cached.response.end_time) > STREAM_BUFFER_SECONDS
  );

  useEffect(() => {
    if (!range || !shouldFetch || loading.has(range)) return;
    if (cached && !isStale) return;
    if (inFlightRanges.has(range)) return;

    const fetchHistory = async () => {
      inFlightRanges.add(range);
      setLoading((prev: Set<RangeKey>) => new Set(prev).add(range));
      setError(null);
      try {
        const params = new URLSearchParams({
          metrics: Object.keys(METRICS).join(','),
          range,
        });
        const res = await fetch(`/api/v1/history?${params}`);
        if (!res.ok) {
          throw new Error(`History request failed (${res.status})`);
        }
        const buffer = await res.arrayBuffer();
        const response = decodeHistoryBinary(buffer);
        setCache((prev: Map<RangeKey, { response: HistoryResponse; fetchedAt: number }>) =>
          new Map(prev).set(range, { response, fetchedAt: Date.now() })
        );
      } catch (err) {
        setError(err as Error);
      } finally {
        setLoading((prev: Set<RangeKey>) => {
          const next = new Set(prev);
          next.delete(range);
          return next;
        });
        inFlightRanges.delete(range);
      }
    };

    fetchHistory();
  }, [range, shouldFetch, cached, isStale, latestLiveTime, loading, setCache, setLoading]);

  return {
    data: cached?.response ?? null,
    isLoading: range ? loading.has(range) : false,
    error,
  };
}
