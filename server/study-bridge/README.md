# Ankibridge

An Anki sync bridge for the study app: a FastAPI service (uvicorn, port 8080
inside the container, `bridge.app:app`) that syncs a user's collection from
AnkiWeb with the `anki` library, converts it with the shared converter from
`tools_local/study`, and serves it to the device. Runs on the Orange Pi at
`/srv/ankibridge` behind a Cloudflare Tunnel. It holds AnkiWeb credentials
(Fernet-encrypted at rest) and user collections, and only usernames in
`BRIDGE_ALLOWLIST` are served, so the hardening bar is Getbooks' or higher.

## Isolation

The box also runs Immich, Jellyfin and the *arr stack, so this container is
treated as one that will eventually be compromised.

- runs as uid 10002 (10001 is getbooks), `read_only` root filesystem,
  `cap_drop: ALL`, `no-new-privileges`, capped memory/pids/cpu, `/tmp` a
  512 MB tmpfs because the anki library needs a writable temp dir and a
  hostile sync payload must not fill the disk
- its own bridge on a pinned subnet (`172.31.84.0/24`; `172.31.83.0/24` is
  getbooks), nothing published on the host: cloudflared reaches it over the
  compose network, so nothing on the LAN can hit it unauthenticated
- `scripts/firewall.sh` drops traffic from that subnet to every RFC1918
  range, the tailnet and link-local, on **both** `DOCKER-USER` and `INPUT` --
  host services never traverse FORWARD, so DOCKER-USER alone leaves SSH open.
  Installed as `ankibridge-firewall.service` so it survives reboots.
- DNS is pinned to 1.1.1.1/9.9.9.9. Docker's embedded resolver forwards from
  inside the container's namespace, so walling off the host otherwise breaks
  name resolution; pointing it at the internet avoids a port 53 hole back home.

Verify after **every** deploy, not just firewall changes:

```sh
ssh orange 'bash -s' < scripts/isolation_test.sh
```

Every private-network probe must time out and egress to AnkiWeb must work;
the script exits nonzero on any breach.

## Operating it

```sh
./scripts/deploy.sh                                        # stage, ship, rebuild
ssh orange 'cd /srv/ankibridge && docker compose up -d --build'   # rebuild only
ssh orange 'cd /srv/ankibridge && docker compose logs -f'         # sync activity, failures
ssh orange 'cd /srv/ankibridge && docker compose stop cloudflared'  # kill switch: public endpoint off
```

The containers are labelled `com.centurylinklabs.watchtower.enable=false`
because watchtower updates every other container on that box.

Backup: nightly snapshot of the authoritative state only (the credential store
and per-user sync state under `/srv/ankibridge/data`, not the rebuildable
converted output) to `/mnt/hdd/backups/ankibridge`. **TODO: wiring not done.**

## Secrets

`.env` lives on the pi only, at `/srv/ankibridge/.env`, mode 600, never in
git. Keys: `BRIDGE_FERNET_KEY`, `CLOUDFLARE_TUNNEL_TOKEN`, `BRIDGE_ALLOWLIST`.
It is never rsync'd in either direction (`deploy.sh` excludes it, and that
exclude is load-bearing) and never pasted into commands, where it would land
in shell history and `ps` output. Edit it in place on the pi.
