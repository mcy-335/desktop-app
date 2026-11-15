"""Smoke tests for the account service using a fresh temporary database."""

from contextlib import closing
from http.client import HTTPConnection
from http.cookies import SimpleCookie
from pathlib import Path
import struct
import sqlite3
from tempfile import TemporaryDirectory
from threading import Thread
import unittest
from unittest.mock import patch
from urllib.parse import urlencode

from server import letterpet_server as service


PASSWORD = "fixture-passphrase"


def snapshot(completed):
    text = (
        f"[companion]\r\nversion=1\r\ncompleted={completed}\r\n"
        f"[extras]\r\nversion=1\r\nunlocked={completed}\r\ntitle=0\r\n"
    )
    companion = b"\xff\xfe" + text.encode("utf-16-le")
    return struct.pack("<4sII", service.PACK_MAGIC, len(companion), 0) + companion


class AccountServiceTest(unittest.TestCase):
    def setUp(self):
        test_root = Path(__file__).resolve().parent
        directory = TemporaryDirectory(prefix=".letterpet-test-", dir=test_root)
        self.assertEqual(Path(directory.name).resolve().parent, test_root)
        self.addCleanup(directory.cleanup)
        self.store = service.Store(Path(directory.name) / "test.sqlite3")
        self.desktop, self.account_id, self.server_id = self.store.create_user(
            "demo_user", PASSWORD, sign_in=True
        ).decode("ascii").splitlines()

    def assert_status(self, status, call, *args):
        with self.assertRaises(service.ApiError) as error:
            call(*args)
        self.assertEqual(error.exception.status, status)

    def shop_login(self):
        shopper = self.store.login("demo_user", PASSWORD, shop=True)
        return shopper, self.store.shop_view(shopper)[0]["csrf"]

    def test_save_history_restore(self):
        shopper, csrf = self.shop_login()
        self.assertEqual(self.store.put_save(self.desktop, 0, snapshot(1)), 1)
        self.assertEqual(self.store.put_save(self.desktop, 1, snapshot(2)), 2)
        self.assert_status(409, self.store.put_save, self.desktop, 1, snapshot(3))
        self.assert_status(409, self.store.shop_restore_save, shopper, csrf, 1, 1)
        self.store.shop_restore_save(shopper, csrf, 1, 2)
        self.assertEqual(self.store.get_save(self.desktop), (3, snapshot(1)))
        _, head, versions = self.store.shop_records(shopper)
        self.assertEqual(head["revision"], 3)
        self.assertEqual([entry["revision"] for entry in versions], [2, 1])

    def test_focus_coins_purchase_and_preview(self):
        run = "a" * 32
        origin = 1_700_000_000_000
        with patch.object(service.rewards.time, "time_ns", return_value=origin * 1_000_000) as clock:
            self.store.reward_status(self.desktop, focus=(run, 0, True), effective=True)
            for minute_slice in range(1, 31):
                elapsed = minute_slice * 10_000
                clock.return_value = (origin + elapsed) * 1_000_000
                reply = self.store.reward_status(self.desktop, focus=(run, elapsed, True),
                                                 details=True, effective=True)
        self.assertEqual(reply.splitlines()[:4], [b"5", b"-", b"59500", b"5"])
        shopper, csrf = self.shop_login()
        self.store.shop_action(shopper, csrf, "try", "heart")
        self.assertEqual(self.store.shop_view(shopper)[1]["preview"], "heart")
        self.store.shop_action(shopper, csrf, "buy", "star")
        self.store.shop_action(shopper, csrf, "buy", "star")
        self.store.shop_action(shopper, csrf, "equip", "star")
        self.assertEqual(self.store.reward_status(self.desktop), b"0\nstar\n")
        _, wallet, (rows, _, _) = self.store.shop_records(shopper, page=1)
        self.assertEqual(wallet["balance"], 0)
        self.assertEqual(sum(row["change"] for row in rows), 0)
        self.assertEqual([row["change"] for row in rows if row["kind"] == "purchase"], [-5])

    def test_focus_partial_rounds_pause_and_remainder(self):
        origin = 1_700_000_000_000
        with patch.object(service.rewards.time, "time_ns") as clock:
            def report(wall_ms, run, elapsed, active):
                clock.return_value = (origin + wall_ms) * 1_000_000
                return self.store.reward_status(self.desktop, focus=(run * 32, elapsed, active),
                                               details=True, effective=True)

            report(0, "a", 0, True)
            report(20_000, "a", 20_000, True)
            report(30_000, "a", 30_000, False)  # End a partial round.
            report(40_000, "b", 0, True)
            report(60_000, "b", 20_000, False)  # Pause or leave the whitelist.
            report(660_000, "b", 20_000, False)
            report(670_000, "b", 20_000, True)  # Resume, excluding the paused gap.
            reply = report(680_000, "b", 30_000, False)
            self.assertEqual(reply.splitlines()[0], b"1")
            with self.store.connect() as db:
                wallet = service.rewards.wallet(db, self.account_id)
                self.assertEqual(wallet["focus_ms"], 60_000)
            # Reading for six more minutes uses the same active-time reports.
            report(690_000, "c", 0, True)
            for step in range(1, 19):
                reply = report(690_000 + step * 20_000, "c", step * 20_000, step < 18)
            self.assertEqual(reply.splitlines()[0], b"7")
            # Retrying a final report cannot mint twice.
            self.assertEqual(report(1_050_000, "c", 360_000, False).splitlines()[0], b"7")

    def test_cloud_save_keeps_affinity_remainder(self):
        payload = ("[companion]\r\nversion=1\r\naffinity=12\r\n"
                   "focus_reward_ms=234567\r\n").encode("utf-16")
        packed = struct.pack("<4sII", service.PACK_MAGIC, len(payload), 0) + payload
        self.assertEqual(self.store.put_save(self.desktop, 0, packed), 1)
        self.assertEqual(self.store.get_save(self.desktop), (1, packed))

    def test_fixed_slots_trial_and_legacy_backup(self):
        shopper, csrf = self.shop_login()
        with self.store.connect(write=True) as db:
            db.execute("INSERT INTO reward_wallets VALUES (?, 100, 0)", (self.account_id,))
        action = lambda operation, sku: self.store.shop_action(shopper, csrf, operation, sku)
        action("buy", "star")
        action("equip", "star")
        legacy = self.store.create_backup()
        with closing(sqlite3.connect(legacy)) as snapshot_db, snapshot_db:
            snapshot_db.execute("DROP TABLE reward_equipment_slots")
        with self.assertRaises(service.rewards.RewardError):
            action("equip", "lamp")
        for item in ("lamp", "bonsai", "hourglass", "tea", "bookmark"):
            action("buy", item)
            action("equip", item)
        layout = ("lamp", "bonsai", "hourglass", "star", "tea", "bookmark")
        self.assertEqual(self.store.shop_view(shopper)[1]["equipped"], layout)
        action("try", "heart")
        state = self.store.reward_status(self.desktop, details=True, effective=True, multi=True).splitlines()
        self.assertEqual(state[1].decode().split(","), list(layout))
        self.assertEqual(state[4], b"heart")
        action("unequip", "tea")
        self.assertEqual(self.store.shop_view(shopper)[1]["preview"], "heart")
        action("buy", "heart")
        action("equip", "heart")
        action("unequip", "star")
        self.assertEqual(self.store.shop_view(shopper)[1]["equipped"],
                         ("lamp", "bonsai", "hourglass", "heart", "bookmark"))
        self.assertEqual(self.store.reward_status(self.desktop).splitlines()[1], b"heart")
        action("try", "tea")
        self.assertEqual(self.store.reward_status(self.desktop, details=True).splitlines()[4:6], [b"-", b"0"])
        current = self.store.create_backup()
        self.store.restore_backup(legacy)
        shopper, csrf = self.shop_login()
        self.assertEqual(self.store.shop_view(shopper)[1]["equipped"], ("star",))
        self.store.restore_backup(current)
        shopper, csrf = self.shop_login()
        self.assertEqual(self.store.shop_view(shopper)[1]["equipped"],
                         ("lamp", "bonsai", "hourglass", "heart", "bookmark"))

    def test_theme_variants_reuse_slots_and_keep_v4_compatible(self):
        shopper, csrf = self.shop_login()
        with self.store.connect(write=True) as db:
            db.execute("INSERT INTO reward_wallets VALUES (?, 100, 0)", (self.account_id,))
        for sku in ("lamp", "starlamp", "ivorytea", "fern", "prism", "star", "bookmark"):
            self.store.shop_action(shopper, csrf, "buy", sku)
            self.store.shop_action(shopper, csrf, "equip", sku)
        self.store.shop_action(shopper, csrf, "try", "starlamp")
        latest = self.store.reward_status(self.desktop, details=True, effective=True, multi=True, themed=True)
        previous = self.store.reward_status(self.desktop, details=True, effective=True, multi=True)
        self.assertEqual(latest.splitlines()[1], b"starlamp,fern,prism,star,ivorytea,bookmark")
        self.assertEqual(latest.splitlines()[4], b"starlamp")
        self.assertEqual(previous.splitlines()[1], b"star,bookmark")
        self.assertEqual(previous.splitlines()[4:6], [b"-", b"0"])
        self.assertIn("lamp", self.store.shop_view(shopper)[2])
        self.store.shop_action(shopper, csrf, "unequip", "lamp")
        self.assertIn("starlamp", self.store.shop_view(shopper)[1]["equipped"])

    def test_backup_restore_and_audit(self):
        self.store.create_user("demo_admin", PASSWORD, role="admin")
        admin = self.store.login("demo_admin", PASSWORD, admin=True)
        with self.store.connect() as db:
            csrf = self.store.require_admin(db, admin)["csrf"]
        self.store.put_save(self.desktop, 0, snapshot(1))
        saved = self.store.create_backup(admin, csrf)
        self.store.admin_update(admin, csrf, self.account_id, "delete", "demo_user")
        self.assert_status(401, self.store.get_save, self.desktop)
        recovery = self.store.restore_backup(saved.name, admin, csrf)
        self.assertTrue(recovery.is_file())
        self.assert_status(401, self.store.admin_backups, admin)
        desktop, account_id, server_id = self.store.login("demo_user", PASSWORD).decode("ascii").splitlines()
        self.assertEqual(account_id, self.account_id)
        self.assertNotEqual(server_id, self.server_id)
        self.assertEqual(self.store.get_save(desktop), (1, snapshot(1)))
        admin = self.store.login("demo_admin", PASSWORD, admin=True)
        _, events, _, _ = self.store.admin_audit(admin, 1)
        self.assertEqual({event["action"] for event in events}, {"backup", "delete", "restore"})

    def test_browser_login_behind_https_proxy(self):
        host = "letterpet.example.ts.net"
        origin = f"https://{host}"
        self.store.create_user("demo_admin", PASSWORD, role="admin")
        forwarded = {"Host": host, "X-Forwarded-Host": host, "X-Forwarded-Proto": "https"}

        def start_server(**options):
            server = service.AccountServer(("127.0.0.1", 0), self.store, **options)
            thread = Thread(target=server.serve_forever, daemon=True)
            thread.start()
            self.addCleanup(thread.join, 5)
            self.addCleanup(server.server_close)
            self.addCleanup(server.shutdown)
            return server

        def request(server, method, path, headers, fields=None):
            connection = HTTPConnection("127.0.0.1", server.server_port, timeout=5)
            try:
                if fields is not None:
                    headers = {**headers, "Content-Type": "application/x-www-form-urlencoded"}
                connection.request(method, path, urlencode(fields) if fields is not None else None, headers)
                response = connection.getresponse()
                response.read()
                cookies = SimpleCookie()
                for name, value in response.getheaders():
                    if name.lower() == "set-cookie":
                        cookies.load(value)
                return response.status, cookies
            finally:
                connection.close()

        default = start_server()
        self.assertEqual(request(default, "GET", "/shop/login", forwarded)[0], 403)
        proxy = start_server(https_proxy_origin=origin)
        for rejected in (
            {"Host": host},
            {**forwarded, "X-Forwarded-Proto": "http"},
            {**forwarded, "X-Forwarded-Host": "another.example.ts.net"},
            {**forwarded, "X-Forwarded-Host": f"{host}, {host}"},
        ):
            self.assertEqual(request(proxy, "GET", "/shop/login", rejected)[0], 403)
        local = {"Host": f"127.0.0.1:{proxy.server_port}"}
        status, cookies = request(proxy, "GET", "/shop/login", local)
        self.assertEqual(status, 200)
        self.assertFalse(cookies[service.SHOP_LOGIN_COOKIE]["secure"])

        for route, username, nonce_name, session_name in (
            ("/shop", "demo_user", service.SHOP_LOGIN_COOKIE, service.SHOP_COOKIE),
            ("/admin", "demo_admin", service.LOGIN_COOKIE, service.ADMIN_COOKIE),
        ):
            status, cookies = request(proxy, "GET", f"{route}/login", forwarded)
            self.assertEqual(status, 200)
            nonce = cookies[nonce_name]
            self.assertTrue(nonce["secure"])
            headers = {**forwarded, "Cookie": f"{nonce_name}={nonce.value}"}
            fields = {"username": username, "password": PASSWORD, "csrf": nonce.value}
            self.assertEqual(request(proxy, "POST", f"{route}/login",
                                     {**headers, "Origin": "https://another.example.ts.net"}, fields)[0], 403)
            status, cookies = request(proxy, "POST", f"{route}/login", {**headers, "Origin": origin}, fields)
            self.assertEqual(status, 303)
            session = cookies[session_name]
            self.assertTrue(session["secure"])
            self.assertEqual(request(proxy, "GET", route, {**forwarded, "Cookie": f"{session_name}={session.value}"})[0], 200)

        with self.assertRaisesRegex(ValueError, "loopback HTTP listener"):
            service.AccountServer(("0.0.0.0", 0), self.store, https_proxy_origin=origin)


if __name__ == "__main__":
    unittest.main()
