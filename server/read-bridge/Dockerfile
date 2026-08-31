FROM python:3.13-slim

# No compiled extras beyond cryptography's wheel: this service parses HTML
# with the standard library and signs with hmac, so the image stays small and
# its dependency surface stays short. That is deliberate -- everything here
# handles bytes from the public internet on behalf of a credential store.
COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

ENV PYTHONUNBUFFERED=1
WORKDIR /app
COPY bridge /app/bridge

# Reachable from the public internet and holds Instapaper OAuth tokens. It has
# no business being root. uid 10003: 10001 is getbooks, 10002 is ankibridge on
# the same box, and shared uids would let one compromised container touch
# another's bind mounts.
RUN useradd --uid 10003 --no-create-home --shell /usr/sbin/nologin readbridge \
 && mkdir -p /data && chown -R 10003:10003 /data /app
USER 10003:10003

EXPOSE 8080
# No --workers flag: workers=1 is load-bearing, the per-user asyncio mutex
# stops being a mutex above one worker (see compose.yaml).
CMD ["uvicorn", "bridge.app:app", "--host", "0.0.0.0", "--port", "8080", "--timeout-keep-alive", "75"]
