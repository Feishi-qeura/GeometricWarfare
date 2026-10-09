"""Local operator tools: safe metadata, explicit retries and WAL-aware backup."""
import argparse
import json
from pathlib import Path
import sqlite3
import time
from .service import Settings
from .store import Store


def backup_database(repository, destination):
    destination = Path(destination).resolve()
    if destination == repository.path.resolve() or destination.exists():
        raise ValueError("fresh_backup_destination_required")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with repository.connection() as origin:
        target = sqlite3.connect(destination)
        try:
            origin.backup(target)
        finally:
            target.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", default=Settings.from_env().database_path)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status")
    backup = sub.add_parser("backup")
    backup.add_argument("destination")
    retry = sub.add_parser("retry-failed")
    retry.add_argument("job_id", type=int)
    args = parser.parse_args()
    source = Path(args.database).resolve()
    if not source.is_file():
        parser.error("database does not exist; specify the configured persistent database")
    repository = Store(source)
    if args.command == "status":
        print(json.dumps(repository.operator_status(), indent=2))
    elif args.command == "retry-failed":
        print(json.dumps({"requeued": repository.retry_failed(args.job_id, now=int(time.time()))}))
    elif args.command == "backup":
        destination = Path(args.destination).resolve()
        if destination == source or destination.exists():
            parser.error("backup destination must be a fresh file outside the website root")
        backup_database(repository, destination)
        print(json.dumps({"backup_created": True}))


if __name__ == "__main__":
    main()
