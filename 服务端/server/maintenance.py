"""Database snapshots, cloud save history and administrative audit records."""

from contextlib import closing
from datetime import datetime, timezone
from pathlib import Path
import os
import re
import sqlite3
import time
import uuid


HISTORY_LIMIT = 12
BACKUP_NAME = re.compile(r"[0-9]{8}-[0-9]{6}-[0-9a-f]{12}\.sqlite3\Z")
SCHEMA = """
CREATE TABLE IF NOT EXISTS save_versions (
    account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    revision INTEGER NOT NULL, payload BLOB NOT NULL, updated_at INTEGER,
    PRIMARY KEY(account_id,revision));
CREATE TABLE IF NOT EXISTS admin_audit (
    id TEXT PRIMARY KEY, created_at INTEGER NOT NULL, actor TEXT NOT NULL,
    action TEXT NOT NULL, target TEXT NOT NULL, result TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS admin_audit_time ON admin_audit(created_at DESC);
"""
PERSISTENT = (
    "accounts", "saves", "account_achievements", "save_versions", "reward_wallets",
    "reward_inventory", "reward_equipment", "reward_equipment_slots", "reward_round_totals", "reward_ledger", "reward_focus_days",
)
TRANSIENT = ("sessions", "admin_sessions", "shop_sessions", "reward_meter", "reward_previews")


def audit(db, actor, action, target, result="ok"):
    db.execute("INSERT INTO admin_audit VALUES (?,?,?,?,?,?)",
               (uuid.uuid4().hex, int(time.time()), actor, action, target, result))


def snapshot_dir(database):
    return database.with_name(database.name + ".backups")


def snapshots(database):
    folder = snapshot_dir(database)
    if not folder.is_dir():
        return []
    files = sorted((entry for entry in folder.iterdir() if BACKUP_NAME.fullmatch(entry.name)
                    and entry.is_file() and not entry.is_symlink()), reverse=True)
    return [(entry.name, entry.stat().st_size) for entry in files]


def managed_path(database, name):
    if not BACKUP_NAME.fullmatch(name):
        raise ValueError("Invalid backup name")
    path = snapshot_dir(database) / name
    if path.is_symlink() or not path.is_file():
        raise ValueError("Backup not found")
    return path


def readonly(path):
    return Path(path).resolve().as_uri() + "?mode=ro"


def backup(database):
    folder = snapshot_dir(database)
    folder.mkdir(mode=0o700, parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    target = folder / f"{stamp}-{uuid.uuid4().hex[:12]}.sqlite3"
    temp = target.with_suffix(".tmp")
    descriptor = os.open(temp, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    os.close(descriptor)
    try:
        with closing(sqlite3.connect(readonly(database), uri=True)) as source:
            with closing(sqlite3.connect(temp)) as copy:
                source.backup(copy, pages=128)
                copy.execute("PRAGMA journal_mode=DELETE")
                with copy:
                    for table in TRANSIENT:
                        copy.execute(f"DELETE FROM {table}")
                    copy.execute("INSERT OR REPLACE INTO metadata VALUES ('backup_format','1')")
                if copy.execute("PRAGMA quick_check").fetchone()[0] != "ok":
                    raise ValueError("Backup integrity check failed")
                if copy.execute("PRAGMA foreign_key_check").fetchone():
                    raise ValueError("Backup integrity check failed")
        os.replace(temp, target)
        return target
    finally:
        temp.unlink(missing_ok=True)


def restore(db, database, source):
    if Path(source).resolve() == database.resolve():
        raise ValueError("A live database cannot be restored from itself")
    # The caller holds SQLite's writer lock. Existing readers retain a consistent old snapshot.
    db.execute("ATTACH DATABASE ? AS snapshot", (readonly(source),))
    marker = db.execute("SELECT value FROM snapshot.metadata WHERE key='backup_format'").fetchone()
    if not marker or marker[0] != "1" or db.execute("PRAGMA snapshot.quick_check").fetchone()[0] != "ok":
        raise ValueError("Backup integrity check failed")
    if db.execute("PRAGMA snapshot.foreign_key_check").fetchone():
        raise ValueError("Backup integrity check failed")
    tables = []
    for table in (*PERSISTENT, "admin_audit"):
        live = [row[1] for row in db.execute(f"PRAGMA main.table_info({table})")]
        saved = [row[1] for row in db.execute(f"PRAGMA snapshot.table_info({table})")]
        if table == "reward_equipment_slots" and not saved:
            continue  # Older snapshots contain only the original plaque equipment.
        tables.append(table)
        if not live or live != saved:
            raise ValueError("Backup schema is incompatible")

    recovery = backup(database)
    db.execute("DELETE FROM main.accounts")
    for table in tables:
        if table == "admin_audit":
            continue
        db.execute(f"INSERT INTO main.{table} SELECT * FROM snapshot.{table}")
    db.execute("INSERT OR IGNORE INTO main.admin_audit SELECT * FROM snapshot.admin_audit")
    # A new identity keeps clients from uploading pre-recovery caches into the restored database.
    server_id = uuid.uuid4().hex
    db.execute("UPDATE main.metadata SET value=? WHERE key='server_id'", (server_id,))
    return recovery, server_id
