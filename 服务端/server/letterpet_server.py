"""LetterPet account and save service. Requires Python 3.10+."""

import argparse
from collections import defaultdict, deque
from contextlib import contextmanager
from getpass import getpass
import hashlib
from hmac import compare_digest
from http.cookies import CookieError, SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import re
import secrets
import sqlite3
import ssl
import struct
import threading
import time
from urllib.parse import parse_qs, urlsplit
import uuid
from pathlib import Path

if __package__:
    from . import admin_pages, maintenance, maintenance_pages, rewards, shop_pages
    from .save_summary import achievements, summarize
else:
    import admin_pages, maintenance, maintenance_pages, rewards, shop_pages
    from save_summary import achievements, summarize

FILE_LIMIT = 256 * 1024
PACK_LIMIT = 12 + 2 * FILE_LIMIT
PACK_MAGIC = b"LPS1"
EMPTY_PACK = struct.pack("<4sII", PACK_MAGIC, 0, 0)
USERNAME = re.compile(r"[a-z][a-z0-9_.-]{2,31}\Z")
TOKEN = re.compile(r"[0-9a-f]{64}\Z")
SETUP_CODE = re.compile(r"[0-9a-f]{32}\Z")
ACCOUNT_PATH = re.compile(r"/admin/accounts/([0-9a-f]{32})(/achievements|/wallet)?\Z")
PASSWORD_ROUNDS = 600_000
SESSION_SECONDS = 7 * 24 * 60 * 60
ADMIN_SESSION_SECONDS = 8 * 60 * 60
LOGIN_NONCE_SECONDS = 10 * 60
ACCOUNTS_PER_PAGE = 30
ADMIN_COOKIE = "lp_admin"
LOGIN_COOKIE = "lp_admin_login"
SHOP_COOKIE = "lp_shop"
SHOP_LOGIN_COOKIE = "lp_shop_login"


def parse_https_proxy_origin(value):
    """Return the canonical HTTPS origin of an explicitly configured local proxy."""
    try:
        address = urlsplit(value)
        port = address.port
        if (address.scheme == "https" and address.hostname and
                re.fullmatch(r"[a-z0-9.-]+", address.hostname) and
                address.netloc.lower() == address.hostname + (f":{port}" if port is not None else "") and
                address.path in ("", "/") and "?" not in value and "#" not in value and
                not any(char.isspace() for char in value) and port != 0):
            suffix = "" if port in (None, 443) else f":{port}"
            return f"https://{address.hostname}{suffix}"
    except ValueError:
        pass
    raise ValueError("Expected an HTTPS origin such as https://machine.example.ts.net")


class ApiError(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


def password_hash(password, salt):
    return hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, PASSWORD_ROUNDS)


def check_password_length(password):
    if not 8 <= len(password) <= 128 or any(c in password for c in "\r\n\0"):
        raise ApiError(400, "Password must have 8–128 characters")


def page_number(query):
    try:
        value = parse_qs(query, max_num_fields=4).get("page", ["1"])[0]
    except ValueError as exc:
        raise ApiError(400, "Invalid query") from exc
    return max(1, int(value)) if re.fullmatch(r"[0-9]{1,8}", value) else 1


def validate_pack(payload):
    if not 12 <= len(payload) <= PACK_LIMIT:
        raise ApiError(400, "Invalid save length")
    magic, companion, timetable = struct.unpack_from("<4sII", payload)
    if magic != PACK_MAGIC or max(companion, timetable) > FILE_LIMIT or len(payload) != 12 + companion + timetable:
        raise ApiError(400, "Invalid save framing")
    sections = []
    for start, length, prefix in ((12, companion, "[companion]"), (12 + companion, timetable, "[timetable]")):
        if not length:
            sections.append("")
            continue
        section = payload[start:start + length]
        if not section.startswith(b"\xff\xfe"):
            raise ApiError(400, "Invalid save encoding")
        try:
            text = section[2:].decode("utf-16-le")
        except UnicodeError as exc:
            raise ApiError(400, "Invalid save encoding") from exc
        if not text.startswith(prefix + "\r\n") or "\x00" in text:
            raise ApiError(400, "Invalid save section")
        sections.append(text)
    return sections


