"""One slow job per user, status by polling.

POST /api/sync must return immediately (a first sync moves gigabytes and an
e-ink device's HTTP client will not hold a request open for it), so the slow
part runs here: an asyncio task wrapping the blocking cycle in a thread,
serialized per user by store.LOCKS. One job per user at a time; a second
POST while one runs gets the running job's id back rather than a queue.
"""

import asyncio
import logging
import secrets
import time

from . import store
from .engine import Frozen

log = logging.getLogger("bridge.jobs")


class Job:
    def __init__(self, uid: str):
        self.id = secrets.token_urlsafe(12)
        self.uid = uid
        self.status = "queued"  # queued | running | done | error | frozen
        self.message = ""  # user-facing sentence when error/frozen
        self.summary: dict = {}
        self.created = time.time()


class Jobs:
    def __init__(self):
        self._jobs: dict[str, Job] = {}
        self._active: dict[str, str] = {}  # uid -> job id

    def get(self, job_id: str) -> Job | None:
        return self._jobs.get(job_id)

    def active_for(self, uid: str) -> Job | None:
        jid = self._active.get(uid)
        job = self._jobs.get(jid) if jid else None
        if job and job.status in ("queued", "running"):
            return job
        return None

    def start(self, uid: str, work) -> Job:
        """work: a blocking callable run in a thread under the user lock.
        Returns the existing job instead when one is already in flight."""
        existing = self.active_for(uid)
        if existing:
            return existing
        job = Job(uid)
        self._jobs[job.id] = job
        self._active[uid] = job.id

        async def run():
            async with store.LOCKS.for_user(uid):
                job.status = "running"
                try:
                    job.summary = await asyncio.to_thread(work)
                    job.status = "done"
                except Frozen as e:
                    job.status = "frozen"
                    job.message = str(e)
                    log.warning("job %s frozen: %s", job.id, e)
                except Exception:
                    job.status = "error"
                    job.message = (
                        "Syncing hit a problem on the bridge. Your reviews are"
                        " safe; try again in a while."
                    )
                    log.exception("job %s failed", job.id)

        asyncio.get_running_loop().create_task(run())
        return job

    def gc(self, max_age_s: int = 3600):
        cutoff = time.time() - max_age_s
        for jid in [j for j, job in self._jobs.items() if job.created < cutoff]:
            job = self._jobs.pop(jid)
            if self._active.get(job.uid) == jid:
                del self._active[job.uid]


JOBS = Jobs()
