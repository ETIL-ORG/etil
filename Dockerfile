# Runtime-only image — binary pre-built locally and staged into .docker-stage/
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 libssl3t64 zlib1g ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY .docker-stage/bin/etil_mcp_server /usr/local/bin/
COPY data/ /app/data/

# Create volume mount points with proper ownership for nobody:nogroup.
# Docker initializes named volume permissions from the image directory on first
# mount, so nobody can write to /data/sessions even with read_only: true.
# /etc/etil is for auth config (roles.json, keys.json, users.json + JWT keys) — read-only bind mount.
RUN mkdir -p /data/sessions /data/library /etc/etil \
    && chown nobody:nogroup /data/sessions /data/library /etc/etil

WORKDIR /app
EXPOSE 8080
USER nobody:nogroup
ENTRYPOINT ["etil_mcp_server"]
CMD ["--host", "0.0.0.0", "--port", "8080"]
