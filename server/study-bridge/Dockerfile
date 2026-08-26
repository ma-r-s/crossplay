FROM python:3.13-slim

# anki ships manylinux_2_35 aarch64 wheels, so no Rust toolchain is needed;
# fonttools/freetype-py/pillow are for the deck builds (font subsetting and
# sentence images), python-multipart for the login and pairing forms.
RUN pip install --no-cache-dir \
      anki==26.8.1 fastapi==0.115.6 "uvicorn[standard]==0.32.1" \
      cryptography==44.0.0 python-multipart==0.0.20 \
      fonttools==4.56.0 freetype-py==2.5.1 pillow==11.3.0

# The converter is shared source from the repo, vendored into the image at
# build time by the deploy script, so the image never depends on the repo
# layout at runtime and a checkout drift can never alter what runs.
COPY tools_local /app/tools_local

# The converter uses flat internal imports, so its study/ directory goes on
# the path and its modules are imported top-level rather than as a package.
ENV PYTHONPATH=/app:/app/tools_local/study
ENV PYTHONUNBUFFERED=1

WORKDIR /app
COPY bridge /app/bridge

# This process is reachable from the public internet and holds AnkiWeb
# credentials and user collections. It has no business being root.
# uid 10002: 10001 is getbooks on the same box.
RUN useradd --uid 10002 --no-create-home --shell /usr/sbin/nologin ankibridge \
 && mkdir -p /data && chown -R 10002:10002 /data /app
USER 10002:10002

EXPOSE 8080
# No --workers flag: workers=1 is load-bearing, the per-user asyncio mutex
# stops being a mutex above one worker (see compose.yaml).
CMD ["uvicorn", "bridge.app:app", "--host", "0.0.0.0", "--port", "8080", "--timeout-keep-alive", "75"]
