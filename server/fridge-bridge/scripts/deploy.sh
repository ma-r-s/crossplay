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
sleep 4
ssh "$HOST" "curl -fsS -m 5 http://127.0.0.1:8098/healthz" \
  || echo "WARNING: deployed, but /healthz did not answer. Check: ssh $HOST docker logs fridgebridge"
