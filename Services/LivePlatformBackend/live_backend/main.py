"""Native Windows/Linux single-instance entry point, always loopback bound."""
import os
import uvicorn
from .app import create_app


def main():
    port = int(os.getenv("BACKEND_PORT", "8765"))
    if not 1024 <= port <= 65535:
        raise ValueError("invalid_port")
    root_path = os.getenv("BACKEND_ROOT_PATH", "")
    if root_path and (not root_path.startswith("/") or ".." in root_path or "?" in root_path):
        raise ValueError("invalid_root_path")
    uvicorn.run(create_app(), host="127.0.0.1", port=port, workers=1, access_log=False,
                proxy_headers=True, forwarded_allow_ips="127.0.0.1", root_path=root_path)


if __name__ == "__main__":
    main()
