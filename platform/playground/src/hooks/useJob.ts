import { useEffect, useState } from "react";
import { api, JobDetail } from "../api/jobs";

const TERMINAL = new Set(["Completed", "Rejected", "Failed"]);

/** Polls GET /jobs/{id} until the job reaches a terminal state.
 *  Backoff: 1s -> 2s -> 4s, capped at 5s. Gives up after 30 minutes.
 */
export function useJobPoll(id: string | null) {
  const [job, setJob] = useState<JobDetail | null>(null);
  const [error, setError] = useState<unknown>(null);
  const [polling, setPolling] = useState<boolean>(Boolean(id));

  useEffect(() => {
    if (!id) {
      setJob(null);
      setPolling(false);
      return;
    }
    let cancelled = false;
    let interval = 1000;
    const started = Date.now();

    const tick = async () => {
      if (cancelled) return;
      try {
        const j = await api.getJob(id);
        if (cancelled) return;
        setJob(j);
        if (TERMINAL.has(j.state)) {
          setPolling(false);
          return;
        }
        if (Date.now() - started > 30 * 60 * 1000) {
          setError(new Error("polling timeout (30 minutes)"));
          setPolling(false);
          return;
        }
        interval = Math.min(interval * 2, 5000);
        setTimeout(tick, interval);
      } catch (exc) {
        setError(exc);
        setPolling(false);
      }
    };

    setPolling(true);
    setError(null);
    tick();
    return () => {
      cancelled = true;
    };
  }, [id]);

  return { job, error, polling };
}
