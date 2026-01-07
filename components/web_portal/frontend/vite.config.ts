import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import compression from 'vite-plugin-compression';
import { resolve } from 'path';
import { execSync } from 'child_process';
import fs from 'fs';

// Allow custom ESP32 IP via environment variable
const ESP32_HOST = process.env.VITE_ESP32_HOST || '192.168.4.1';

// Emit version.txt alongside built assets using package.json version + short git SHA
const emitVersionPlugin = () => ({
  name: 'emit-version-txt',
  closeBundle() {
    const pkgPath = resolve(__dirname, 'package.json');
    let version = 'dev';
    try {
      const pkg = JSON.parse(fs.readFileSync(pkgPath, 'utf8'));
      if (pkg?.version) version = pkg.version;
    } catch { /* ignore */ }

    let sha = '';
    try {
      sha = execSync('git rev-parse --short HEAD', { cwd: resolve(__dirname, '../../..') }).toString().trim();
    } catch { /* ignore */ }

    const verString = sha ? `${version}+${sha}` : version;
    const outDir = resolve(__dirname, '../../../www');
    fs.mkdirSync(outDir, { recursive: true });
    fs.writeFileSync(resolve(outDir, 'version.txt'), `${verString}\n`);
    // eslint-disable-next-line no-console
    console.log(`[emit-version] Wrote ${verString} to version.txt`);
  },
});

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
    emitVersionPlugin(),
  ],

  build: {
    outDir: '../../../www',
    emptyOutDir: true,
    sourcemap: false,
    minify: 'esbuild',
    target: 'es2020',

    rollupOptions: {
      output: {
        manualChunks: (id) => {
          // Charts library - only loaded when visiting /charts route
          if (id.includes('@mui/x-charts')) {
            return 'vendor-charts';
          }
          // Bundle all other node_modules together
          if (id.includes('node_modules')) {
            return 'vendor';
          }
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
