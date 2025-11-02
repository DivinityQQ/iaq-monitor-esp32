import type {
  DeviceInfo,
  State,
  Metrics,
  Health,
  WiFiStatus,
  WiFiScanResult,
  WiFiConfig,
  MQTTStatus,
  MQTTConfig,
  SensorsResponse,
  CadenceResponse,
  ApiSuccess,
  SensorId,
} from './types';

class ApiClient {
  private baseUrl = '/api/v1';

  // ============================================================================
  // DEVICE INFO
  // ============================================================================

  async getInfo(): Promise<DeviceInfo> {
    return this.fetch('/info');
  }

  async restartDevice(): Promise<ApiSuccess> {
    return this.fetch('/device/restart', { method: 'POST' });
  }

  // ============================================================================
  // STATE & METRICS
  // ============================================================================

  async getState(): Promise<State> {
    return this.fetch('/state');
  }

  async getMetrics(): Promise<Metrics> {
    return this.fetch('/metrics');
  }

  async getHealth(): Promise<Health> {
    return this.fetch('/health');
  }

  // ============================================================================
  // WIFI
  // ============================================================================

  async getWiFiStatus(): Promise<WiFiStatus> {
    return this.fetch('/wifi');
  }

  async scanWiFi(limit?: number): Promise<WiFiScanResult> {
    const query = limit ? `?limit=${limit}` : '';
    return this.fetch(`/wifi/scan${query}`);
  }

  async setWiFi(config: WiFiConfig): Promise<ApiSuccess> {
    return this.fetch('/wifi', { method: 'POST', body: config });
  }

  async restartWiFi(): Promise<ApiSuccess> {
    return this.fetch('/wifi/restart', { method: 'POST' });
  }

  // ============================================================================
  // MQTT
  // ============================================================================

  async getMQTTStatus(): Promise<MQTTStatus> {
    return this.fetch('/mqtt');
  }

  async setMQTT(config: MQTTConfig): Promise<ApiSuccess> {
    return this.fetch('/mqtt', { method: 'POST', body: config });
  }

  async restartMQTT(): Promise<ApiSuccess> {
    return this.fetch('/mqtt', { method: 'POST', body: { restart: true } });
  }

  // ============================================================================
  // SENSORS
  // ============================================================================

  async getSensors(): Promise<SensorsResponse> {
    return this.fetch('/sensors');
  }

  async getSensorCadences(): Promise<CadenceResponse> {
    return this.fetch('/sensors/cadence');
  }

  async setSensorCadence(sensorId: SensorId, ms: number): Promise<ApiSuccess> {
    return this.fetch(`/sensor/${sensorId}/cadence`, {
      method: 'POST',
      body: { ms }
    });
  }

  async readSensor(sensorId: SensorId): Promise<ApiSuccess> {
    return this.fetch(`/sensor/${sensorId}/read`, { method: 'POST' });
  }

  async resetSensor(sensorId: SensorId): Promise<ApiSuccess> {
    return this.fetch(`/sensor/${sensorId}/reset`, { method: 'POST' });
  }

  async enableSensor(sensorId: SensorId): Promise<ApiSuccess> {
    return this.fetch(`/sensor/${sensorId}/enable`, { method: 'POST' });
  }

  async disableSensor(sensorId: SensorId): Promise<ApiSuccess> {
    return this.fetch(`/sensor/${sensorId}/disable`, { method: 'POST' });
  }

  // ============================================================================
  // PRIVATE HELPER
  // ============================================================================

  private async fetch(
    path: string,
    options?: Omit<RequestInit, 'body'> & { body?: unknown }
  ): Promise<any> {
    const response = await window.fetch(this.baseUrl + path, {
      ...options,
      headers: {
        'Content-Type': 'application/json',
        ...options?.headers,
      },
      body: options?.body ? JSON.stringify(options.body) : undefined,
    });

    if (!response.ok) {
      let errorMessage = 'API request failed';
      try {
        const error = await response.json();
        errorMessage = error.error?.message || error.message || errorMessage;
      } catch {
        // If JSON parsing fails, use status text
        errorMessage = response.statusText || errorMessage;
      }
      throw new Error(errorMessage);
    }

    return response.json();
  }
}

export const apiClient = new ApiClient();
