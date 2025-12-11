import { useEffect, useRef } from 'react';
import { useAtomValue } from 'jotai';
import type { State } from '../../api/types';
import { stateAtom } from '../../store/atoms';
import { appendStateToBuffers } from '../../utils/streamBuffers';

/**
 * Background streamer that mirrors stateAtom samples into global chart buffers.
 * Mount once at app level so history persists across route changes.
 * Uses ref guard to prevent double-append in React StrictMode (effects run twice in dev).
 */
export function ChartBufferStream() {
  const state = useAtomValue(stateAtom);
  const lastStateRef = useRef<State | null>(null);

  useEffect(() => {
    // Guard against StrictMode double-invocation and duplicate state objects
    if (state && state !== lastStateRef.current) {
      lastStateRef.current = state;
      appendStateToBuffers(state);
    }
  }, [state]);

  return null;
}

