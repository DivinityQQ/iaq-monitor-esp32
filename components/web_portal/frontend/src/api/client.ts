import type {
  DeviceInfo,
  State,
  Metrics,
  Health,
  Power,
  WiFiStatus,
  WiFiScanResult,
  WiFiConfig,
  MQTTStatus,
  MQTTConfig,
  SensorsResponse,
  CadenceResponse,
  ApiSuccess,
  SensorId,
  PowerOutputsConfig,
  ChargerConfig,
  PowerAlarmsConfig,
  OTAVersionInfo,
  OTAUploadResponse,
} from './types';

class ApiClient {
  private baseUrl = '/api/v1';
  private defaultTimeoutMs = Number((import.meta as any).env?.VITE_API_TIMEOUT_MS) || 4000;

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

  async scanWiFi(limit?: number, signal?: AbortSignal): Promise<WiFiScanResult> {
    const query = limit ? `?limit=${limit}` : '';
    return this.fetch(`/wifi/scan${query}`, { signal });
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
  // POWER (PowerFeather only)
  // ============================================================================

  async getPower(): Promise<Power> {
    return this.fetch('/power');
  }

  async setPowerOutputs(config: PowerOutputsConfig): Promise<ApiSuccess> {
    return this.fetch('/power/outputs', { method: 'POST', body: config });
  }

  async setCharger(config: ChargerConfig): Promise<ApiSuccess> {
    return this.fetch('/power/charger', { method: 'POST', body: config });
  }

  async setPowerAlarms(config: PowerAlarmsConfig): Promise<ApiSuccess> {
    return this.fetch('/power/alarms', { method: 'POST', body: config });
  }

  // ============================================================================
  // OTA UPDATE
  // ============================================================================

  async getOTAInfo(): Promise<OTAVersionInfo> {
    return this.fetch('/ota/info');
  }

  async uploadFirmware(
    file: File,
    onProgress?: (progress: number) => void
  ): Promise<OTAUploadResponse> {
    return this.uploadBinary('/ota/firmware', file, onProgress);
  }

  async uploadFrontend(
    file: File,
    onProgress?: (progress: number) => void
  ): Promise<OTAUploadResponse> {
    return this.uploadBinary('/ota/frontend', file, onProgress);
  }

  async rollbackFirmware(): Promise<ApiSuccess> {
    return this.fetch('/ota/rollback', { method: 'POST' });
  }

  private uploadBinary(
    path: string,
    file: File,
    onProgress?: (progress: number) => void
  ): Promise<OTAUploadResponse> {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', this.baseUrl + path);

      xhr.upload.onprogress = (event) => {
        if (event.lengthComputable && onProgress) {
          onProgress(Math.round((event.loaded / event.total) * 100));
        }
      };

      xhr.onload = () => {
        try {
          const response = JSON.parse(xhr.responseText);
          if (xhr.status >= 200 && xhr.status < 300) {
            resolve(response as OTAUploadResponse);
          } else {
            reject(new Error(response.error?.message || response.message || 'Upload failed'));
          }
        } catch {
          reject(new Error(xhr.statusText || 'Upload failed'));
        }
      };

      xhr.onerror = () => reject(new Error('Network error during upload'));
      xhr.ontimeout = () => reject(new Error('Upload timed out'));

      // No timeout for large uploads - let it complete
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      xhr.send(file);
    });
  }

  // ============================================================================
  // PRIVATE HELPER
  // ============================================================================

  private async fetch(
    path: string,
    options?: Omit<RequestInit, 'body'> & { body?: unknown }
  ): Promise<any> {
    const controller = new AbortController();
    const timeout = window.setTimeout(() => controller.abort(), this.defaultTimeoutMs);

    // If caller provided a signal, chain it
    if (options?.signal) {
      const ext = options.signal;
      if (ext.aborted) controller.abort();
      else ext.addEventListener('abort', () => controller.abort(), { once: true });
    }

    try {
      const response = await window.fetch(this.baseUrl + path, {
        ...options,
        headers: {
          ...(options?.body ? { 'Content-Type': 'application/json' } : {}),
          'Accept': 'application/json',
          ...options?.headers,
        },
        body: options?.body ? JSON.stringify(options.body) : undefined,
        signal: controller.signal,
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
    } catch (err: any) {
      if (err?.name === 'AbortError') {
        throw new Error('Request timed out');
      }
      throw err;
    } finally {
      window.clearTimeout(timeout);
    }
  }
}

export const apiClient = new ApiClient();
