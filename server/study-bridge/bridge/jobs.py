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

from . import events, store
from .engine import Frozen

log = logging.getLogger("bridge.jobs")

# The job clock, as a name the tests replace to pin the seconds an event
# reports. time.monotonic itself stays alone: asyncio's loop reads it too.
_clock = time.monotonic


def _report(service, device, failure, summary, props, elapsed):
    """One event per finished job: what it moved, or what it died of. Never
    raises -- the job is already done or failed on its own terms, and the
    board is not allowed to change that."""
    try:
        if failure is None:
            p = dict(props(summary) if props else {})
            p["seconds"] = round(elapsed, 1)
            events.post(service, "sync", device=device, props=p)
        else:
            message = f"{type(failure).__name__}: {failure}"[:200]
            events.post(
                service,
                "sync",
                device=device,
                level="error",
                props={"message": message},
            )
    except Exception:
        log.exception("event for a %s job not posted", service)


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

    def start(
        self, uid: str, work, *, service: str = "", device: str = "", props=None
    ) -> Job:
        """work: a blocking callable run in a thread under the user lock.
        Returns the existing job instead when one is already in flight.

        service names the board's row for this job (events.py): when set,
        the job's end posts one event -- `sync` with props(summary) plus the
        seconds it took, or `sync` at level error with what it died of.
        device is the hashed id the event is counted under."""
        existing = self.active_for(uid)
        if existing:
            return existing
        job = Job(uid)
        self._jobs[job.id] = job
        self._active[uid] = job.id

        async def run():
            async with store.LOCKS.for_user(uid):
                job.status = "running"
                started = _clock()
                failure = None
                try:
                    job.summary = await asyncio.to_thread(work)
                    job.status = "done"
                except Frozen as e:
                    job.status = "frozen"
                    job.message = str(e)
                    log.warning("job %s frozen: %s", job.id, e)
                    failure = e
                except Exception as e:
                    job.status = "error"
                    job.message = (
                        "Syncing hit a problem on the bridge. Your reviews are"
                        " safe; try again in a while."
                    )
                    log.exception("job %s failed", job.id)
                    failure = e
                if service:
                    _report(
                        service, device, failure, job.summary, props, _clock() - started
                    )

        asyncio.get_running_loop().create_task(run())
        return job

    def gc(self, max_age_s: int = 3600):
        cutoff = time.time() - max_age_s
        for jid in [j for j, job in self._jobs.items() if job.created < cutoff]:
            job = self._jobs.pop(jid)
            if self._active.get(job.uid) == jid:
                del self._active[job.uid]


JOBS = Jobs()
