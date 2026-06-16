import { defineConfig, devices } from "@playwright/test";

// E2E suite. Requires the full stack running:
//   docker compose up -d (or `npm run dev` + `jobsvc` + `jobsvc-worker`)
// then `npm run test:e2e`.
export default defineConfig({
  testDir: "./tests/e2e",
  timeout: 60_000,
  retries: 0,
  reporter: "list",
  use: {
    baseURL: process.env.PLAYGROUND_URL ?? "http://localhost:5173",
    trace: "retain-on-failure",
  },
  projects: [
    { name: "chromium", use: devices["Desktop Chrome"] },
  ],
});
