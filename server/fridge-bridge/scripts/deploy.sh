#!/bin/sh
# Ship this service to the box and restart it.
#
#   server/fridge-bridge/scripts/deploy.sh
#
# Shaped after read-bridge's. Two things it does that are not obvious:
#
#   * chowns the bind mount through a THROWAWAY CONTAINER rather than with a
#     local chown. The deploying user is not root on the box, so a plain chown
#     fails silently and the service then 500s on its first write with
#     "Permission denied: /data/fridges" -- which is how this was first
#     deployed.
#   * stamps BUILD with the git short sha, so the running service can be asked
#     what it is. A fix that is merged and not deployed looks exactly like a
#     fix that does not work.
set -e
HOST="${FRIDGE_HOST:-orange}"
DEST="${FRIDGE_DEST:-/srv/fridgebridge}"
cd "$(dirname "$0")/.."

git rev-parse --short HEAD > BUILD 2>/dev/null || echo unknown > BUILD
rsync -az --delete --exclude data --exclude .env --exclude compose.override.yaml ./ "$HOST:$DEST/"
rm -f BUILD

ssh "$HOST" "cd $DEST \
 && docker run --rm -v $DEST/data:/data alpine:3 chown -R 10004:10004 /data \
 && docker compose up -d --build"

# Not the exit status of the deploy: a container that starts and then dies on
# its first request exits 0 here. Ask the service itself.
#
# THROUGH THE CONTAINER, not through a host port. This asked 127.0.0.1:8098 and
# that port has never existed: compose only EXPOSES 8080 on the private network,
# where the cloudflared sidecar reaches it, and publishes nothing to the host.
# So every deploy this service has ever had ended in "WARNING: /healthz did not
# answer" over a service that was answering perfectly well, which is worse than
# no check at all -- a warning that is always there is a warning nobody reads.
#
# AND IT ANSWERS WITH THE SHA IT IS RUNNING, which is the question worth
# asking: `docker compose up -d --build` prints "Container Running" when it
# decided nothing needed recreating, and that is indistinguishable from a
# rebuild. Compared against the sha this script just stamped, so a deploy that
# did not take says so instead of looking identical to one that did.
sleep 4
WANT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
GOT="$(ssh "$HOST" "docker exec fridgebridge python -c \
  \"import urllib.request; print(urllib.request.urlopen('http://127.0.0.1:8080/healthz', timeout=5).read().decode())\"" 2>/dev/null || true)"
case "$GOT" in
  "ok $WANT") echo "healthz: ok, running $WANT" ;;
  ok\ *)      echo "WARNING: /healthz answers '$GOT' but this tree is $WANT; the container is NOT running what you just deployed." ;;
  *)          echo "WARNING: deployed, but /healthz did not answer. Check: ssh $HOST docker logs fridgebridge" ;;
esac
