import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Pinned versions (re-verified 2026-06-16):
//   react / react-dom         19.2.7
//   @monaco-editor/react      ^4.7.0
//   monaco-editor             ^0.52.2
//   vite                      ^6
//   typescript                ^5.6
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      "/api": "http://localhost:8000",
      "/healthz": "http://localhost:8000",
      "/readyz": "http://localhost:8000",
    },
  },
  build: {
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
