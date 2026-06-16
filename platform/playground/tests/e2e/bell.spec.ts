import { test, expect } from "@playwright/test";

const BELL = `target generic
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
`;

test("login → write Bell → click Run → see histogram", async ({ page }) => {
  await page.goto("/login");
  await page.getByTestId("login-email").fill("user@x.com");
  await page.getByTestId("login-password").fill("user-password");
  await page.getByTestId("login-submit").click();

  // Editor mounts after login.
  await expect(page.getByTestId("run-button")).toBeVisible();

  // Pick the local target.
  await page.getByTestId("target-picker").selectOption("generic");
  await page.getByTestId("shots-input").fill("100");

  // The Monaco editor is hard to type into; we patch the program via
  // a small JS hook the page exposes for testing.
  await page.evaluate((src) => {
    // monaco models share text via a global helper; simplest path:
    // the application owns a global window.__setProgram for tests.
    (window as any).__setProgram?.(src);
  }, BELL);

  await page.getByTestId("run-button").click();

  // Wait for the histogram to appear (worker must be running).
  await expect(page.getByTestId("histogram")).toBeVisible({ timeout: 30_000 });
});
