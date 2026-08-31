"""One slow job per user, status by polling.

Twin of server/study-bridge/bridge/jobs.py. POST /api/sync must return
immediately -- an e-ink device's HTTP client will not hold a request open
while the bridge fetches and converts a dozen articles -- so the slow part
runs here: an asyncio task wrapping the blocking cycle in a thread,
serialised per user by store.LOCKS. One job per user at a time; a second POST
while one runs gets the running job's id back rather than a queue.
"""

import asyncio
import logging
import secrets
import time

from . import store

log = logging.getLogger("bridge.jobs")


class Refused(Exception):
    """A failure with a sentence worth showing the user verbatim -- an
    Instapaper refusal rather than a bridge fault. Anything else becomes the
    generic message below, because an unexpected traceback has no sentence."""


class Job:
    def __init__(self, uid: str):
        self.id = secrets.token_urlsafe(12)
        self.uid = uid
        self.status = "queued"  # queued | running | done | error
        self.message = ""
        self.summary: dict = {}
        self.created = time.time()


class Jobs:
    def __init__(self):
        self._jobs: dict[str, Job] = {}
        self._active: dict[str, str] = {}

    def get(self, job_id: str) -> Job | None:
        return self._jobs.get(job_id)

    def active_for(self, uid: str) -> Job | None:
        jid = self._active.get(uid)
        job = self._jobs.get(jid) if jid else None
        if job and job.status in ("queued", "running"):
            return job
        return None

    def start(self, uid: str, work) -> Job:
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
                except Refused as e:
                    job.status = "error"
                    job.message = str(e)
                    log.info("job %s refused: %s", job.id, e)
                except Exception:
                    job.status = "error"
                    job.message = (
                        "Syncing hit a problem on the bridge. Nothing on your"
                        " reader was lost; try again in a while."
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
