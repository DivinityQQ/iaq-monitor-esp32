import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import compression from 'vite-plugin-compression';
import { resolve } from 'path';

// Allow custom ESP32 IP via environment variable
const ESP32_HOST = process.env.VITE_ESP32_HOST || '192.168.4.1';

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
        target: `http://${ESP32_HOST}`,
        changeOrigin: true,
      },
      '/ws': {
        target: `ws://${ESP32_HOST}`,
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
