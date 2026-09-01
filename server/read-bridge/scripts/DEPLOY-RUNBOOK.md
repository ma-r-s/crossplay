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

## 1. Secrets on the pi, and only there

`/srv/readbridge/.env`, mode 600. `deploy.sh` excludes it in both directions,
so it is never copied up and never overwritten.

    ssh orange 'umask 077 && cat >> /srv/readbridge/.env' <<'ENV'
    READ_CONSUMER_KEY=<from instapaper.com/main/request_oauth_consumer_token>
    READ_CONSUMER_SECRET=<same page>
    READ_ALLOWLIST=<mario's instapaper address>
    ENV
    ssh orange 'chmod 600 /srv/readbridge/.env && ls -l /srv/readbridge/.env'

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

`read.ma-r-s.com` into the tunnel, the way `sync.ma-r-s.com` was. The firmware
constant is `kBridgeHost` in `src/apps_local/instapaper/InstapaperSync.h`, and
the device build has NO override -- it can only ever reach that one name.

## 5. Then, and only then, the thing nobody has run

Sign in at `https://read.ma-r-s.com` with the owner's account, pair a reader,
and sync. That path has never executed: the wolfSSL handshake, the ~35KB heap
floor it needs, certificate verification against Cloudflare's chain, and the
four assumptions about Instapaper's API in `docs/apps/instapaper-plan.md`.

Study's equivalent cost three flags and a `-232` that looked like a curve
problem and was a missing hash. Expect to spend time here rather than none.

## 6. Later: open it

Submit the app for review; when approved, set `READ_ALLOWLIST=*` and add the
Cloudflare rate-limiting rules in the same sitting. Not before.
