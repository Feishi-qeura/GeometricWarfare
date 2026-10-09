FROM python:3.12-slim
WORKDIR /app
COPY requirements.lock.txt .
RUN pip install --no-cache-dir -r requirements.lock.txt
COPY live_backend ./live_backend
RUN mkdir /data && useradd --uid 10001 --no-create-home backend && chown backend /data
USER backend
ENV BACKEND_DATABASE_PATH=/data/live-backend.sqlite3
# main intentionally binds loopback. A future Linux deployment must colocate
# its HTTPS proxy in the same network namespace (or provide a reviewed adapter).
# Do not expose a plaintext bind to 0.0.0.0 as a shortcut.
CMD ["python", "-m", "live_backend.main"]
