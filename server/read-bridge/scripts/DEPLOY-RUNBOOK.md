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

## Status: published 2026-09-03 at https://read.ma-r-s.com, allowlist still CLOSED

Steps 1-4 are done. `/srv/readbridge` holds the service, both containers are
up, the firewall is installed and enabled, and the isolation test passes from
BOTH vantage points with every private probe timing out and egress to
instapaper.com working. The tunnel is `readbridge`
(`43692df2-e226-4e7f-bc72-72cfdf326572`), its single ingress rule maps
`read.ma-r-s.com` to `http://readbridge:8080`, and the zone holds a proxied
CNAME to `<tunnel-id>.cfargotunnel.com` exactly like `sync` and `books`.

Verified from a laptop, not from a deploy command's exit status:
`/healthz` 200, the sign-in page renders, `POST /api/pair/start` returns a
code and a pollToken, `GET /api/pair/poll` answers `{"pending":true}`, and
`POST /api/pair/abandon` cleans the code up.

What remains is step 5 (nobody has signed in and paired a real reader) and
step 6 (opening the allowlist). **`READ_ALLOWLIST` is untouched and still the
owner's address only.** Publishing the hostname does not change who may sign
in, and it must not: see step 6.

The kill switch is one command, and it leaves everything else on the box
alone:

    ssh orange 'cd /srv/readbridge && docker compose stop cloudflared'

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

## 4. The hostname, and the reason it is not the one this file used to name

`read.ma-r-s.com` into the tunnel, the way `sync.ma-r-s.com` was. The firmware
constant is `kBridgeHost` in `src/apps_local/instapaper/InstapaperSync.h`, and
the device build has NO override -- it can only ever reach that one name.

**It said `read.crossplay.ma-r-s.com` until 2026-09-03, and that name can
never work on this account.** ma-r-s.com is on Cloudflare's free plan, whose
Universal SSL certificate covers exactly `ma-r-s.com` and `*.ma-r-s.com`: ONE
label. The edge answers a TLS handshake for a deeper name with alert 40 and no
certificate at all, which you can see without deploying anything:

    echo | openssl s_client -connect 104.21.2.163:443 \
      -servername read.crossplay.ma-r-s.com
    # ... tls alert handshake failure ... no peer certificate available

So the DNS record was never the missing piece on its own. A CNAME for that
name would have resolved and then failed TLS in the browser and on the device,
which is a worse failure than a missing record because it looks like a broken
service rather than an unfinished one. Covering `*.crossplay.ma-r-s.com` needs
Advanced Certificate Manager, which is paid; a one-label name is free and is
what both working siblings already use.

Two consequences worth carrying:

- **`app/crossplayhosts` would break Study sync and Get Books the same way.**
  It moves `sync.ma-r-s.com` to `sync.crossplay.ma-r-s.com` and
  `books.ma-r-s.com` to `books.crossplay.ma-r-s.com`. Both are two labels
  deep. Do not ship it without buying the certificate first.
- The certificate now served for `read.ma-r-s.com` is the SAME certificate,
  same serial, that `sync.ma-r-s.com` presents -- and the device's wolfSSL
  root store is already proven against that chain by Study sync on hardware.
  One less unknown in step 5.

Created with the Cloudflare REST API (the `cf` CLI has no `tunnel` command;
its stored OAuth token works as a Bearer):

    POST /accounts/<acct>/cfd_tunnel            {"name":"readbridge","config_src":"cloudflare"}
    PUT  /accounts/<acct>/cfd_tunnel/<id>/configurations
         {"config":{"ingress":[{"hostname":"read.ma-r-s.com","service":"http://readbridge:8080"},
                               {"service":"http_status:404"}],"warp-routing":{"enabled":false}}}
    POST /zones/<zone>/dns_records              CNAME read -> <id>.cfargotunnel.com, proxied

The tunnel token goes into `/srv/readbridge/.env` and nowhere else. Pipe it;
do not paste it, and do not let it reach a terminal:

    GET /accounts/<acct>/cfd_tunnel/<id>/token  |  ssh orange 'umask 077 && cat >> /srv/readbridge/.env'

## 5. Then, and only then, the thing nobody has run

Sign in at `https://read.ma-r-s.com` with the owner's account, pair a reader,
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
concluding anything about the box, and read the failure carefully: a DNS error
on `read.ma-r-s.com` means the record is gone, a TLS alert means the name is
too deep for the zone's certificate (step 4), and a 502 means the tunnel is up
but cannot reach the service container. Those are three different repairs.
