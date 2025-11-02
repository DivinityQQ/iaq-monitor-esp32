/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_ESP32_HOST?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
