FROM python:3.13-slim

# No compiled dependencies at all: this service moves a 48KB bitmap and a JSON
# file and signs nothing. Shorter surface than the other two bridges, which is
# the point of copying the newer one.
COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

ENV PYTHONUNBUFFERED=1
WORKDIR /app
COPY bridge /app/bridge

# Public on the internet and holding pictures people meant for one other
# person. uid 10004: 10001 getbooks, 10002 ankibridge, 10003 readbridge on the
# same box, and a shared uid would let one compromised container read
# another's bind mount.
RUN useradd --uid 10004 --no-create-home --shell /usr/sbin/nologin fridgebridge \
 && mkdir -p /data && chown -R 10004:10004 /data /app
USER 10004:10004

EXPOSE 8080
CMD ["uvicorn", "bridge.app:app", "--host", "0.0.0.0", "--port", "8080", "--timeout-keep-alive", "75"]
