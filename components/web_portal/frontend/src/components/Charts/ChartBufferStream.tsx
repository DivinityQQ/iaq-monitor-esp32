import { useEffect } from 'react';
import { useAtomValue } from 'jotai';
import { stateAtom } from '../../store/atoms';
import { appendStateToBuffers } from '../../utils/streamBuffers';

/**
 * Background streamer that mirrors stateAtom samples into global chart buffers.
 * Mount once at app level so history persists across route changes.
 */
export function ChartBufferStream() {
  const state = useAtomValue(stateAtom);
  useEffect(() => { if (state) appendStateToBuffers(state); }, [state]);
  return null;
}

