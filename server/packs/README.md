# The pack host

`https://packs.ma-r-s.com/` serves the Wikipedia packs the install page
copies onto a card (`site/wikipedia/`, `docs/apps/wikipedia-plan.md`). Plain
files from `/srv/packs/data` on the Orange Pi, nginx unprivileged, behind its
own Cloudflare Tunnel (`packs`), deployed the way the bridges are: no host
ports, its own subnet (172.31.86.0/24), the container read-only, an iptables
unit that walls it off from the LAN and the host.

Layout on the pi:

    /srv/packs/data/wikipedia/en-2026-05/   manifest.json, dict.zst, blocks.dir,
                                            titles.N.idx, shards/NNN.blk
    /srv/packs/data/wikipedia/en -> en-2026-05

The page reads the stable name. `scripts/publish_pack.sh` copies a new pack
beside the old and flips the link; shards are cached at the edge for a day
and the manifest never, so a new pack is seen within the day and every shard
is checked against the sha256 the manifest carries.

The Cloudflare side (tunnel, ingress `packs.ma-r-s.com -> http://packs:8080`,
proxied CNAME) was created with the `cf` CLI's OAuth token against the REST
API; the connector token lives in `/srv/packs/.env` (mode 600) and nowhere
else. Purging the edge cache and a "cache everything" rule for the host are
dashboard steps (the token has no cache or WAF scope).

Deploy: `scripts/deploy.sh` (ships, starts, installs the unit, runs
`scripts/isolation_test.sh`). A healthy container is not a confined one:
the isolation test is the only thing that says so.
