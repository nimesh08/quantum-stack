/**
 * `@heisenberg/sdk` — typed REST client plus React Query hooks for
 * the Heisenberg Quantum Stack jobsvc.
 *
 * Author: Nimesh Cheedella <chnimesh0808@gmail.com>
 */

export * from "./types.js";
export { createClient } from "./client.js";
export type { Client, ClientOptions } from "./client.js";
export {
  useJob,
  useJobs,
  useTargets,
  useCreateJob,
  useCancelJob,
} from "./hooks.js";

/** Package version (kept in sync with `package.json`). */
export const VERSION = "0.5.0";