class Store:
    def __init__(self, database):
        self.database = Path(database)
        self.database.parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as db:
            db.executescript("""
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS accounts (
                    id TEXT PRIMARY KEY, username TEXT UNIQUE NOT NULL,
                    salt BLOB NOT NULL, password BLOB NOT NULL, enabled INTEGER NOT NULL DEFAULT 1,
                    role TEXT NOT NULL DEFAULT 'user' CHECK(role IN ('user', 'admin')));
                CREATE TABLE IF NOT EXISTS sessions (
                    digest BLOB PRIMARY KEY, account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
                    expires INTEGER NOT NULL);
                CREATE INDEX IF NOT EXISTS session_account ON sessions(account_id);
                CREATE TABLE IF NOT EXISTS saves (
                    account_id TEXT PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
                    revision INTEGER NOT NULL, payload BLOB NOT NULL, updated_at INTEGER);
                CREATE TABLE IF NOT EXISTS admin_sessions (
                    digest BLOB PRIMARY KEY, account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
                    expires INTEGER NOT NULL, csrf TEXT NOT NULL);
                CREATE INDEX IF NOT EXISTS admin_session_account ON admin_sessions(account_id);
                CREATE TABLE IF NOT EXISTS account_achievements (
                    account_id TEXT PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
                    unlocked INTEGER NOT NULL DEFAULT 0, title INTEGER NOT NULL DEFAULT 0,
                    save_revision INTEGER NOT NULL DEFAULT 0);
            """)
            db.executescript(rewards.SCHEMA)
            db.executescript(maintenance.SCHEMA)
        with self.connect(write=True) as db:
            if "role" not in {row["name"] for row in db.execute("PRAGMA table_info(accounts)")}:
                db.execute("ALTER TABLE accounts ADD COLUMN role TEXT NOT NULL DEFAULT 'user' "
                           "CHECK(role IN ('user', 'admin'))")
            if "updated_at" not in {row["name"] for row in db.execute("PRAGMA table_info(saves)")}:
                db.execute("ALTER TABLE saves ADD COLUMN updated_at INTEGER")
            db.execute("INSERT OR IGNORE INTO metadata VALUES ('server_id',?)", (uuid.uuid4().hex,))
            self.server_id = db.execute("SELECT value FROM metadata WHERE key='server_id'").fetchone()[0]
            rewards.migrate_ledger(db)
            for row in db.execute("""
                SELECT a.id,s.payload,coalesce(s.revision,0) AS revision
                FROM accounts a LEFT JOIN saves s ON s.account_id=a.id
                LEFT JOIN account_achievements g ON g.account_id=a.id
                WHERE a.role='user' AND (g.account_id IS NULL OR g.save_revision<>coalesce(s.revision,0))
            """):
                unlocked, title = achievements(validate_pack(row["payload"])[0]) if row["payload"] else (0, 0)
                self.save_achievements(db, row["id"], unlocked, title, row["revision"])

    @contextmanager
    def connect(self, *, write=False):
        db = sqlite3.connect(self.database, timeout=5, uri=True)
        db.row_factory = sqlite3.Row
        try:
            db.execute("PRAGMA foreign_keys=ON")
            if write:
                db.execute("BEGIN IMMEDIATE")
            yield db
            db.commit()
        except BaseException:
            db.rollback()
            raise
        finally:
            db.close()

    def has_admin(self):
        with self.connect() as db:
            return self.admin_exists(db)

    @staticmethod
    def admin_exists(db):
        return db.execute("SELECT 1 FROM accounts WHERE role='admin' LIMIT 1").fetchone() is not None

    def create_user(self, username, password, *, sign_in=False, role="user", first_admin=False, actor=None):
        check_password_length(password)
        if not USERNAME.fullmatch(username):
            raise ApiError(400, "Use 3–32 lowercase letters, digits, dots, underscores or hyphens; start with a letter")
        salt = secrets.token_bytes(16)
        hashed = password_hash(password, salt)
        account_id = uuid.uuid4().hex
        with self.connect(write=True) as db:
            if role == "admin" and first_admin and self.admin_exists(db):
                raise ApiError(409, "Administrator already configured")
            try:
                db.execute("INSERT INTO accounts(id,username,salt,password,role) VALUES (?,?,?,?,?)",
                           (account_id, username, salt, hashed, role))
            except sqlite3.IntegrityError as exc:
                raise ApiError(409, "Account already exists") from exc
            if role == "user":
                db.execute("INSERT INTO account_achievements(account_id) VALUES (?)", (account_id,))
            if actor or first_admin:
                maintenance.audit(db, actor or username, "create-" + role, username)
            if sign_in:
                if role == "admin":
                    return self.issue_admin_session(db, account_id)
                return self.issue_session(db, account_id)

    def issue_session(self, db, account_id):
        token = secrets.token_hex(32)
        now = int(time.time())
        db.execute("DELETE FROM sessions WHERE expires<=?", (now,))
        db.execute("INSERT INTO sessions VALUES (?,?,?)", (self.token_digest(token), account_id, now + SESSION_SECONDS))
        server_id = db.execute("SELECT value FROM metadata WHERE key='server_id'").fetchone()[0]
        return f"{token}\n{account_id}\n{server_id}\n".encode("ascii")

    def issue_admin_session(self, db, account_id):
        token = secrets.token_hex(32)
        now = int(time.time())
        db.execute("DELETE FROM admin_sessions WHERE expires<=?", (now,))
        db.execute("INSERT INTO admin_sessions VALUES (?,?,?,?)",
                   (self.token_digest(token), account_id, now + ADMIN_SESSION_SECONDS, secrets.token_hex(32)))
        return token

    def login(self, username, password, *, admin=False, shop=False):
        if len(password) > 128 or not USERNAME.fullmatch(username):
            raise ApiError(401, "Invalid account or password")
        role = "admin" if admin else "user"
        with self.connect() as db:
            account = db.execute("SELECT * FROM accounts WHERE username=?", (username,)).fetchone()
        computed = password_hash(password, account["salt"] if account else b"\x00" * 16)
        if not account or not compare_digest(computed, account["password"]) or not account["enabled"]:
            raise ApiError(401, "Invalid account or password")
        with self.connect(write=True) as db:
            # A concurrent password reset or disable must win over this login.
            latest = db.execute("SELECT password,enabled FROM accounts WHERE id=? AND role=?",
                                (account["id"], role)).fetchone()
            if not latest or not latest["enabled"] or not compare_digest(computed, latest["password"]):
                raise ApiError(401, "Invalid account or password")
            if admin:
                return self.issue_admin_session(db, account["id"])
            if shop:
                token = secrets.token_hex(32)
                now = int(time.time())
                db.execute("DELETE FROM shop_sessions WHERE expires<=?", (now,))
                db.execute("INSERT INTO shop_sessions VALUES (?,?,?,?)", (
                    self.token_digest(token), account["id"], now + ADMIN_SESSION_SECONDS, secrets.token_hex(32)))
                return token
            return self.issue_session(db, account["id"])

    @staticmethod
    def token_digest(token):
        return hashlib.sha256(token.encode("ascii")).digest()

    def account(self, db, token):
        row = db.execute("""
            SELECT a.* FROM sessions s JOIN accounts a ON s.account_id=a.id
            WHERE s.digest=? AND s.expires>? AND a.enabled=1 AND a.role='user'
        """, (self.token_digest(token), int(time.time()))).fetchone()
        if row is None:
            raise ApiError(401, "Session expired")
        return row

    def logout(self, token):
        with self.connect(write=True) as db:
            digest = self.token_digest(token)
            db.execute("DELETE FROM sessions WHERE digest=?", (digest,))
            db.execute("DELETE FROM reward_meter WHERE owner=?", (digest,))

    def change_password(self, token, old, new):
        check_password_length(new)
        if len(old) > 128:
            raise ApiError(403, "Incorrect current password")
        with self.connect() as db:
            account = self.account(db, token)
        if not compare_digest(password_hash(old, account["salt"]), account["password"]):
            raise ApiError(403, "Incorrect current password")
        salt = secrets.token_bytes(16)
        hashed = password_hash(new, salt)
        with self.connect(write=True) as db:
            current = self.account(db, token)
            if not compare_digest(current["password"], account["password"]):
                raise ApiError(401, "Session expired")
            self.set_password(db, current["id"], salt, hashed)

    @staticmethod
    def set_password(db, account_id, salt, hashed):
        db.execute("UPDATE accounts SET salt=?,password=? WHERE id=?", (salt, hashed, account_id))
        Store.revoke_sessions(db, account_id)

    @staticmethod
    def revoke_sessions(db, account_id):
        db.execute("DELETE FROM sessions WHERE account_id=?", (account_id,))
        db.execute("DELETE FROM admin_sessions WHERE account_id=?", (account_id,))
        db.execute("DELETE FROM shop_sessions WHERE account_id=?", (account_id,))
        db.execute("DELETE FROM reward_meter WHERE account_id=?", (account_id,))
        db.execute("DELETE FROM reward_previews WHERE account_id=?", (account_id,))

    def require_shopper(self, db, token, csrf=None):
        shopper = db.execute("""
            SELECT a.id,a.username,s.csrf FROM shop_sessions s JOIN accounts a ON a.id=s.account_id
            WHERE s.digest=? AND s.expires>? AND a.enabled=1 AND a.role='user'
        """, (self.token_digest(token), int(time.time()))).fetchone()
        if shopper is None:
            raise ApiError(401, "请先登录商城。")
        if csrf is not None and (not TOKEN.fullmatch(csrf) or not compare_digest(csrf, shopper["csrf"])):
            raise ApiError(403, "表单已失效，请刷新页面后重试。")
        return shopper

    def shop_view(self, token):
        with self.connect() as db:
            db.execute("BEGIN")
            shopper = self.require_shopper(db, token)
            account_id = shopper["id"]
            wallet = dict(rewards.wallet(db, account_id))
            wallet["equipped"] = rewards.equipment(db, account_id, wallet["sku"])
            return shopper, wallet, rewards.inventory(db, account_id)

    def shop_records(self, token, *, page=None):
        with self.connect() as db:
            db.execute("BEGIN")
            shopper = self.require_shopper(db, token)
            account_id = shopper["id"]
            if page is not None:
                return shopper, rewards.wallet(db, account_id), rewards.ledger(db, account_id, page)
            head = db.execute("SELECT revision,updated_at,payload FROM saves WHERE account_id=?",
                              (account_id,)).fetchone()
            history = db.execute("""
                SELECT revision,updated_at,payload FROM save_versions WHERE account_id=? ORDER BY revision DESC
            """, (account_id,)).fetchall()
        return shopper, head, history

    def shop_restore_save(self, token, csrf, revision, expected):
        with self.connect(write=True) as db:
            shopper = self.require_shopper(db, token, csrf)
            account_id = shopper["id"]
            current = db.execute("SELECT * FROM saves WHERE account_id=?", (account_id,)).fetchone()
            if not current or current["revision"] != expected:
                raise ApiError(409, "云端存档已更新。请刷新历史记录后重试。")
            old = db.execute("SELECT payload FROM save_versions WHERE account_id=? AND revision=?",
                             (account_id, revision)).fetchone()
            if old is None:
                raise ApiError(404, "这个历史版本已不在保留范围内。")
            if old["payload"] != current["payload"]:
                companion, _ = validate_pack(old["payload"])
                self.write_save(db, account_id, current, old["payload"], achievements(companion))

    def shop_action(self, token, csrf, action, sku=None):
        with self.connect(write=True) as db:
            shopper = self.require_shopper(db, token, csrf)
            if action == "logout":
                db.execute("DELETE FROM shop_sessions WHERE digest=?", (self.token_digest(token),))
            elif action == "buy":
                rewards.buy(db, shopper["id"], sku)
            elif action == "equip":
                rewards.equip(db, shopper["id"], sku)
            elif action == "unequip":
                rewards.unequip(db, shopper["id"], sku)
            elif action == "try":
                rewards.preview(db, shopper["id"], sku)

    def reward_status(self, token, *, focus=None, details=False, effective=False, multi=False, themed=False):
        recording = focus is not None and effective
        with self.connect(write=recording) as db:
            if not recording:
                db.execute("BEGIN")
            account_id = self.account(db, token)["id"]
            minted = 0
            if not recording:
                state = rewards.wallet(db, account_id)
            else:
                owner = self.token_digest(token)
                state = rewards.checkpoint(db, account_id, owner, *focus)
                if details:
                    minted = rewards.round_coins(db, account_id, owner, focus[0])
            if multi:
                items = rewards.equipment(db, account_id, state["sku"])
                equipped = ",".join(sku for sku in items if themed or sku not in rewards.THEMED_SKUS)
            else:
                equipped = state["sku"]
            reply = f"{state['balance']}\n{equipped or '-'}\n"
            if details:
                next_ms = rewards.until_next_coin(state["focus_ms"])
                preview_ms = min(state["preview_ms"], rewards.PREVIEW_MS)
                trial = state["preview"]
                if (not multi and trial not in {"star", "heart", "moon"}) or (
                        not themed and trial in rewards.THEMED_SKUS):
                    trial, preview_ms = None, 0
                reply += f"{next_ms}\n{minted}\n{trial or '-'}\n{preview_ms}\n"
            if effective:
                reply += f"{int(rewards.reached_daily_limit(db, account_id))}\n"
            return reply.encode("ascii")

    def administer(self, action, username, password=None):
        if password is not None:
            check_password_length(password)
            salt = secrets.token_bytes(16)
            hashed = password_hash(password, salt)
        with self.connect(write=True) as db:
            account = db.execute("SELECT id FROM accounts WHERE username=?", (username,)).fetchone()
            if account is None:
                raise ApiError(404, "Account not found")
            if action == "set-password":
                self.set_password(db, account["id"], salt, hashed)
            else:
                db.execute("UPDATE accounts SET enabled=? WHERE id=?", (int(action == "enable"), account["id"]))
                if action == "disable":
                    self.revoke_sessions(db, account["id"])
            maintenance.audit(db, "本机命令行", action, username)

    def require_admin(self, db, token, csrf=None):
        row = db.execute("""
            SELECT a.username,s.csrf FROM admin_sessions s JOIN accounts a ON a.id=s.account_id
            WHERE s.digest=? AND s.expires>? AND a.enabled=1 AND a.role='admin'
        """, (self.token_digest(token), int(time.time()))).fetchone()
        if row is None:
            raise ApiError(401, "Administrator sign-in required")
        if csrf is not None and (not TOKEN.fullmatch(csrf) or not compare_digest(csrf, row["csrf"])):
            raise ApiError(403, "Invalid form token")
        return row

    def admin_accounts(self, token, search, page):
        with self.connect() as db:
            db.execute("BEGIN")
            admin = self.require_admin(db, token)
            counts = db.execute("""
                SELECT count(*) AS total,count(CASE WHEN enabled=1 THEN 1 END) AS enabled
                FROM accounts WHERE role='user'
            """).fetchone()
            matches = db.execute("SELECT count(*) FROM accounts WHERE role='user' AND instr(username,?)>0",
                                 (search,)).fetchone()[0]
            pages = max(1, (matches + ACCOUNTS_PER_PAGE - 1) // ACCOUNTS_PER_PAGE)
            page = min(page, pages)
            rows = db.execute("""
                SELECT a.id,a.username,a.enabled,s.revision,s.updated_at,g.unlocked
                FROM accounts a LEFT JOIN saves s ON s.account_id=a.id
                JOIN account_achievements g ON g.account_id=a.id
                WHERE a.role='user' AND instr(a.username,?)>0 ORDER BY a.username
                LIMIT ? OFFSET ?
            """, (search, ACCOUNTS_PER_PAGE, (page - 1) * ACCOUNTS_PER_PAGE)).fetchall()
            return admin, counts, rows, page, pages

    def admin_account(self, token, account_id, *, include_save=True):
        with self.connect() as db:
            db.execute("BEGIN")
            admin = self.require_admin(db, token)
            row = db.execute("""
                SELECT a.id,a.username,a.enabled,s.revision,s.updated_at,g.unlocked,g.title,
                       CASE WHEN ? THEN s.payload END AS payload
                FROM accounts a LEFT JOIN saves s ON s.account_id=a.id
                JOIN account_achievements g ON g.account_id=a.id
                WHERE a.id=? AND a.role='user'
            """, (include_save, account_id)).fetchone()
            if row is None:
                raise ApiError(404, "Account not found")
        data = summarize(*validate_pack(row["payload"])) if row["payload"] else None
        return admin, row, data

    def admin_logout(self, token, csrf):
        with self.connect(write=True) as db:
            self.require_admin(db, token, csrf)
            db.execute("DELETE FROM admin_sessions WHERE digest=?", (self.token_digest(token),))

    def admin_wallet(self, token, account_id, page):
        with self.connect() as db:
            db.execute("BEGIN")
            admin = self.require_admin(db, token)
            account = db.execute("SELECT username FROM accounts WHERE id=? AND role='user'",
                                 (account_id,)).fetchone()
            if account is None:
                raise ApiError(404, "Account not found")
            return admin, account["username"], rewards.wallet(db, account_id), rewards.ledger(db, account_id, page)

    def admin_audit(self, token, page):
        with self.connect() as db:
            db.execute("BEGIN")
            admin = self.require_admin(db, token)
            count = db.execute("SELECT count(*) FROM admin_audit").fetchone()[0]
            pages = max(1, (count + ACCOUNTS_PER_PAGE - 1) // ACCOUNTS_PER_PAGE)
            page = min(page, pages)
            rows = db.execute("""
                SELECT * FROM admin_audit ORDER BY created_at DESC, rowid DESC LIMIT ? OFFSET ?
            """, (ACCOUNTS_PER_PAGE, (page - 1) * ACCOUNTS_PER_PAGE)).fetchall()
            return admin, rows, page, pages

    def admin_backups(self, token):
        with self.connect() as db:
            admin = self.require_admin(db, token)
            return admin, maintenance.snapshots(self.database)

    def record_backup_failure(self, actor, action, target):
        if actor is not None:
            try:
                with self.connect(write=True) as db:
                    maintenance.audit(db, actor, action, target, "failed")
            except sqlite3.Error:
                pass

    def create_backup(self, token=None, csrf=None):
        actor = None
        try:
            with self.connect(write=True) as db:
                actor = self.require_admin(db, token, csrf)["username"] if token is not None else "本机命令行"
                snapshot = maintenance.backup(self.database)
                maintenance.audit(db, actor, "backup", snapshot.name)
                return snapshot
        except (OSError, ValueError, sqlite3.Error):
            self.record_backup_failure(actor, "backup", self.database.name)
            raise

    def restore_backup(self, source, token=None, csrf=None):
        actor = None
        try:
            with self.connect(write=True) as db:
                actor = self.require_admin(db, token, csrf)["username"] if token is not None else "本机命令行"
                path = maintenance.managed_path(self.database, source) if token is not None else Path(source)
                recovery, server_id = maintenance.restore(db, self.database, path)
                maintenance.audit(db, actor, "restore", path.name)
        except (OSError, ValueError, sqlite3.Error):
            self.record_backup_failure(actor, "restore", Path(source).name[:100])
            raise
        self.server_id = server_id
        return recovery

    def admin_update(self, token, csrf, account_id, action, confirmation=""):
        error = None
        with self.connect(write=True) as db:
            admin = self.require_admin(db, token, csrf)
            row = db.execute("SELECT username FROM accounts WHERE id=? AND role='user'", (account_id,)).fetchone()
            if row is None:
                error = ApiError(404, "Account not found")
            elif action == "delete":
                if confirmation != row["username"]:
                    error = ApiError(400, "Account name does not match")
                else:
                    db.execute("DELETE FROM accounts WHERE id=?", (account_id,))
            elif action in ("enable", "disable"):
                db.execute("UPDATE accounts SET enabled=? WHERE id=?", (int(action == "enable"), account_id))
                if action == "disable":
                    self.revoke_sessions(db, account_id)
            else:
                raise ApiError(400, "Invalid account action")
            if action in ("enable", "disable", "delete"):
                maintenance.audit(db, admin["username"], action, row["username"] if row else account_id,
                                  "refused" if error else "ok")
        if error:
            raise error

    def get_save(self, token):
        with self.connect() as db:
            account = self.account(db, token)
            row = db.execute("SELECT revision,payload FROM saves WHERE account_id=?", (account["id"],)).fetchone()
            return (row["revision"], row["payload"]) if row else (0, EMPTY_PACK)

    @staticmethod
    def save_achievements(db, account_id, unlocked, title, revision):
        db.execute("""
            INSERT INTO account_achievements(account_id,unlocked,title,save_revision) VALUES (?,?,?,?)
            ON CONFLICT(account_id) DO UPDATE SET unlocked=excluded.unlocked,title=excluded.title,
                save_revision=excluded.save_revision
        """, (account_id, unlocked, title, revision))

    def write_save(self, db, account_id, current, payload, achievement):
        revision = current["revision"] + 1 if current else 1
        if current:
            db.execute("INSERT INTO save_versions VALUES (?,?,?,?)",
                       (account_id, current["revision"], current["payload"], current["updated_at"]))
            db.execute("""
                DELETE FROM save_versions WHERE account_id=? AND revision NOT IN (
                    SELECT revision FROM save_versions WHERE account_id=? ORDER BY revision DESC LIMIT ?)
            """, (account_id, account_id, maintenance.HISTORY_LIMIT))
        db.execute("""
            INSERT INTO saves(account_id,revision,payload,updated_at) VALUES (?,?,?,?)
            ON CONFLICT(account_id) DO UPDATE SET revision=excluded.revision,payload=excluded.payload,
                updated_at=excluded.updated_at
        """, (account_id, revision, payload, int(time.time())))
        self.save_achievements(db, account_id, *achievement, revision)
        return revision

    def put_save(self, token, expected, payload):
        companion, _ = validate_pack(payload)
        with self.connect(write=True) as db:
            account_id = self.account(db, token)["id"]
            row = db.execute("SELECT * FROM saves WHERE account_id=?", (account_id,)).fetchone()
            revision, previous = (row["revision"], row["payload"]) if row else (0, EMPTY_PACK)
            if payload == previous:
                return revision
            if revision != expected:
                raise ApiError(409, "Save changed on another device")
            return self.write_save(db, account_id, row, payload, achievements(companion))


class AccountServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, store, ssl_context=None, *, https_proxy_origin=None):
        if https_proxy_origin is not None:
            if ssl_context is not None or not ipaddress.ip_address(address[0]).is_loopback:
                raise ValueError("The HTTPS proxy requires a loopback HTTP listener")
            https_proxy_origin = parse_https_proxy_origin(https_proxy_origin)
        self.store = store
        self.setup_code = None if store.has_admin() else secrets.token_hex(16)
        self.ssl_context = ssl_context
        self.https_proxy_origin = https_proxy_origin
        self.attempts = defaultdict(deque)
        self.attempt_lock = threading.Lock()
        super().__init__(address, Handler)

    def available_setup_code(self):
        code = self.setup_code
        return code if code and not self.store.has_admin() else None

    def get_request(self):
        sock, address = super().get_request()
        sock.settimeout(10)
        return sock, address

    def finish_request(self, sock, address):
        if self.ssl_context is None:
            return super().finish_request(sock, address)
        with self.ssl_context.wrap_socket(sock, server_side=True) as connection:
            super().finish_request(connection, address)

    def rate_limit(self, host):
        with self.attempt_lock:
            now = time.monotonic()
            for key, values in list(self.attempts.items()):
                if not values or now - values[-1] >= 60:
                    self.attempts.pop(key)
            if host not in self.attempts and len(self.attempts) >= 4096:
                raise ApiError(429, "Too many attempts")
            attempts = self.attempts[host]
            while attempts and now - attempts[0] >= 60:
                attempts.popleft()
            if len(attempts) >= 12:
                raise ApiError(429, "Too many attempts")
            attempts.append(now)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        # Keep request lines, authorization headers and credentials out of logs.
        return

    def reply(self, status, data=b"", content_type="text/plain; charset=utf-8", revision=None, headers=()):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        if revision is not None:
            self.send_header("X-LetterPet-Revision", str(revision))
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if data:
            self.wfile.write(data)

    def read_body(self, limit):
        value = self.headers.get("Content-Length", "")
        if self.headers.get("Transfer-Encoding") or not re.fullmatch(r"[0-9]{1,7}", value):
            raise ApiError(400, "Content-Length required")
        length = int(value)
        if length > limit:
            raise ApiError(413, "Request too large")
        body = self.rfile.read(length)
        if len(body) != length:
            raise ApiError(400, "Incomplete request")
        return body

    def form(self, *names):
        if self.headers.get_content_type() != "application/x-www-form-urlencoded":
            raise ApiError(415, "Expected form data")
        try:
            fields = parse_qs(self.read_body(4096).decode("ascii"), keep_blank_values=True,
                              strict_parsing=True, max_num_fields=len(names), errors="strict")
        except (ValueError, UnicodeError) as exc:
            raise ApiError(400, "Invalid form") from exc
        if set(fields) != set(names) or any(len(fields[name]) != 1 for name in names):
            raise ApiError(400, "Invalid form fields")
        return [fields[name][0] for name in names]

    def token(self):
        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Bearer ") or not TOKEN.fullmatch(auth[7:]):
            raise ApiError(401, "Sign in required")
        return auth[7:]

    def page(self, body, status=200, headers=()):
        self.reply(status, body.encode("utf-8"), "text/html; charset=utf-8", headers=(
            ("Content-Security-Policy", "default-src 'none'; style-src 'self'; img-src 'self'; form-action 'self'; "
                                        "base-uri 'none'; frame-ancestors 'none'"),
            ("Referrer-Policy", "same-origin"),
            ("X-Content-Type-Options", "nosniff"),
            *headers,
        ))

    def redirect(self, path, *headers):
        self.reply(303, headers=(("Location", path), *headers))

    def cookie_token(self, name):
        cookie = SimpleCookie()
        try:
            cookie.load(self.headers.get("Cookie", ""))
        except CookieError:
            return ""
        morsel = cookie.get(name)
        return morsel.value if morsel and TOKEN.fullmatch(morsel.value) else ""

    def session_cookie(self, name, value, lifetime, path="/admin"):
        cookie = SimpleCookie()
        cookie[name] = value
        cookie[name]["path"] = path
        cookie[name]["max-age"] = lifetime
        cookie[name]["httponly"] = True
        cookie[name]["samesite"] = "Strict"
        cookie[name]["secure"] = self.server.ssl_context is not None or self.forwarded_origin() is not None
        return "Set-Cookie", cookie[name].OutputString()

    def forwarded_origin(self):
        configured = self.server.https_proxy_origin
        if configured is None or not ipaddress.ip_address(self.client_address[0]).is_loopback:
            return None
        if self.headers.get_all("X-Forwarded-Proto") != ["https"]:
            return None
        hosts = self.headers.get_all("X-Forwarded-Host", [])
        if len(hosts) != 1 or not re.fullmatch(r"[a-zA-Z0-9.-]+(?::[0-9]+)?", hosts[0]):
            return None
        try:
            origin = parse_https_proxy_origin(f"https://{hosts[0]}")
        except ValueError:
            return None
        return configured if origin == configured else None

    def browser_origin(self):
        forwarded = self.forwarded_origin()
        if forwarded is not None:
            return forwarded
        secure = self.server.ssl_context is not None
        scheme, default_port = ("https", 443) if secure else ("http", 80)
        host = self.headers.get("Host", "")
        try:
            address = urlsplit(f"{scheme}://{host}")
            port = address.port if address.port is not None else default_port
            if not address.hostname or address.netloc != host or address.username is not None:
                raise ValueError
            if not secure:
                loopback = address.hostname == "localhost" or ipaddress.ip_address(address.hostname).is_loopback
                if not loopback or port != self.server.server_port:
                    raise ValueError
        except ValueError as exc:
            raise ApiError(403, "Open the administration page on the server") from exc
        origin = f"{scheme}://{host.lower()}"
        if port == default_port and origin.endswith(f":{default_port}"):
            origin = origin.removesuffix(f":{default_port}")
        return origin

    def auth_page(self, message="", status=200, *, setup=False, username=""):
        nonce = secrets.token_hex(32)
        cookie = self.session_cookie(LOGIN_COOKIE, nonce, LOGIN_NONCE_SECONDS)
        if setup:
            body = admin_pages.setup(nonce, message, username)
        else:
            body = admin_pages.login(nonce, message, can_setup=bool(self.server.available_setup_code()))
        self.page(body, status, (cookie,))

    def check_form_token(self, nonce, cookie=LOGIN_COOKIE):
        if not TOKEN.fullmatch(nonce) or not compare_digest(nonce, self.cookie_token(cookie)):
            raise ApiError(403, "Invalid form token")

    def finish_admin_sign_in(self, token):
        self.redirect("/admin", self.session_cookie(ADMIN_COOKIE, token, ADMIN_SESSION_SECONDS),
                      self.session_cookie(LOGIN_COOKIE, "", 0))

    def dispatch_setup(self):
        setup_code = self.server.available_setup_code()
        if not setup_code:
            self.redirect("/admin/login")
            return
        if self.command == "GET":
            self.auth_page(setup=True)
            return
        if self.command != "POST":
            raise ApiError(405, "Method not allowed")
        username, password, confirmation, code, nonce = self.form("username", "password", "confirmation", "code", "csrf")
        self.check_form_token(nonce)
        self.server.rate_limit(self.client_address[0])
        code = code.strip()
        if not SETUP_CODE.fullmatch(code) or not compare_digest(code, setup_code):
            self.auth_page("注册码无效，请核对服务端启动窗口中的最新注册码。", 403, setup=True, username=username)
            return
        if password != confirmation:
            self.auth_page("两次输入的密码不一致。", 400, setup=True, username=username)
            return
        try:
            token = self.server.store.create_user(username, password, role="admin", first_admin=True, sign_in=True)
        except ApiError as exc:
            if exc.message == "Administrator already configured":
                self.redirect("/admin/login")
                return
            if exc.status not in (400, 409):
                raise
            message = "该账号名已被使用。" if exc.status == 409 else "请检查账号和密码是否符合格式要求。"
            self.auth_page(message, exc.status, setup=True, username=username)
            return
        self.server.setup_code = None
        self.finish_admin_sign_in(token)

    def dispatch_admin(self, path, query):
        store = self.server.store
        origin = self.browser_origin()
        token = self.cookie_token(ADMIN_COOKIE)
        if self.command == "POST" and self.headers.get("Origin", origin) != origin:
            raise ApiError(403, "Invalid request origin")

        route = self.command, path
        if path == "/admin/setup":
            self.dispatch_setup()
        elif route == ("GET", "/admin/login"):
            self.auth_page()
        elif route == ("POST", "/admin/login"):
            username, password, nonce = self.form("username", "password", "csrf")
            self.check_form_token(nonce)
            self.server.rate_limit(self.client_address[0])
            try:
                token = store.login(username, password, admin=True)
            except ApiError as exc:
                if exc.status != 401:
                    raise
                self.auth_page("管理员账号或密码有误，或账号已停用。", 401)
                return
            self.finish_admin_sign_in(token)
        elif route in (("GET", "/admin"), ("GET", "/admin/")):
            fields = parse_qs(query, max_num_fields=4) if query else {}
            search = fields.get("q", [""])[0].strip().lower()[:32]
            page = fields.get("page", [""])[0]
            page = max(1, int(page)) if re.fullmatch(r"[0-9]{1,8}", page) else 1
            self.page(admin_pages.accounts(*store.admin_accounts(token, search, page), search))
        elif route == ("POST", "/admin/logout"):
            csrf, = self.form("csrf")
            store.admin_logout(token, csrf)
            self.redirect("/admin/login", self.session_cookie(ADMIN_COOKIE, "", 0))
        elif route == ("GET", "/admin/audit"):
            self.page(maintenance_pages.audit(*store.admin_audit(token, page_number(query))))
        elif route == ("GET", "/admin/backups"):
            self.page(maintenance_pages.backups(*store.admin_backups(token)))
        elif route == ("POST", "/admin/backups/create"):
            csrf, = self.form("csrf")
            try:
                store.create_backup(token, csrf)
            except (OSError, ValueError, sqlite3.Error) as exc:
                raise ApiError(503, "Backup creation failed") from exc
            self.redirect("/admin/backups")
        elif route == ("POST", "/admin/backups/restore"):
            csrf, name, confirm = self.form("csrf", "name", "confirm")
            if confirm != "restore":
                raise ApiError(400, "Confirm database restore")
            try:
                recovery = store.restore_backup(name, token, csrf)
            except (ValueError, OSError, sqlite3.Error) as exc:
                raise ApiError(400, "Backup restore failed") from exc
            self.page(maintenance_pages.restored(recovery.name),
                      headers=(self.session_cookie(ADMIN_COOKIE, "", 0),))
        elif match := ACCOUNT_PATH.fullmatch(path):
            account_id = match[1]
            if match[2] == "/wallet":
                if self.command != "GET":
                    raise ApiError(405, "Method not allowed")
                self.page(maintenance_pages.admin_wallet(account_id, *store.admin_wallet(token, account_id, page_number(query))))
            elif match[2] == "/achievements":
                if self.command != "GET":
                    raise ApiError(405, "Method not allowed")
                admin, row, _ = store.admin_account(token, account_id, include_save=False)
                self.page(admin_pages.account_achievements(admin, row))
            elif self.command == "GET":
                self.page(admin_pages.account(*store.admin_account(token, account_id)))
            elif self.command == "POST":
                action, csrf, confirmation = self.form("action", "csrf", "confirmation")
                store.admin_update(token, csrf, account_id, action, confirmation)
                self.redirect("/admin" if action == "delete" else path)
            else:
                raise ApiError(405, "Method not allowed")
        else:
            raise ApiError(404, "Unknown endpoint")

    def shop_login_page(self, message="", status=200):
        nonce = secrets.token_hex(32)
        cookie = self.session_cookie(SHOP_LOGIN_COOKIE, nonce, LOGIN_NONCE_SECONDS, "/shop")
        self.page(shop_pages.login(nonce, message), status, (cookie,))

    def dispatch_shop(self, path, query):
        store = self.server.store
        origin = self.browser_origin()
        if self.command == "POST" and self.headers.get("Origin", origin) != origin:
            raise ApiError(403, "请在商城页面内提交表单。")
        token = self.cookie_token(SHOP_COOKIE)
        route = self.command, path
        if route == ("GET", "/shop/login"):
            self.shop_login_page()
        elif route == ("POST", "/shop/login"):
            username, password, nonce = self.form("username", "password", "csrf")
            self.check_form_token(nonce, SHOP_LOGIN_COOKIE)
            self.server.rate_limit(self.client_address[0])
            try:
                token = store.login(username, password, shop=True)
            except ApiError as exc:
                if exc.status != 401:
                    raise
                self.shop_login_page("账号或密码有误，或账号已停用。", 401)
                return
            self.redirect("/shop", self.session_cookie(SHOP_COOKIE, token, ADMIN_SESSION_SECONDS, "/shop"),
                          self.session_cookie(SHOP_LOGIN_COOKIE, "", 0, "/shop"))
        elif route in (("GET", "/shop"), ("GET", "/shop/"), ("GET", "/shop/closet")):
            self.page(shop_pages.mall(*store.shop_view(token), closet=path == "/shop/closet"))
        elif route == ("POST", "/shop/logout"):
            csrf, = self.form("csrf")
            store.shop_action(token, csrf, "logout")
            self.redirect("/shop/login", self.session_cookie(SHOP_COOKIE, "", 0, "/shop"))
        elif route == ("GET", "/shop/ledger"):
            self.page(maintenance_pages.shop_ledger(*store.shop_records(token, page=page_number(query))))
        elif route == ("GET", "/shop/saves"):
            self.page(maintenance_pages.save_history(*store.shop_records(token), decode=validate_pack))
        elif route == ("POST", "/shop/saves/restore"):
            csrf, revision, expected = self.form("csrf", "revision", "expected")
            if not all(re.fullmatch(r"[0-9]{1,10}", value) for value in (revision, expected)):
                raise ApiError(400, "存档版本无效。")
            store.shop_restore_save(token, csrf, int(revision), int(expected))
            self.redirect("/shop/saves")
        elif route in (("POST", "/shop/buy"), ("POST", "/shop/equip"), ("POST", "/shop/try"),
                       ("POST", "/shop/unequip"), ("POST", "/shop/closet/unequip"),
                       ("POST", "/shop/closet/equip"), ("POST", "/shop/closet/try")):
            csrf, sku = self.form("csrf", "sku")
            store.shop_action(token, csrf, path.rsplit("/", 1)[-1], sku)
            self.redirect("/shop/closet" if path.startswith("/shop/closet/") else "/shop")
        else:
            raise ApiError(404, "找不到此商城页面。")

    def dispatch(self):
        store = self.server.store
        route = (self.command, self.path)
        if route == ("GET", "/"):
            self.page(admin_pages.home())
        elif route == ("GET", "/service.css"):
            self.reply(200, admin_pages.STYLES, "text/css; charset=utf-8")
        elif self.command == "GET" and self.path.startswith("/shop-art/"):
            sku = self.path.removeprefix("/shop-art/").removesuffix(".png")
            if sku not in rewards.CATALOG or self.path != f"/shop-art/{sku}.png":
                raise ApiError(404, "Unknown item image")
            artwork = rewards.ARTWORK.get(sku, sku)
            path = Path(__file__).resolve().parent.parent / "assets" / "rewards" / f"{artwork}.png"
            if not path.is_file():
                raise ApiError(404, "Item image unavailable")
            self.reply(200, path.read_bytes(), "image/png",
                       headers=(("X-Content-Type-Options", "nosniff"),))
        elif route == ("GET", "/health"):
            self.reply(200, b"LetterPet v1\n")
        elif route == ("POST", "/v1/login"):
            self.server.rate_limit(self.client_address[0])
            self.reply(200, store.login(*self.form("username", "password")))
        elif route == ("POST", "/v1/register"):
            self.server.rate_limit(self.client_address[0])
            self.reply(201, store.create_user(*self.form("username", "password"), sign_in=True))
        elif route == ("POST", "/v1/logout"):
            store.logout(self.token())
            self.reply(204)
        elif route == ("POST", "/v1/password"):
            self.server.rate_limit(self.client_address[0])
            store.change_password(self.token(), *self.form("old", "new"))
            self.reply(204)
        elif route in (("GET", "/v1/rewards"), ("GET", "/v2/rewards"), ("GET", "/v3/rewards"), ("GET", "/v4/rewards"), ("GET", "/v5/rewards")):
            self.reply(200, store.reward_status(self.token(), details=self.path != "/v1/rewards",
                                                effective=self.path != "/v1/rewards" and self.path != "/v2/rewards",
                                                multi=self.path in ("/v4/rewards", "/v5/rewards"),
                                                themed=self.path == "/v5/rewards"))
        elif route in (("POST", "/v1/focus"), ("POST", "/v2/focus"), ("POST", "/v3/focus"), ("POST", "/v4/focus"), ("POST", "/v5/focus")):
            run, elapsed, active = self.form("run", "elapsed", "active")
            if (not re.fullmatch(r"[0-9a-f]{32}", run) or not re.fullmatch(r"[0-9]{1,13}", elapsed)
                    or active not in ("0", "1")):
                raise ApiError(400, "Invalid focus checkpoint")
            effective = self.path in ("/v3/focus", "/v4/focus", "/v5/focus")
            if effective and int(elapsed) > rewards.ROUND_LIMIT_MS:
                raise ApiError(400, "Focus round is too long")
            self.reply(200, store.reward_status(self.token(), focus=(run, int(elapsed), active == "1"),
                                                details=self.path != "/v1/focus", effective=effective,
                                                multi=self.path in ("/v4/focus", "/v5/focus"),
                                                themed=self.path == "/v5/focus"))
        elif route == ("GET", "/v1/data"):
            revision, payload = store.get_save(self.token())
            self.reply(200, payload, "application/octet-stream", revision)
        elif route == ("PUT", "/v1/data"):
            expected = self.headers.get("X-LetterPet-Revision", "")
            if not re.fullmatch(r"[0-9]{1,10}", expected):
                raise ApiError(400, "Revision required")
            if self.headers.get_content_type() != "application/octet-stream":
                raise ApiError(415, "Expected binary save")
            revision = store.put_save(self.token(), int(expected), self.read_body(PACK_LIMIT))
            self.reply(204, revision=revision)
        else:
            raise ApiError(404, "Unknown endpoint")

    def handle_api(self):
        try:
            self.dispatch()
        except (ApiError, rewards.RewardError) as exc:
            self.reply(exc.status, exc.message.encode("utf-8"))
        except sqlite3.Error:
            self.reply(503, b"Storage unavailable")
        except (ConnectionError, TimeoutError):
            return

    def handle_request(self):
        path, _, query = self.path.partition("?")
        if path == "/shop" or path.startswith("/shop/"):
            try:
                if len(query) > 1024:
                    raise ApiError(400, "Invalid query")
                self.dispatch_shop(path, query)
            except (ApiError, rewards.RewardError) as exc:
                if exc.status == 401:
                    self.redirect("/shop/login")
                else:
                    self.page(shop_pages.error(exc.status, exc.message), exc.status)
            except sqlite3.Error:
                self.page(shop_pages.error(503, "商城暂时不可用，请稍后重试。"), 503)
            except (ConnectionError, TimeoutError):
                return
            return
        if path != "/admin" and not path.startswith("/admin/"):
            self.handle_api()
            return
        try:
            if len(query) > 1024:
                raise ApiError(400, "Invalid query")
            self.dispatch_admin(path, query)
        except ApiError as exc:
            if exc.status == 401:
                self.redirect("/admin/login")
            else:
                self.page(admin_pages.error(exc.status, exc.message, path), exc.status)
        except ValueError:
            self.page(admin_pages.error(400, "Invalid query", path), 400)
        except sqlite3.Error:
            self.page(admin_pages.error(503, "Storage unavailable", path), 503)
        except (ConnectionError, TimeoutError):
            return

    do_GET = do_POST = do_PUT = handle_request


def prompt_password():
    password = getpass("Password: ")
    if password != getpass("Repeat password: "):
        raise ApiError(400, "Passwords do not match")
    check_password_length(password)
    return password


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", type=Path, default=Path(__file__).resolve().parent / "data" / "letterpet.sqlite3")
    commands = parser.add_subparsers(dest="command", required=True)
    serve = commands.add_parser("serve")
    serve.add_argument("--host", default="127.0.0.1")
    serve.add_argument("--port", type=int, default=8765)
    serve.add_argument("--cert", type=Path)
    serve.add_argument("--key", type=Path)
    serve.add_argument("--https-proxy-origin", metavar="HTTPS_ORIGIN", type=parse_https_proxy_origin,
                       help="External HTTPS origin of a reverse proxy on this computer")
    for command in ("create-user", "create-admin", "set-password", "disable", "enable"):
        commands.add_parser(command).add_argument("username")
    commands.add_parser("list-users")
    commands.add_parser("backup")
    commands.add_parser("restore").add_argument("snapshot", type=Path)
    args = parser.parse_args()
    try:
        store = Store(args.db)
        if args.command == "serve":
            if bool(args.cert) != bool(args.key):
                parser.error("Supply both --cert and --key")
            if args.https_proxy_origin and (args.cert or not ipaddress.ip_address(args.host).is_loopback):
                parser.error("--https-proxy-origin requires a loopback HTTP listener without --cert or --key")
            context = None
            if args.cert:
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.minimum_version = ssl.TLSVersion.TLSv1_2
                context.load_cert_chain(args.cert, args.key)
            elif not ipaddress.ip_address(args.host).is_loopback:
                parser.error("Use HTTPS for non-loopback addresses")
            with AccountServer((args.host, args.port), store, context,
                               https_proxy_origin=args.https_proxy_origin) as server:
                print(f"LetterPet API on {'https' if context else 'http'}://{args.host}:{server.server_port}", flush=True)
                if server.https_proxy_origin:
                    print(f"LetterPet HTTPS proxy origin: {server.https_proxy_origin}", flush=True)
                if server.setup_code:
                    print(f"First administrator setup code: {server.setup_code}", flush=True)
                server.serve_forever()
        elif args.command == "backup":
            print(store.create_backup().resolve())
        elif args.command == "restore":
            recovery = store.restore_backup(args.snapshot)
            print(f"Database restored. Pre-restore snapshot: {recovery.resolve()}")
        elif args.command == "list-users":
            with store.connect() as db:
                for row in db.execute("SELECT username,enabled,role FROM accounts ORDER BY username"):
                    print(row["username"], "enabled" if row["enabled"] else "disabled", row["role"])
        elif args.command in ("create-user", "create-admin"):
            role = "admin" if args.command == "create-admin" else "user"
            store.create_user(args.username, prompt_password(), role=role, actor="本机命令行")
            print("Administrator created" if role == "admin" else "Account created")
        else:
            store.administer(args.command, args.username, prompt_password() if args.command == "set-password" else None)
            print("Account updated")
    except KeyboardInterrupt:
        pass
    except (ApiError, OSError, ValueError, sqlite3.Error) as exc:
        parser.exit(1, (exc.message if isinstance(exc, ApiError) else str(exc)) + "\n")


if __name__ == "__main__":
    main()
