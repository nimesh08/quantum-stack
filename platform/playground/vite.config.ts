import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { fileURLToPath, URL } from "node:url";

// Pinned versions (re-verified 2026-06-16):
//   react / react-dom         19.2.7
//   @monaco-editor/react      ^4.7.0
//   monaco-editor             ^0.52.2
//   vite                      ^6
//   typescript                ^5.6
//
// Build output goes into ../jobsvc/src/jobsvc/static/ so the FastAPI
// service serves the SPA at `/`. `heisenberg playground build` runs
// `npm run build` against this config.
const jobsvcStatic = fileURLToPath(
  new URL("../jobsvc/src/jobsvc/static/", import.meta.url),
);

export default defineConfig({
  plugins: [react()],
  server: {
    host: "0.0.0.0",
    port: 5173,
    proxy: {
      "/api": "http://localhost:8080",
      "/healthz": "http://localhost:8080",
      "/readyz": "http://localhost:8080",
    },
  },
  build: {
    outDir: jobsvcStatic,
    emptyOutDir: true,
    target: "es2022",
    sourcemap: true,
  },
  test: {
    environment: "jsdom",
    setupFiles: ["./tests/unit/setup.ts"],
    globals: true,
    include: ["tests/unit/**/*.{test,spec}.{ts,tsx}"],
    coverage: {
      provider: "v8",
      include: ["src/**/*.{ts,tsx}"],
      exclude: ["src/main.tsx", "src/**/*.d.ts"],
    },
  },
});
