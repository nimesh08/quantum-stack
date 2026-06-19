/**
 * React Query hooks. These are tree-shake-friendly: import only the
 * ones you use. They are guarded behind a runtime check so the
 * package can be imported in non-React contexts (Node scripts, BFFs)
 * without pulling React into the bundle.
 *
 * Author: Nimesh Cheedella <chnimesh0808@gmail.com>
 */

import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";

import type { Client } from "./client.js";
import type { CreateJobBody, JobDetail } from "./types.js";

/**
 * Query a single job by id. Polls every 1500ms until the state is
 * terminal (`Completed`, `Failed`, or `Rejected`).
 */
export function useJob(client: Client, id: string | null) {
  return useQuery({
    queryKey: ["heisenberg", "job", id],
    queryFn: () => client.jobs.get(id!),
    enabled: id !== null,
    refetchInterval: (q) => {
      const data = q.state.data as JobDetail | undefined;
      if (!data) return 1500;
      if (
        data.state === "Completed" ||
        data.state === "Failed" ||
        data.state === "Rejected"
      ) {
        return false;
      }
      return 1500;
    },
  });
}

/** Query the caller's recent jobs, paginated by cursor. */
export function useJobs(
  client: Client,
  opts: { limit?: number; cursor?: string } = {},
) {
  return useQuery({
    queryKey: ["heisenberg", "jobs", opts],
    queryFn: () => client.jobs.list(opts),
  });
}

/** Query the chip targets known to the jobsvc instance. */
export function useTargets(client: Client) {
  return useQuery({
    queryKey: ["heisenberg", "targets"],
    queryFn: () => client.targets.list(),
    staleTime: 60_000,
  });
}

/**
 * Mutation: submit a job. On success, invalidates the jobs cache so
 * the new row appears on the next fetch.
 */
export function useCreateJob(client: Client) {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (body: CreateJobBody) => client.jobs.create(body),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["heisenberg", "jobs"] }),
  });
}

/** Mutation: cancel a non-terminal job. */
export function useCancelJob(client: Client) {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (id: string) => client.jobs.cancel(id),
    onSuccess: (_, id) => {
      qc.invalidateQueries({ queryKey: ["heisenberg", "job", id] });
      qc.invalidateQueries({ queryKey: ["heisenberg", "jobs"] });
    },
  });
}
