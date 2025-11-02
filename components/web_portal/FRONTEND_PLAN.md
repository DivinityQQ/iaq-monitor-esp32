# Frontend Implementation Plan for IAQ Monitor Web Portal

**Last Updated:** 2025-11-02
**Status:** Week 1 Complete - Foundation Implemented
**Estimated Timeline:** 38-47 hours over 4 weeks
**Progress:** Week 1 (100%) | Week 2 (0%) | Week 3 (0%) | Week 4 (0%)

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Tech Stack](#tech-stack)
3. [Project Structure](#project-structure)
4. [Architecture & Data Flow](#architecture--data-flow)
5. [Key Features](#key-features)
6. [Build Configuration](#build-configuration)
7. [Development Workflow](#development-workflow)
8. [Implementation Timeline](#implementation-timeline)
9. [Performance Optimizations](#performance-optimizations)
10. [Testing Strategy](#testing-strategy)
11. [Future Enhancements](#future-enhancements)

---

## Executive Summary

Build a high-performance real-time dashboard SPA using the latest 2025 web technologies, prioritizing developer experience and performance. With 2MB LittleFS budget available, we can afford a full-featured stack while maintaining excellent performance.

**Key Goals:**
- Real-time sensor data visualization (1Hz updates via WebSocket)
- Modern Material Design 3 UI
- Type-safe API integration
- Responsive mobile-first design
- Production-ready with room to grow

**Bundle Size Budget:**
- Base libraries: ~230KB (11%)
- Application code: ~400KB (20%)
- **Total: ~630KB (31% of 2MB)**
- Remaining: ~1,370KB for future features

---

## Tech Stack

### Core Framework & Libraries (Latest Versions - January 2025)

```json
{
  "dependencies": {
    "react": "^19.2.0",
    "react-dom": "^19.2.0",
    "jotai": "^2.15.0",
    "@mui/material": "^7.3.4",
    "@mui/icons-material": "^7.3.4",
    "@emotion/react": "^11.13.5",
    "@emotion/styled": "^11.13.5",
    "uplot": "^1.6.31",
    "uplot-react": "^1.2.2",
    "react-use-websocket": "^4.8.1",
    "wouter": "^3.5.0"
  },
  "devDependencies": {
    "@types/react": "^19.0.0",
    "@types/react-dom": "^19.0.0",
    "@vitejs/plugin-react": "^4.3.4",
    "typescript": "^5.9.3",
    "vite": "^6.0.7",
    "vite-plugin-compression": "^0.5.1"
  }
}
```

### Technology Rationale

| Technology | Version | Bundle Size | Rationale |
|------------|---------|-------------|-----------|
| **React** | 19.2.0 | 59 KB | Latest stable, full ecosystem, Actions & useOptimistic for async UX |
| **Jotai** | 2.15.0 | 7 KB | Atomic state perfect for real-time data, automatic optimization |
| **MUI** | 7.3.4 | 204 KB | Latest Material Design 3, production-ready components |
| **uPlot** | 1.6.31 | 48 KB | Best performance for real-time charts (166K points in 25ms) |
| **Vite** | 6.0.7 | 0 KB | Latest build tool, fast HMR, excellent optimization |
| **TypeScript** | 5.9.3 | 0 KB | Latest with performance improvements, essential for type safety |
| **react-use-websocket** | 4.8.1 | 15 KB | React-friendly WebSocket with auto-reconnect |
| **wouter** | 3.5.0 | 2 KB | Tiny client-side router |

**Total Base Bundle:** ~230 KB gzipped (11% of 2MB budget)

---

## Project Structure

```
iaq-monitor-esp32/
├── components/web_portal/
│   ├── CMakeLists.txt              # No changes needed!
│   ├── API.md                      # Backend API documentation
│   ├── FRONTEND_PLAN.md            # This document
│   ├── web_portal.c                # Backend implementation
│   ├── dns_server.c
│   ├── include/
│   ├── certs/
│   └── frontend/                   # NEW: Frontend source
│       ├── src/
│       │   ├── main.tsx            # App entry point
│       │   ├── App.tsx             # Root component with providers
│       │   ├── theme.ts            # MUI Material Design 3 theme
│       │   │
│       │   ├── api/                # API layer
│       │   │   ├── client.ts       # REST API wrapper (fetch)
│       │   │   ├── websocket.ts    # WebSocket manager
│       │   │   └── types.ts        # TypeScript types from API.md
│       │   │
│       │   ├── store/              # Jotai state management
│       │   │   ├── atoms.ts        # Base atoms (state/metrics/health)
│       │   │   ├── derived.ts      # Derived atoms (calculations)
│       │   │   └── actions.ts      # Async actions (API calls)
│       │   │
│       │   ├── components/         # React components
│       │   │   ├── Layout/
│       │   │   │   ├── AppBar.tsx          # Top navigation bar
│       │   │   │   ├── NavDrawer.tsx       # Side navigation
│       │   │   │   └── ConnectionStatus.tsx # WiFi/MQTT/WS indicators
│       │   │   │
│       │   │   ├── Dashboard/
│       │   │   │   ├── Dashboard.tsx       # Main dashboard view
│       │   │   │   ├── SensorCard.tsx      # Individual sensor card
│       │   │   │   ├── AQICard.tsx         # Air Quality Index display
│       │   │   │   ├── ComfortCard.tsx     # Comfort score display
│       │   │   │   └── MetricsGrid.tsx     # Grid layout for cards
│       │   │   │
│       │   │   ├── Charts/
│       │   │   │   ├── RealtimeChart.tsx   # uPlot real-time chart
│       │   │   │   ├── ChartContainer.tsx  # Chart wrapper with controls
│       │   │   │   └── HistoryChart.tsx    # Historical data (future)
│       │   │   │
│       │   │   ├── Config/
│       │   │   │   ├── WiFiConfig.tsx      # WiFi setup & scan
│       │   │   │   ├── MQTTConfig.tsx      # MQTT broker config
│       │   │   │   ├── SensorControl.tsx   # Per-sensor controls
│       │   │   │   └── ConfigTabs.tsx      # Tab container
│       │   │   │
│       │   │   ├── Health/
│       │   │   │   ├── SystemHealth.tsx    # System info (heap, uptime)
│       │   │   │   ├── SensorStatus.tsx    # Per-sensor status
│       │   │   │   └── HealthDashboard.tsx # Main health view
│       │   │   │
│       │   │   └── Common/
│       │   │       ├── LoadingSkeleton.tsx # Loading states
│       │   │       ├── ErrorBoundary.tsx   # Error handling
│       │   │       └── StatusChip.tsx      # Status indicators
│       │   │
│       │   ├── hooks/              # Custom React hooks
│       │   │   ├── useWebSocket.ts         # WebSocket integration
│       │   │   ├── useRealtimeData.ts      # Real-time data management
│       │   │   ├── useApiClient.ts         # REST API client
│       │   │   └── useLocalStorage.ts      # Persistent settings
│       │   │
│       │   └── utils/              # Utility functions
│       │       ├── formatting.ts           # Number/date formatters
│       │       ├── colors.ts               # AQI color mapping
│       │       ├── constants.ts            # App constants
│       │       └── validation.ts           # Form validation
│       │
│       ├── public/
│       │   └── favicon.svg
│       │
│       ├── index.html              # Entry HTML
│       ├── package.json            # npm dependencies
│       ├── tsconfig.json           # TypeScript config
│       ├── tsconfig.node.json      # TypeScript for Vite
│       ├── vite.config.ts          # Vite build config
│       └── .gitignore
│
├── www/                            # Build output (git-ignored)
│   ├── index.html
│   ├── assets/
│   │   ├── index-[hash].js         # Main bundle
│   │   ├── vendor-react-[hash].js  # React chunk
│   │   ├── vendor-mui-[hash].js    # MUI chunk
│   │   ├── vendor-charts-[hash].js # uPlot chunk (lazy)
│   │   └── index-[hash].css        # Styles
│   └── favicon.svg
│
└── CMakeLists.txt                  # Root - web image generation handled in main (LittleFS)
```

---

## Architecture & Data Flow

### State Management with Jotai

**Philosophy:** Atomic state management for fine-grained reactivity. Perfect for high-frequency sensor updates.

```typescript
// store/atoms.ts
import { atom } from 'jotai';
import { State, Metrics, Health } from '../api/types';

// WebSocket connection state
export const wsConnectedAtom = atom(false);
export const wsReconnectingAtom = atom(false);

// Real-time data atoms (updated by WebSocket messages)
export const stateAtom = atom<State | null>(null);
export const metricsAtom = atom<Metrics | null>(null);
export const healthAtom = atom<Health | null>(null);

// Device info (from /api/v1/info)
export const deviceInfoAtom = atom<DeviceInfo | null>(null);

// Derived atoms (computed values)
export const aqiColorAtom = atom((get) => {
  const metrics = get(metricsAtom);
  const aqi = metrics?.aqi?.value;
  if (!aqi) return 'grey';
  if (aqi <= 50) return 'green';
  if (aqi <= 100) return 'yellow';
  if (aqi <= 150) return 'orange';
  if (aqi <= 200) return 'red';
  return 'purple';
});

export const sensorStatusMapAtom = atom((get) => {
  const health = get(healthAtom);
  return health?.sensors || {};
});

export const connectionStatusAtom = atom((get) => {
  const info = get(deviceInfoAtom);
  const wsConnected = get(wsConnectedAtom);
  return {
    websocket: wsConnected,
    wifi: info?.network?.wifi_connected || false,
    mqtt: info?.network?.mqtt_connected || false,
  };
});
```

**Why Jotai:**
- **Atomic updates** → Only components using specific atoms re-render
- **Automatic optimization** → No manual memoization needed
- **Perfect for real-time** → Handles 1Hz state updates efficiently
- **TypeScript-first** → Excellent type inference
- **7KB bundle** → Lightweight

### WebSocket Data Flow

```
ESP32 WebSocket (/ws)
    ↓ (1Hz state, 5s metrics, 30s health)
    ↓
useWebSocket Hook
    ↓
Parse JSON envelope: { type, data }
    ↓
useSetAtom(stateAtom/metricsAtom/healthAtom)
    ↓
Jotai updates atoms
    ↓
React components auto-update (atomic subscriptions)
    ↓
UI re-renders only affected components
```

**Message Types:**
```typescript
type WSMessage =
  | { type: 'state'; data: State }      // 1 Hz
  | { type: 'metrics'; data: Metrics }  // Every 5s
  | { type: 'health'; data: Health };   // Every 30s + WiFi events
```

### REST API Integration

```typescript
// api/client.ts
class ApiClient {
  private baseUrl = '/api/v1';

  async getInfo(): Promise<DeviceInfo> {
    return this.fetch('/info');
  }

  async getState(): Promise<State> {
    return this.fetch('/state');
  }

  async setWiFi(config: WiFiConfig): Promise<void> {
    return this.fetch('/wifi', { method: 'POST', body: config });
  }

  async scanWiFi(limit?: number): Promise<WiFiScanResult> {
    return this.fetch(`/wifi/scan?limit=${limit || 20}`);
  }

  private async fetch(path: string, options?: RequestInit) {
    const response = await fetch(this.baseUrl + path, {
      ...options,
      headers: {
        'Content-Type': 'application/json',
        ...options?.headers,
      },
      body: options?.body ? JSON.stringify(options.body) : undefined,
    });

    if (!response.ok) {
      const error = await response.json();
      throw new Error(error.message || 'API request failed');
    }

    return response.json();
  }
}

export const apiClient = new ApiClient();
```

### Component Architecture

```
App.tsx (Providers: Jotai, MUI Theme)
├── Router (wouter)
│   │
│   ├── Route: / → Dashboard
│   │   ├── MetricsGrid
│   │   │   ├── SensorCard (Temp)      [useAtomValue(stateAtom)]
│   │   │   ├── SensorCard (Humidity)  [useAtomValue(stateAtom)]
│   │   │   ├── SensorCard (CO2)       [useAtomValue(stateAtom)]
│   │   │   ├── SensorCard (PM2.5)     [useAtomValue(stateAtom)]
│   │   │   ├── AQICard                [useAtomValue(metricsAtom)]
│   │   │   └── ComfortCard            [useAtomValue(metricsAtom)]
│   │   └── RealtimeChart (lazy)       [useAtomValue(stateAtom)]
│   │
│   ├── Route: /charts → ChartsView
│   │   └── Historical charts (future)
│   │
│   ├── Route: /config → ConfigTabs
│   │   ├── WiFiConfig                 [REST API]
│   │   ├── MQTTConfig                 [REST API]
│   │   └── SensorControl              [REST API]
│   │
│   └── Route: /health → HealthDashboard
│       ├── SystemHealth               [useAtomValue(healthAtom)]
│       └── SensorStatus Grid          [useAtomValue(sensorStatusMapAtom)]
│
├── AppBar
│   ├── Device Info                    [useAtomValue(deviceInfoAtom)]
│   └── ConnectionStatus               [useAtomValue(connectionStatusAtom)]
│
└── NavDrawer
    └── Navigation links
```

---

## Key Features

### Phase 1: Core Dashboard (Priority: HIGH)

#### 1.1 Layout & Navigation
- **Responsive AppBar** (MUI)
  - Device ID and uptime in title
  - Connection status indicators (3 chips: WS, WiFi, MQTT)
  - Menu button for drawer toggle (mobile)
  - Settings icon
- **Navigation Drawer** (MUI)
  - Persistent on desktop (md+)
  - Temporary overlay on mobile
  - Nav items: Dashboard, Charts, Config, Health, System
- **Responsive Grid** (MUI Grid2)
  - 12-column grid system
  - Breakpoints: xs/sm/md/lg/xl

#### 1.2 Real-time Metrics Grid
Six primary sensor cards with live updates:

1. **Temperature Card**
   - Large reading: `24.5°C`
   - Subtitle: "Comfortable" (green/yellow/red)
   - Icon: ThermostatIcon
   - Trend arrow (up/down/stable)

2. **Humidity Card**
   - Reading: `45%`
   - Subtitle: "Optimal" / "Too dry" / "Too humid"
   - Icon: OpacityIcon
   - Dew point: `12.3°C`

3. **CO2 Card**
   - Reading: `650 ppm`
   - Subtitle: "Good" / "Moderate" / "Poor"
   - Icon: Co2Icon
   - Rate: `+5 ppm/hr`

4. **PM2.5 Card**
   - Reading: `12 µg/m³`
   - Subtitle: "Good" / "Moderate" / "Unhealthy"
   - Icon: AirIcon
   - Spike indicator

5. **AQI Card** (featured, larger)
   - Large AQI value: `42`
   - Category: "Good" (colored background)
   - Dominant pollutant: "PM2.5"
   - Sub-indices: PM2.5 (28), PM10 (15)

6. **Comfort Score Card** (featured, larger)
   - Score: `85/100`
   - Category: "Comfortable"
   - Heat index: `23.8°C`
   - Absolute humidity: `7.8 g/m³`

**Update Frequency:** 1Hz via WebSocket (state + metrics atoms)

#### 1.3 WebSocket Connection
```typescript
// hooks/useWebSocket.ts
export function useRealtimeData() {
  const setState = useSetAtom(stateAtom);
  const setMetrics = useSetAtom(metricsAtom);
  const setHealth = useSetAtom(healthAtom);
  const setConnected = useSetAtom(wsConnectedAtom);

  const { readyState, lastMessage } = useWebSocket(
    getWebSocketUrl(), // ws:// or wss:// based on protocol
    {
      onOpen: () => setConnected(true),
      onClose: () => setConnected(false),
      onError: (event) => console.error('WebSocket error:', event),
      shouldReconnect: () => true,
      reconnectInterval: 3000,
      reconnectAttempts: Infinity,
    }
  );

  useEffect(() => {
    if (lastMessage) {
      try {
        const msg: WSMessage = JSON.parse(lastMessage.data);
        switch (msg.type) {
          case 'state': setState(msg.data); break;
          case 'metrics': setMetrics(msg.data); break;
          case 'health': setHealth(msg.data); break;
        }
      } catch (error) {
        console.error('WebSocket message parse error:', error);
      }
    }
  }, [lastMessage]);

  return { connected: readyState === WebSocket.OPEN };
}
```

### Phase 2: Configuration (Priority: HIGH)

#### 2.1 WiFi Configuration
- **WiFi Status Card**
  - Current SSID, RSSI, IP address
  - Status indicator (connected/disconnected)
  - "Restart WiFi" button

- **WiFi Scan**
  - "Scan Networks" button
  - Loading state during scan
  - Results table: SSID, Signal, Security, Channel
  - Click row to pre-fill connection form

- **WiFi Connection Form**
  - SSID input (with autocomplete from scan)
  - Password input (toggle visibility)
  - "Restart after connecting" checkbox (default: true)
  - "Connect" button
  - Success/error toast notifications

#### 2.2 MQTT Configuration
- **MQTT Status Card**
  - Broker URL
  - Connection status (connected/disconnected)
  - "Restart MQTT" button

- **MQTT Setup Form**
  - Broker URL input (validation: mqtt[s]://)
  - Username input (optional)
  - Password input (optional, toggle visibility)
  - "Restart after saving" checkbox (default: true)
  - "Save Configuration" button
  - Success/error toast notifications

#### 2.3 Sensor Control
**Per-sensor card** (SHT45, BMP280, SGP41, PMS5003, S8, MCU):
- Sensor name and icon
- Status chip (UNINIT/INIT/WARMING/READY/ERROR/DISABLED)
- Last read timestamp (relative: "2s ago")
- Error count
- Warmup countdown (if warming)
- **Action buttons:**
  - "Read Now" → POST `/api/v1/sensor/{id}/read`
  - "Reset" → POST `/api/v1/sensor/{id}/reset`
  - "Enable/Disable" → POST `/api/v1/sensor/{id}/enable|disable`
- **Cadence slider:**
  - 0ms (disabled) to 60,000ms
  - Common presets: 1s, 5s, 10s, 30s, 60s
  - POST `/api/v1/sensor/{id}/cadence` with `{ ms: number }`

### Phase 3: Charts & Visualization (Priority: MEDIUM)

#### 3.1 Real-time Chart (uPlot)
- **Chart Configuration:**
  - 60-second rolling window
  - Multi-series: temp, humidity, CO2
  - Dual Y-axes (left: temp/humidity, right: CO2)
  - Time on X-axis (relative: -60s to 0s)
  - Zoom/pan support
  - Legend with current values

- **Implementation:**
  ```typescript
  // components/Charts/RealtimeChart.tsx
  import uPlot from 'uplot';
  import UplotReact from 'uplot-react';

  export function RealtimeChart() {
    const state = useAtomValue(stateAtom);
    const [data, setData] = useState<uPlot.AlignedData>([[], [], [], []]);

    useEffect(() => {
      if (!state) return;

      const now = Date.now() / 1000;
      setData(prev => {
        const [time, temp, rh, co2] = prev;
        const newTime = [...time, now];
        const newTemp = [...temp, state.temp_c];
        const newRh = [...rh, state.rh_pct];
        const newCo2 = [...co2, state.co2_ppm];

        // Keep only last 60 seconds
        const cutoff = now - 60;
        const startIdx = newTime.findIndex(t => t >= cutoff);

        return [
          newTime.slice(startIdx),
          newTemp.slice(startIdx),
          newRh.slice(startIdx),
          newCo2.slice(startIdx),
        ];
      });
    }, [state]);

    return (
      <UplotReact
        options={chartOptions}
        data={data}
      />
    );
  }
  ```

#### 3.2 Historical Charts (Future)
- Time range selector (1h, 6h, 24h, 7d)
- Multiple chart types (line, area, bar)
- Export data as CSV
- **Requires backend:** Add data logging endpoint

### Phase 4: Health & System (Priority: MEDIUM)

#### 4.1 System Health Dashboard
- **System Info Card:**
  - Uptime (formatted: "5d 3h 12m")
  - Free heap (with percentage and trend)
  - MCU temperature
  - WiFi RSSI (with signal strength bar)
  - Time sync status (synced/not synced)
  - Current epoch time

- **Network Status:**
  - Mode (AP-only / STA / AP+STA)
  - STA IP address (if connected)
  - AP IP address (if enabled)

#### 4.2 Sensor Status Grid
**Per-sensor card:**
- Sensor name with icon
- Large status indicator (color-coded)
- State: UNINIT (gray) / INIT (blue) / WARMING (yellow) / READY (green) / ERROR (red) / DISABLED (gray)
- Error count (with badge if >0)
- Last successful read (relative time)
- Warmup progress (if WARMING state)

**States:**
- UNINIT: "Not initialized"
- INIT: "Initializing..."
- WARMING: "Warming up (Xs remaining)"
- READY: "Ready"
- ERROR: "Error detected"
- DISABLED: "Disabled"

#### 4.3 System Controls
- **Device Restart:**
  - Button with confirmation dialog
  - POST `/api/v1/device/restart`
  - Shows countdown after confirmation

- **WiFi Restart:**
  - Button (no confirmation needed)
  - POST `/api/v1/wifi/restart`
  - Toast notification

- **MQTT Restart:**
  - Button (no confirmation needed)
  - POST `/api/v1/mqtt` with `restart: true`
  - Toast notification

---

## Build Configuration

### Vite Configuration

```typescript
// vite.config.ts
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import compression from 'vite-plugin-compression';
import { resolve } from 'path';

export default defineConfig({
  plugins: [
    react({
      jsxImportSource: '@emotion/react',
      babel: {
        plugins: ['@emotion/babel-plugin'],
      },
    }),
    compression({
      algorithm: 'gzip',
      ext: '.gz',
    }),
  ],

  build: {
    outDir: '../../../www',
    emptyOutDir: true,
    sourcemap: false,
    minify: 'esbuild',
    target: 'es2020',

    rollupOptions: {
      output: {
        manualChunks: {
          'vendor-react': ['react', 'react-dom', 'jotai'],
          'vendor-mui': [
            '@mui/material',
            '@mui/icons-material',
            '@emotion/react',
            '@emotion/styled',
          ],
          'vendor-charts': ['uplot', 'uplot-react'],
        },
      },
    },
  },

  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://192.168.4.1', // ESP32 AP mode IP
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://192.168.4.1',
        ws: true,
        changeOrigin: true,
      },
    },
  },

  resolve: {
    alias: {
      '@': resolve(__dirname, 'src'),
    },
  },
});
```

**Optimizations:**
- **Manual chunks** for better caching (vendor libs rarely change)
- **Gzip compression** for static files
- **No sourcemaps** in production
- **ESBuild minifier** (faster than Terser)
- **ES2020 target** (modern browsers only)

### TypeScript Configuration

```json
// tsconfig.json
{
  "compilerOptions": {
    "target": "ES2020",
    "useDefineForClassFields": true,
    "lib": ["ES2020", "DOM", "DOM.Iterable"],
    "module": "ESNext",
    "skipLibCheck": true,

    /* Bundler mode */
    "moduleResolution": "bundler",
    "allowImportingTsExtensions": true,
    "resolveJsonModule": true,
    "isolatedModules": true,
    "noEmit": true,
    "jsx": "react-jsx",
    "jsxImportSource": "@emotion/react",

    /* Linting */
    "strict": true,
    "noUnusedLocals": true,
    "noUnusedParameters": true,
    "noFallthroughCasesInSwitch": true,

    /* Path aliases */
    "baseUrl": ".",
    "paths": {
      "@/*": ["src/*"]
    }
  },
  "include": ["src"],
  "references": [{ "path": "./tsconfig.node.json" }]
}
```

```json
// tsconfig.node.json
{
  "compilerOptions": {
    "composite": true,
    "skipLibCheck": true,
    "module": "ESNext",
    "moduleResolution": "bundler",
    "allowSyntheticDefaultImports": true
  },
  "include": ["vite.config.ts"]
}
```

### Package.json Scripts

```json
{
  "name": "iaq-monitor-frontend",
  "private": true,
  "version": "1.0.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "tsc && vite build",
    "build:prod": "tsc && vite build --mode production",
    "preview": "vite preview",
    "type-check": "tsc --noEmit",
    "lint": "eslint src --ext ts,tsx"
  }
}
```

---

## Development Workflow

### Local Development Setup

```bash
# Terminal 1: Start frontend dev server
cd components/web_portal/frontend
npm install
npm run dev

# Vite dev server starts at http://localhost:5173
# Proxy forwards /api → http://192.168.4.1
# WebSocket connects to ws://192.168.4.1/ws
```

**Benefits:**
- Hot Module Replacement (instant updates on save)
- Fast Refresh (preserves component state)
- Real backend connection (no mocking needed)
- TypeScript type checking
- Source maps for debugging

**Environment Variables:**
```bash
# .env.local (optional, for custom ESP32 IP)
VITE_ESP32_HOST=192.168.1.100
```

Update vite.config.ts to use:
```typescript
const ESP32_HOST = process.env.VITE_ESP32_HOST || '192.168.4.1';
```

### Production Build & Flash

```bash
# Step 1: Build frontend
cd components/web_portal/frontend
npm run build:prod

# Output: www/ directory with optimized bundle
# - index.html
# - assets/index-[hash].js
# - assets/index-[hash].css
# - favicon.svg

# Step 2: Build ESP-IDF firmware
cd ../../../
idf.py build

# CMake automatically:
# 1. Detects www/ directory exists (in main/CMakeLists.txt)
# 2. Calls littlefs_create_partition_image(www ${WEB_DIR} FLASH_IN_PROJECT)
# 3. Generates LittleFS image
# 4. Includes in build

# Step 3: Flash to ESP32
idf.py flash

# Or flash everything at once:
idf.py build flash
```

**Build Pipeline:**
1. Vite builds optimized SPA → `www/`
2. ESP-IDF detects `www/` exists
3. `littlefs_create_partition_image` creates a LittleFS image from `www/`
4. LittleFS image included in flash image
5. Flash to ESP32 (app + LittleFS)

### Bundle Size Monitoring

```bash
# After build, check sizes
cd www
du -sh .        # Total size
du -sh assets/* # Per-file sizes

# Expected sizes:
# Total: 600-700 KB (uncompressed)
# index.html: ~2 KB
# vendor-react-*.js: ~80 KB (gzipped ~60 KB)
# vendor-mui-*.js: ~250 KB (gzipped ~200 KB)
# vendor-charts-*.js: ~50 KB (gzipped ~48 KB)
# index-*.js: ~100 KB (gzipped ~40 KB)
# index-*.css: ~50 KB (gzipped ~10 KB)
```

---

## Implementation Timeline

### Week 1: Foundation (8-10 hours)

**Day 1-2: Project Setup (4h)**
- ✅ Create `frontend/` directory structure
- ✅ Initialize npm project (`npm init`)
- ✅ Install dependencies (React, MUI, Jotai, etc.)
- ✅ Configure TypeScript (tsconfig.json)
- ✅ Configure Vite (vite.config.ts)
- ✅ Create basic `index.html` and `main.tsx`
- ✅ Test dev server (`npm run dev`)

**Day 3: API Layer (2h)**
- ✅ Create TypeScript types from API.md (`api/types.ts`)
- ✅ Build REST API client (`api/client.ts`)
- ✅ Test API calls to ESP32

**Day 4-5: Basic Layout (4h)**
- ✅ Setup MUI theme (`theme.ts`)
- ✅ Create App.tsx with providers (Jotai, MUI)
- ✅ Build AppBar component
- ✅ Build NavDrawer component
- ✅ Setup routing (wouter)
- ✅ Test responsive behavior

**Deliverable:** Working shell with navigation, no data yet

**Implementation Notes (2025-11-02):**
- ✅ All Week 1 tasks completed successfully
- ✅ Build output: 508KB total (25% of 2MB budget)
  - Gzipped vendor-react: 6.9KB
  - Gzipped vendor-mui: 53KB
  - Gzipped main bundle: 60KB
- ✅ TypeScript compilation: No errors
- ✅ Responsive layout tested: Desktop drawer persistent, mobile drawer temporary
- ✅ Routing functional: All 4 routes working (/, /charts, /config, /health)
- 📝 Ready for Week 2: WebSocket integration and dashboard components

---

### Week 2: Core Dashboard (12-15 hours)

**Day 1-2: WebSocket Integration (4h)**
- ✅ Create Jotai atoms (`store/atoms.ts`)
- ✅ Build WebSocket hook (`hooks/useWebSocket.ts`)
- ✅ Integrate with Jotai atoms
- ✅ Test real-time updates
- ✅ Add reconnection logic

**Day 3-4: Sensor Cards (6h)**
- ✅ Create SensorCard component (reusable)
- ✅ Build 6 metric cards (temp, humidity, CO2, PM2.5, AQI, comfort)
- ✅ Add icons and color coding
- ✅ Format values with units
- ✅ Add trend indicators
- ✅ Test with live data

**Day 5: Dashboard Layout (3h)**
- ✅ Create MetricsGrid component
- ✅ Setup responsive grid (MUI Grid2)
- ✅ Add loading skeletons
- ✅ Add error states (null data handling)
- ✅ Polish animations and transitions

**Deliverable:** Functional real-time dashboard with 6 cards updating at 1Hz

---

### Week 3: Configuration (10-12 hours)

**Day 1-2: WiFi Configuration (5h)**
- ✅ Build WiFiConfig component
- ✅ Implement network scan (GET `/api/v1/wifi/scan`)
- ✅ Create scan results table
- ✅ Build connection form (SSID + password)
- ✅ Add form validation
- ✅ Implement connect action (POST `/api/v1/wifi`)
- ✅ Add restart button (POST `/api/v1/wifi/restart`)
- ✅ Add toast notifications (success/error)

**Day 3: MQTT Configuration (3h)**
- ✅ Build MQTTConfig component
- ✅ Display current broker status (GET `/api/v1/mqtt`)
- ✅ Create setup form (broker URL, username, password)
- ✅ Add URL validation
- ✅ Implement save action (POST `/api/v1/mqtt`)
- ✅ Add restart button
- ✅ Add toast notifications

**Day 4-5: Sensor Control (4h)**
- ✅ Build SensorControl component
- ✅ Create per-sensor cards (6 sensors)
- ✅ Display sensor status (state, errors, last read)
- ✅ Add action buttons (Read, Reset, Enable, Disable)
- ✅ Implement cadence slider
- ✅ Wire up all POST endpoints
- ✅ Add confirmation dialogs for destructive actions

**Deliverable:** Full configuration interface (WiFi, MQTT, sensors)

---

### Week 4: Charts & Polish (8-10 hours)

**Day 1-2: Real-time Charts (4h)**
- ✅ Install uPlot and uPlot-React
- ✅ Create RealtimeChart component
- ✅ Implement 60-second rolling window
- ✅ Add multi-series (temp, humidity, CO2)
- ✅ Configure dual Y-axes
- ✅ Add zoom/pan support
- ✅ Style chart (colors, grid, legend)
- ✅ Lazy load chart component

**Day 3: Health Dashboard (2h)**
- ✅ Build SystemHealth component
- ✅ Display uptime, heap, RSSI, time sync
- ✅ Build SensorStatus component
- ✅ Create sensor status grid
- ✅ Add color-coded state indicators

**Day 4: System Controls (1h)**
- ✅ Add device restart button
- ✅ Add confirmation dialog
- ✅ Implement restart action
- ✅ Add countdown/loading state

**Day 5: Polish & Testing (3h)**
- ✅ Add loading skeletons everywhere
- ✅ Create ErrorBoundary component
- ✅ Add empty states (no data)
- ✅ Improve mobile responsiveness
- ✅ Add ConnectionStatus component to AppBar
- ✅ Test on real hardware
- ✅ Optimize bundle size
- ✅ Test build pipeline (npm run build → idf.py flash)
- ✅ Final QA pass

**Deliverable:** Production-ready SPA with all features

---

### Total Timeline: 38-47 hours (~1 month part-time)

---

## Performance Optimizations

### Bundle Size Optimization

**1. Code Splitting**
```typescript
// Lazy load charts
const RealtimeChart = lazy(() => import('./components/Charts/RealtimeChart'));

function ChartsView() {
  return (
    <Suspense fallback={<CircularProgress />}>
      <RealtimeChart />
    </Suspense>
  );
}
```

**2. Tree Shaking**
```typescript
// Import only what you need from MUI
import Button from '@mui/material/Button';      // ✅ Good
import { Button } from '@mui/material';         // ⚠️ Okay with tree-shaking
import * as MUI from '@mui/material';           // ❌ Bad, imports everything
```

**3. Dynamic Imports**
```typescript
// Load chart library only when needed
async function loadChart() {
  const uPlot = await import('uplot');
  return uPlot.default;
}
```

### Runtime Performance

**1. Atomic State Updates (Jotai)**
```typescript
// Only SensorCard for temperature re-renders, not the whole dashboard
function TemperatureCard() {
  const state = useAtomValue(stateAtom);
  return <Card>{state?.temp_c}°C</Card>;
}
```

**2. Memoization (React.memo)**
```typescript
// Prevent re-renders when props don't change
export const SensorCard = memo(function SensorCard({ value, unit }: Props) {
  return <Card>...</Card>;
});
```

**3. Debounce/Throttle**
```typescript
// Throttle slider updates
const handleCadenceChange = useMemo(
  () => throttle((value: number) => {
    apiClient.setSensorCadence(sensorId, value);
  }, 500),
  [sensorId]
);
```

**4. Virtual Scrolling**
```typescript
// For long lists (WiFi scan results with 100+ APs)
import { FixedSizeList } from 'react-window';

function WiFiScanResults({ aps }: Props) {
  return (
    <FixedSizeList
      height={400}
      itemCount={aps.length}
      itemSize={56}
    >
      {({ index, style }) => (
        <div style={style}>
          <WiFiAPItem ap={aps[index]} />
        </div>
      )}
    </FixedSizeList>
  );
}
```

### Network Optimization

**1. WebSocket Optimization**
- Send only deltas (backend already does this)
- Use binary format for large payloads (future: CBOR)
- Compress WebSocket frames (if supported by esp_http_server)

**2. HTTP Optimization**
- Use HTTP/2 (HTTPS mode automatically enables it)
- Enable cache headers (backend already does: `Cache-Control: public, max-age=600`)
- Compress responses (backend should enable gzip)

**3. Lazy Loading**
```typescript
// Load charts only when user navigates to charts page
const ChartsView = lazy(() => import('./components/Charts/ChartsView'));
```

---

## Testing Strategy

### Manual Testing Checklist

**Dashboard:**
- [ ] All 6 cards display correct live data
- [ ] WebSocket reconnects after disconnect
- [ ] Cards show loading state on initial load
- [ ] Cards show "No data" when sensor is disabled
- [ ] Color coding matches thresholds (AQI green/yellow/red)
- [ ] Values update at correct frequency (1Hz visible)

**WiFi Config:**
- [ ] Scan shows available networks
- [ ] Signal strength indicators correct
- [ ] Connect form validates SSID (required)
- [ ] Connect form validates password (min 8 chars for WPA2)
- [ ] Success toast shows after connection
- [ ] Error toast shows on failure
- [ ] Restart button works

**MQTT Config:**
- [ ] Current broker status displays correctly
- [ ] URL validation works (mqtt:// or mqtts://)
- [ ] Save updates configuration
- [ ] Success/error toasts work
- [ ] Restart button works

**Sensor Control:**
- [ ] All 6 sensors display correct status
- [ ] "Read Now" button triggers immediate read
- [ ] "Reset" button resets sensor (error count clears)
- [ ] "Enable/Disable" toggles sensor state
- [ ] Cadence slider updates sensor read interval
- [ ] Warmup countdown displays for WARMING state

**Charts:**
- [ ] Real-time chart updates at 1Hz
- [ ] 60-second window scrolls correctly
- [ ] Multi-series (temp, humidity, CO2) visible
- [ ] Legend shows current values
- [ ] Zoom/pan works
- [ ] Chart lazy loads (check Network tab)

**Health:**
- [ ] System info displays (uptime, heap, RSSI)
- [ ] Sensor status grid shows all sensors
- [ ] State colors match (green=READY, red=ERROR, etc.)
- [ ] Error counts display correctly
- [ ] Last read timestamps update

**System:**
- [ ] Device restart button shows confirmation
- [ ] Device restart triggers ESP32 reboot
- [ ] WiFi/MQTT restart buttons work
- [ ] Toasts show for all actions

**Responsive:**
- [ ] Drawer is persistent on desktop (md+)
- [ ] Drawer is temporary on mobile (xs/sm)
- [ ] Cards stack vertically on mobile
- [ ] Grid layout adapts to screen size
- [ ] No horizontal scrolling

**Performance:**
- [ ] Initial load <2s
- [ ] No lag during 1Hz updates
- [ ] Smooth animations (60fps)
- [ ] Memory usage stable over time
- [ ] No memory leaks (run for 10+ minutes)

### Browser Compatibility

**Target Browsers:**
- Chrome 90+ (primary)
- Firefox 88+
- Safari 14+ (iOS 14+)
- Edge 90+

**Testing:**
- Desktop: Chrome, Firefox, Edge
- Mobile: Safari (iOS), Chrome (Android)
- Tablet: iPad Safari, Android Chrome

### Performance Metrics

**Lighthouse Targets:**
- Performance: >90
- Accessibility: >90
- Best Practices: >90
- SEO: >80

**Core Web Vitals:**
- LCP (Largest Contentful Paint): <2.5s
- FID (First Input Delay): <100ms
- CLS (Cumulative Layout Shift): <0.1
- FCP (First Contentful Paint): <1s
- TTI (Time to Interactive): <2s

**Bundle Size Limits:**
- Total bundle: <700 KB uncompressed
- Main JS: <150 KB gzipped
- Vendor JS: <300 KB gzipped
- CSS: <20 KB gzipped

---

## Future Enhancements

### Phase 5: PWA Features (8-10 hours)
- [ ] Service worker for offline support
- [ ] Web app manifest (installable)
- [ ] Offline mode (show last known data)
- [ ] Background sync
- [ ] Push notifications (if ESP32 supports)

### Phase 6: Dark Mode (4-6 hours)
- [ ] Add theme toggle (AppBar)
- [ ] Persist theme preference (localStorage)
- [ ] MUI theme variants (light/dark)
- [ ] Automatic dark mode (prefers-color-scheme)

### Phase 7: Data Logging (12-15 hours)
**Requires backend changes:**
- [ ] Add data storage (LittleFS or SD card)
- [ ] Add REST endpoints for historical data
- [ ] Implement time-series database (e.g., CSV files)
- [ ] Add retention policy (delete old data)

**Frontend:**
- [ ] Historical charts (1h, 6h, 24h, 7d views)
- [ ] Date range picker
- [ ] Data export (CSV, JSON)
- [ ] Statistics (min/max/avg)

### Phase 8: Alerts & Notifications (8-10 hours)
**Backend:**
- [ ] Add alert configuration API
- [ ] Add threshold monitoring
- [ ] Add notification system (email, push)

**Frontend:**
- [ ] Alert configuration UI
- [ ] Threshold sliders
- [ ] Alert history view
- [ ] Notification preferences

### Phase 9: Multi-Device Support (15-20 hours)
**Backend:**
- [ ] Add device discovery (mDNS)
- [ ] Add multi-device API

**Frontend:**
- [ ] Device selector dropdown
- [ ] Multi-device dashboard (grid of cards)
- [ ] Device comparison view
- [ ] Favorites/grouping

### Phase 10: Advanced Features (20+ hours)
- [ ] Correlation analysis (e.g., CO2 vs occupancy)
- [ ] Predictive modeling (ML-based forecasts)
- [ ] Custom dashboards (drag-and-drop widgets)
- [ ] Data visualization (3D charts, heatmaps)
- [ ] User accounts (multi-user support)
- [ ] API key management
- [ ] Firmware OTA updates UI

---

## Appendix: Complete TypeScript Types

```typescript
// api/types.ts

// ============================================================================
// SENSOR TYPES
// ============================================================================

export type SensorId = 'mcu' | 'sht45' | 'bmp280' | 'sgp41' | 'pms5003' | 's8';
export type SensorState = 'UNINIT' | 'INIT' | 'WARMING' | 'READY' | 'ERROR' | 'DISABLED';
export type SensorAction = 'read' | 'reset' | 'enable' | 'disable' | 'cadence';

export interface SensorStatus {
  state: SensorState;
  errors: number;
  last_read_s?: number;
  warmup_remaining_s?: number;
}

export interface SensorCadence {
  ms: number;
  from_nvs: boolean;
}

// ============================================================================
// STATE (1Hz WebSocket updates)
// ============================================================================

export interface State {
  temp_c: number | null;
  rh_pct: number | null;
  pressure_hpa: number | null;
  pm25_ugm3: number | null;
  pm10_ugm3: number | null;
  pm1_ugm3?: number | null;
  co2_ppm: number | null;
  voc_index: number | null;
  nox_index: number | null;
  mcu_temp_c: number | null;
  aqi: number | null;
  comfort_score: number | null;
}

// ============================================================================
// METRICS (5s WebSocket updates)
// ============================================================================

export interface Metrics {
  aqi: {
    value: number;
    category: string;
    dominant: string;
    pm25_subindex: number;
    pm10_subindex: number;
  };
  comfort: {
    score: number;
    category: string;
    dew_point_c: number;
    abs_humidity_gm3: number;
    heat_index_c: number;
  };
  pressure: {
    trend: string;
    delta_hpa: number;
    window_hours: number;
  };
  co2_score: number;
  voc_category: string;
  nox_category: string;
  overall_iaq_score: number;
  mold_risk: {
    score: number;
    category: string;
  };
  co2_rate_ppm_hr: number;
  pm25_spike_detected: boolean;
}

// ============================================================================
// HEALTH (30s WebSocket updates + WiFi events)
// ============================================================================

export interface Health {
  uptime: number;
  wifi_rssi: number;
  free_heap: number;
  time_synced: boolean;
  epoch?: number;
  sensors: Record<SensorId, SensorStatus>;
}

// ============================================================================
// DEVICE INFO (REST API: GET /api/v1/info)
// ============================================================================

export interface DeviceInfo {
  device_id: string;
  version: {
    major: number;
    minor: number;
    patch: number;
  };
  uptime: number;
  free_heap: number;
  network: {
    mode: string;
    wifi_connected: boolean;
    mqtt_connected: boolean;
    sta_ip?: string;
    ap_ip?: string;
  };
}

// ============================================================================
// WIFI
// ============================================================================

export interface WiFiStatus {
  provisioned: boolean;
  mode: string;
  ssid: string;
  rssi: number;
}

export interface WiFiAP {
  ssid: string;
  rssi: number;
  channel: number;
  auth: string;
}

export interface WiFiScanResult {
  aps: WiFiAP[];
}

export interface WiFiConfig {
  ssid: string;
  password: string;
  restart?: boolean;
}

// ============================================================================
// MQTT
// ============================================================================

export interface MQTTStatus {
  broker_url: string;
  configured: boolean;
  connected: boolean;
}

export interface MQTTConfig {
  broker_url: string;
  username?: string;
  password?: string;
  restart?: boolean;
}

// ============================================================================
// WEBSOCKET MESSAGES
// ============================================================================

export type WSMessage =
  | { type: 'state'; data: State }
  | { type: 'metrics'; data: Metrics }
  | { type: 'health'; data: Health };

// ============================================================================
// API RESPONSES
// ============================================================================

export interface ApiError {
  error: {
    code: string;
    message: string;
    status: number;
  };
}

export interface ApiSuccess {
  status: 'ok' | 'restarting';
}

export interface CadenceResponse {
  cadences: Record<SensorId, SensorCadence>;
}

export interface SensorsResponse {
  sensors: Record<SensorId, SensorStatus>;
}
```

---

## Appendix: MUI Theme Configuration

```typescript
// theme.ts
import { createTheme } from '@mui/material/styles';

export const theme = createTheme({
  palette: {
    mode: 'light',
    primary: {
      main: '#1976d2',
      light: '#42a5f5',
      dark: '#1565c0',
    },
    secondary: {
      main: '#dc004e',
      light: '#f50057',
      dark: '#c51162',
    },
    success: {
      main: '#2e7d32',
      light: '#4caf50',
      dark: '#1b5e20',
    },
    warning: {
      main: '#ed6c02',
      light: '#ff9800',
      dark: '#e65100',
    },
    error: {
      main: '#d32f2f',
      light: '#ef5350',
      dark: '#c62828',
    },
    info: {
      main: '#0288d1',
      light: '#03a9f4',
      dark: '#01579b',
    },
    background: {
      default: '#f5f5f5',
      paper: '#ffffff',
    },
  },

  typography: {
    fontFamily: [
      '-apple-system',
      'BlinkMacSystemFont',
      '"Segoe UI"',
      'Roboto',
      '"Helvetica Neue"',
      'Arial',
      'sans-serif',
    ].join(','),
    h1: {
      fontSize: '2.5rem',
      fontWeight: 500,
    },
    h2: {
      fontSize: '2rem',
      fontWeight: 500,
    },
    h3: {
      fontSize: '1.75rem',
      fontWeight: 500,
    },
    h4: {
      fontSize: '1.5rem',
      fontWeight: 500,
    },
    h5: {
      fontSize: '1.25rem',
      fontWeight: 500,
    },
    h6: {
      fontSize: '1rem',
      fontWeight: 500,
    },
  },

  shape: {
    borderRadius: 12,
  },

  components: {
    MuiCard: {
      styleOverrides: {
        root: {
          borderRadius: 12,
          boxShadow: '0 2px 8px rgba(0,0,0,0.1)',
        },
      },
    },
    MuiButton: {
      styleOverrides: {
        root: {
          borderRadius: 8,
          textTransform: 'none',
          fontWeight: 500,
        },
      },
    },
    MuiChip: {
      styleOverrides: {
        root: {
          borderRadius: 8,
          fontWeight: 500,
        },
      },
    },
  },
});

// AQI color mapping
export const getAQIColor = (aqi: number | null | undefined): string => {
  if (aqi === null || aqi === undefined) return '#9e9e9e'; // Gray
  if (aqi <= 50) return '#4caf50';   // Green - Good
  if (aqi <= 100) return '#ffeb3b';  // Yellow - Moderate
  if (aqi <= 150) return '#ff9800';  // Orange - Unhealthy for Sensitive
  if (aqi <= 200) return '#f44336';  // Red - Unhealthy
  if (aqi <= 300) return '#9c27b0';  // Purple - Very Unhealthy
  return '#7b1fa2';                  // Dark Purple - Hazardous
};

// Comfort score color mapping
export const getComfortColor = (score: number | null | undefined): string => {
  if (score === null || score === undefined) return '#9e9e9e';
  if (score >= 80) return '#4caf50';  // Green - Comfortable
  if (score >= 60) return '#ffeb3b';  // Yellow - Acceptable
  if (score >= 40) return '#ff9800';  // Orange - Uncomfortable
  return '#f44336';                   // Red - Very Uncomfortable
};

// Sensor state color mapping
export const getSensorStateColor = (state: SensorState): string => {
  switch (state) {
    case 'READY': return '#4caf50';     // Green
    case 'WARMING': return '#ff9800';   // Orange
    case 'INIT': return '#2196f3';      // Blue
    case 'ERROR': return '#f44336';     // Red
    case 'DISABLED': return '#9e9e9e';  // Gray
    case 'UNINIT': return '#757575';    // Dark Gray
    default: return '#9e9e9e';
  }
};
```

---

**End of Frontend Plan**

This document should be updated as implementation progresses and requirements change.
