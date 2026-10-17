#!/bin/sh
# Create fridge.ma-r-s.com: one Cloudflare tunnel, one DNS record, one restart.
#
#   CF_API_TOKEN=... server/fridge-bridge/scripts/create-hostname.sh
#
# Needs a token with Zone:DNS:Edit and Account:Cloudflare Tunnel:Edit on the
# ma-r-s.com zone. Make one at
# dash.cloudflare.com/profile/api-tokens, or re-auth the cf CLI and take its
# token. The one cached on this Mac has expired, which is why this is a script
# and not something already done.
#
# THE HOSTNAME MUST BE EXACTLY ONE LABEL BELOW THE APEX. Cloudflare's Universal
# SSL on the free plan covers ma-r-s.com and *.ma-r-s.com and nothing deeper,
# so fridge.crossplay.ma-r-s.com would get no certificate at all and the reader
# would see a TLS alert with no peer cert. Measured, not assumed: the three
# bridges already on this zone serve CN=ma-r-s.com chaining GTS WE1 -> GTS Root
# R4, which is in the firmware's baked bundle.
set -e
: "${CF_API_TOKEN:?set CF_API_TOKEN}"
HOST="${HOST:-orange}"
NAME="${NAME:-fridge}"
ZONE="${ZONE:-ma-r-s.com}"
API=https://api.cloudflare.com/client/v4
cf() { curl -sS -H "Authorization: Bearer $CF_API_TOKEN" -H "content-type: application/json" "$@"; }
ok() { python3 -c "import sys,json; d=json.load(sys.stdin); sys.exit(0 if d.get('success') else (print(d.get('errors'),file=sys.stderr) or 1))"; }

echo "zone..."
Z=$(cf "$API/zones?name=$ZONE")
echo "$Z" | ok
ZID=$(echo "$Z" | python3 -c "import sys,json;print(json.load(sys.stdin)['result'][0]['id'])")
ACC=$(echo "$Z" | python3 -c "import sys,json;print(json.load(sys.stdin)['result'][0]['account']['id'])")

echo "tunnel..."
T=$(cf -X POST "$API/accounts/$ACC/cfd_tunnel" \
     -d "{\"name\":\"fridgebridge\",\"config_src\":\"cloudflare\"}")
echo "$T" | ok
TID=$(echo "$T" | python3 -c "import sys,json;print(json.load(sys.stdin)['result']['id'])")
TOK=$(cf "$API/accounts/$ACC/cfd_tunnel/$TID/token" \
      | python3 -c "import sys,json;print(json.load(sys.stdin)['result'])")

echo "ingress..."
# The service is not published on the host, so the tunnel reaches it by
# container name over the compose network cloudflared shares with it.
cf -X PUT "$API/accounts/$ACC/cfd_tunnel/$TID/configurations" -d '{
  "config": {"ingress": [
    {"hostname": "'"$NAME.$ZONE"'", "service": "http://fridgebridge:8080"},
    {"service": "http_status:404"}
  ]}}' | ok

echo "dns..."
cf -X POST "$API/zones/$ZID/dns_records" -d "{
  \"type\":\"CNAME\",\"name\":\"$NAME\",\"content\":\"$TID.cfargotunnel.com\",
  \"proxied\":true,\"comment\":\"Live (fridge-bridge) on the orange pi\"}" | ok

echo "starting the tunnel on $HOST..."
ssh "$HOST" "cd /srv/fridgebridge \
 && grep -q FRIDGE_TUNNEL_TOKEN .env 2>/dev/null || echo 'FRIDGE_TUNNEL_TOKEN=$TOK' >> .env \
 && rm -f compose.override.yaml \
 && docker compose up -d"

echo
echo "give the edge a minute, then:"
echo "  curl -sS https://$NAME.$ZONE/healthz"
echo
echo "and check the chain the reader will see is the one it trusts:"
echo "  echo | openssl s_client -connect $NAME.$ZONE:443 -servername $NAME.$ZONE 2>/dev/null | grep 's:'"
echo "  (must end at GTS Root R4, not ISRG Root YR)"
