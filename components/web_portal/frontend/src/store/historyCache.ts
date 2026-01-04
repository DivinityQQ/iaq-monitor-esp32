import { atom } from 'jotai';
import type { MetricKey, RangeKey } from '../components/Charts/config/chartConfig';

export interface HistoryResponse {
  resolution_s: number;
  end_time: number;
  metrics: Record<MetricKey, {
    scale: number;
    offset: number;
    buckets: ([number, number, number] | null)[];
  }>;
}

export interface CacheEntry {
  response: HistoryResponse;
  fetchedAt: number;
}

export const historyCacheAtom = atom<Map<RangeKey, CacheEntry>>(new Map<RangeKey, CacheEntry>());
export const historyLoadingAtom = atom<Set<RangeKey>>(new Set<RangeKey>());
