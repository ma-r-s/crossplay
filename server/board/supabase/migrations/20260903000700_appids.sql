-- A session has two ids: the one the hooks see (SessionStart prints it) and
-- the desktop app's local_... id that the app's messaging tool addresses. A
-- claim holds both, so a worker can reach the orchestrator through either.
alter table claims add column if not exists app_session text;
