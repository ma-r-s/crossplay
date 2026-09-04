# Deploying read-bridge for the first time

Written to be executed by Mario, or by a session with his direct word, because
every step below touches his server or publishes a service that accepts other
people's passwords. Nothing here should be improvised from a relayed message.

## Order matters, and this is why

The Instapaper application is in **owner only** mode until its review is
approved. The consumer key works immediately for the account that registered
it and for nobody else. So the first deployment proves the whole path on one
real account, and the service stays closed while it does.

**Do not set `READ_ALLOWLIST=*` yet.** Until the review lands, an open
allowlist lets strangers reach the sign-in endpoint with credentials Instapaper
will refuse anyway: it adds no capability and exposes the one endpoint that is
a credential-stuffing oracle by construction. Worse, the only real defence
against distributed stuffing is Cloudflare-side rate limiting, which is still
an open item -- the in-process limiters in `bridge/ratelimit.py` are defeated
by an attacker with many addresses, by construction and by their own docstring.

Open it when BOTH are true: the review is approved, and the Cloudflare rules
exist.

## Status: deployed 2026-08-31, NOT yet public

Steps 1-3 are done. `/srv/readbridge` holds the service, the container is
healthy, the firewall is installed and enabled, and the isolation test passes
with every private probe timing out and egress to instapaper.com working.

What remains is step 4 and it is the only thing between here and a working
sync: no `CLOUDFLARE_TUNNEL_TOKEN`, so `readbridge-cloudflared` has been
STOPPED rather than left crash-looping, and nothing is published on the host.
The service is reachable only from inside the pi. That is the correct state to
be in while the hostname is still being sequenced.

**A THING THE FIRST DEPLOY GOT WRONG, so the next one does not.** `deploy.sh`
ships the service and starts it; it does NOT install the firewall. The
container came up unconfined and the isolation test caught it reaching the
host's SSH, Immich, the router and a tailnet peer. Install the unit BEFORE or
immediately after the first `docker compose up`, and never treat a green deploy
as an isolated one:

    ssh orange 'sudo /srv/readbridge/scripts/firewall.sh'
    # then the systemd unit, so it survives a reboot, modelled on
    # getbooks-firewall.service and ankibridge-firewall.service
    ssh orange 'sudo systemctl enable --now readbridge-firewall.service'

## 1. Secrets on the pi, and only there

`/srv/readbridge/.env`, mode 600. `deploy.sh` excludes it in both directions,
so it is never copied up and never overwritten.

    ssh orange 'umask 077 && cat >> /srv/readbridge/.env' <<'ENV'
    READ_CONSUMER_KEY=<from instapaper.com/main/request_oauth_consumer_token>
    READ_CONSUMER_SECRET=<same page>
    READ_ALLOWLIST=<mario's instapaper address>
    ENV
    ssh orange 'chmod 600 /srv/readbridge/.env && ls -l /srv/readbridge/.env'

Optional, for the board's numbers (`docs/workflow/events.md`; the values are
the URL and ANON key in `<workspace>/.board/supabase.env`, never the service
role key). Without both, the service posts nothing and logs so once:

    SUPABASE_URL=https://<project>.supabase.co
    SUPABASE_ANON_KEY=<the public anon key>

`READ_FERNET_KEY` is generated ON the pi and never leaves it:

    ssh orange "docker run --rm python:3.13-slim sh -c \
      'pip -q install cryptography && python -c \
      \"from cryptography.fernet import Fernet;print(Fernet.generate_key().decode())\"'"

## 2. Ship and start

    bash server/read-bridge/scripts/deploy.sh

## 3. Verify the isolation, every deploy and not only firewall changes

    ssh orange 'bash -s' < server/read-bridge/scripts/isolation_test.sh

Every private-network probe must time out and egress to instapaper.com must
work. Nonzero exit is a breach; do not continue.

## 4. The hostname

`read.crossplay.ma-r-s.com` into the tunnel, the way `sync.ma-r-s.com` was. The firmware
constant is `kBridgeHost` in `src/apps_local/instapaper/InstapaperSync.h`, and
the device build has NO override -- it can only ever reach that one name.

## 5. Then, and only then, the thing nobody has run

Sign in at `https://read.crossplay.ma-r-s.com` with the owner's account, pair a reader,
and sync. That path has never executed: the wolfSSL handshake, the ~35KB heap
floor it needs, certificate verification against Cloudflare's chain, and the
four assumptions about Instapaper's API in `docs/apps/instapaper-plan.md`.

Study's equivalent cost three flags and a `-232` that looked like a curve
problem and was a missing hash. Expect to spend time here rather than none.

## 6. Later: open it

The app is **submitted for review** (pressed 2026-08-31). When approval lands,
set `READ_ALLOWLIST=*` and add the Cloudflare rate-limiting rules IN THE SAME
SITTING. Not before, and not one without the other -- an open allowlist without
those rules is the credential-stuffing oracle this ordering exists to avoid.

## Before any of it: can this machine even reach the pi?

    ssh orange 'echo up'                          # the deploy path
    curl -sS https://sync.ma-r-s.com/healthz      # is the pi serving at all?

Those answer different questions and on 2026-08-31 they disagreed, which sent
one session to the wrong conclusion. The Anki bridge answered 200 -- the box
was up, cloudflared was up, it was serving the public internet -- while `ssh
orange` could not resolve, because this machine had no route to it under that
name: no `~/.ssh/config` entry, and `known_hosts` holding 192.168.68.x,
192.168.1.x and tailscale 100.x addresses while the machine sat on
192.168.20.0/24.

So: a failing `ssh` says nothing about the pi. Check the tunnel before
concluding anything about the box, and note that `read.crossplay.ma-r-s.com` failing with
a DNS error rather than a connection error means step 4 has not been done --
not that anything is broken.
