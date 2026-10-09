"""Narrow local recovery of the exact response-shape incident; no API calls."""
import argparse
from contextlib import closing
import json
import os
from pathlib import Path
import re
import sqlite3
import sys
import time


def context(backend_directory, environment_file=None):
    root = Path(backend_directory).resolve()
    env_file = Path(environment_file).resolve() if environment_file else root / '.env'
    for line in env_file.read_text(encoding='utf-8-sig').splitlines():
        if not line.strip() or line.lstrip().startswith('#'): continue
        key, separator, value = line.partition('=')
        if not separator or not re.fullmatch(r'BACKEND_[A-Z0-9_]+', key): raise ValueError('invalid_environment_file')
        os.environ[key] = value
    sys.path.insert(0, str(root))
    from live_backend.service import Settings
    settings = Settings.from_env()
    if settings.online: raise ValueError('test_environment_required')
    if not settings.enable_uploads: raise ValueError('uploads_disabled')
    database = Path(settings.database_path)
    if not database.is_absolute(): database = root / database
    database = database.resolve()
    if not database.is_file(): raise ValueError('configured_database_not_found')
    return root, settings, database


def eligible_head(db, app_id, job_id):
    db.row_factory = sqlite3.Row
    head = db.execute("SELECT id,operation,state,error,body FROM outbox WHERE app_id=? AND online=0 AND state!='done' ORDER BY id LIMIT 1", (app_id,)).fetchone()
    if not head or head['id'] != job_id or head['operation'] != 'set_valid_version' or head['state'] != 'failed' or head['error'] != 'upstream_response_shape':
        raise ValueError('expected_failed_response_shape_head_required')
    from live_backend.upstream import validate_world_body
    body = json.loads(head['body'])
    validate_world_body('set_valid_version', body)
    if body['app_id'] != app_id or body['is_online_version'] is not False: raise ValueError('world_identity_mismatch')


def locate(root, environment_file, job_id):
    _, settings, database = context(root, environment_file)
    with closing(sqlite3.connect(database.as_uri() + '?mode=ro', uri=True, timeout=10)) as db:
        db.execute('PRAGMA query_only=ON')
        eligible_head(db, settings.app_id, job_id)
    return {'database_file': str(database), 'online_version': False, 'job_id': job_id}


def retry(root, environment_file, job_id, backup_file):
    _, settings, database = context(root, environment_file)
    backup = Path(backup_file).resolve()
    # The wrapper precreates this private file with the database's ACL before
    # SQLite backup writes any records. Never overwrite a nonempty snapshot.
    if backup.parent != database.parent or backup == database or not backup.is_file() or backup.stat().st_size != 0:
        raise ValueError('fresh_same_directory_backup_required')
    with closing(sqlite3.connect(database.as_uri() + '?mode=ro', uri=True, timeout=10)) as origin:
        origin.execute('PRAGMA query_only=ON')
        eligible_head(origin, settings.app_id, job_id)
        with closing(sqlite3.connect(backup, timeout=10)) as destination:
            origin.backup(destination)
    with closing(sqlite3.connect(database, timeout=10, isolation_level=None)) as db:
        db.execute('BEGIN IMMEDIATE')
        try:
            eligible_head(db, settings.app_id, job_id)
            changed = db.execute("UPDATE outbox SET state='pending',not_before=?,lease_token=NULL,lease_until=0 WHERE id=? AND app_id=? AND online=0 AND operation='set_valid_version' AND state='failed' AND error='upstream_response_shape'", (int(time.time()), job_id, settings.app_id)).rowcount
            if changed != 1: raise ValueError('job_state_changed')
            db.commit()
        except Exception:
            db.rollback()
            raise
    return {'requeued': True, 'job_id': job_id, 'database_backup_created': True, 'scores_and_rounds_unchanged': True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--backend-directory', required=True)
    parser.add_argument('--environment-file')
    parser.add_argument('--job-id', type=int, default=1)
    parser.add_argument('--mode', choices=('locate', 'retry'), required=True)
    parser.add_argument('--backup-file')
    args = parser.parse_args()
    try:
        result = locate(args.backend_directory, args.environment_file, args.job_id) if args.mode == 'locate' else retry(args.backend_directory, args.environment_file, args.job_id, args.backup_file)
        print(json.dumps(result, ensure_ascii=True, indent=2))
        return 0
    except Exception as error:
        allowed = {'invalid_environment_file', 'test_environment_required', 'uploads_disabled', 'configured_database_not_found', 'expected_failed_response_shape_head_required', 'world_identity_mismatch', 'fresh_same_directory_backup_required', 'job_state_changed'}
        code = str(error) if isinstance(error, ValueError) and str(error) in allowed else 'recovery_precondition_failed'
        print(json.dumps({'recovery_error': code}))
        return 1


if __name__ == '__main__': raise SystemExit(main())
