export interface ChartDataPoint {
  time: number; // Unix timestamp (seconds)
  avg: number | null;
  min?: number | null;
  max?: number | null;
}
