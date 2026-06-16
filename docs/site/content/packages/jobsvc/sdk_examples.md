# SDK examples — `jobsvc`

Driving the API from Python.

## Minimal client

```python
import httpx
import time

class Client:
    def __init__(self, base, email, password):
        self.base = base
        self.s = httpx.Client(base_url=base, timeout=30.0)
        r = self.s.post("/api/v1/login",
                        json={"email": email, "password": password}).json()
        self.s.headers["Authorization"] = f"Bearer {r['access_token']}"

    def submit(self, source, source_kind, target, shots=1000, name=""):
        return self.s.post("/api/v1/jobs", json={
            "source": source, "source_kind": source_kind,
            "target": target, "shots": shots, "name": name,
        }).json()

    def get(self, job_id):
        return self.s.get(f"/api/v1/jobs/{job_id}").json()

    def wait(self, job_id, interval=1.0, timeout=600):
        start = time.monotonic()
        while True:
            j = self.get(job_id)
            if j["state"] in ("Completed", "Rejected", "Failed"):
                return j
            if time.monotonic() - start > timeout:
                raise TimeoutError(j["state"])
            time.sleep(interval)

c = Client("http://localhost:8000", "admin@local", "admin-password")
job = c.submit("target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n",
               "spinor", "generic", shots=1000)
print(job["estimate"])

result = c.wait(job["id"])
print(result["result"]["counts"])
```

## Using an API key

```python
client = httpx.Client(base_url="http://localhost:8000",
                      headers={"X-API-Key": "Q4r2p8aA.GvXc..."})
job = client.post("/api/v1/jobs", json={...}).json()
```

## Async client

```python
import httpx, asyncio

async def main():
    async with httpx.AsyncClient(base_url="http://localhost:8000") as c:
        tokens = (await c.post("/api/v1/login", json={"email": "...", "password": "..."})).json()
        c.headers["Authorization"] = f"Bearer {tokens['access_token']}"
        # ...

asyncio.run(main())
```

## See also

[REST reference](../../api/rest/index.html), [Python autodoc](../../api/python/jobsvc/index.md), [Cookbook](cookbook.md)
