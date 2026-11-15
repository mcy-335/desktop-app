"""Account wallet, inventory and online focus accounting."""

from dataclasses import dataclass
import time


@dataclass(frozen=True)
class Item:
    sku: str
    name: str
    price: int
    slot: str | None
    description: str
    theme: str = ""

    @property
    def wearable(self):
        return self.slot is not None


THEMES = {
    "wine": "红金",
    "blue": "午夜蓝",
    "cream": "奶油白",
    "forest": "森林绿",
    "contrast": "高对比",
}

SLOTS = {
    "lamp": "左上悬灯",
    "bonsai": "右上叶饰",
    "hourglass": "左下时砂",
    "plaque": "左侧悬牌",
    "tea": "右侧信笺",
    "bookmark": "右下悬牌",
}

CATALOG = {item.sku: item for item in (
    Item("star", "星芒悬牌", 5, "plaque", "星芒图案印在金边象牙卡牌上，轻悬于人物左侧。"),
    Item("heart", "莓心悬牌", 12, "plaque", "莓粉爱心与金边卡纸，随人物轻轻摆动。"),
    Item("moon", "月影悬牌", 20, "plaque", "浅蓝月牙印在悬浮卡牌上，陪伴夜间专注。"),
    Item("lamp", "琥珀星灯", 15, "lamp", "酒红灯顶、琥珀玻璃和短流苏组成的迷你悬灯。", "wine"),
    Item("bonsai", "青松浮签", 14, "bonsai", "青松盆景图案的收藏卡，悬浮在右上方。", "wine"),
    Item("hourglass", "流砂吊坠", 10, "hourglass", "黄铜时砂轻悬于左下方，留出桌面的操作空间。", "wine"),
    Item("tea", "午后茶笺", 7, "tea", "白瓷茶盏图案的金边卡笺，悬在人物右侧。", "wine"),
    Item("bookmark", "金羽悬牌", 9, "bookmark", "酒红金羽图案卡牌，悬挂在右侧下方。", "wine"),
    Item("starlamp", "星轨灯坠", 15, "lamp", "银色星轨环绕冰蓝玻璃灯球，在左上方轻摆。", "blue"),
    Item("ivorytea", "封蜡来信", 7, "tea", "象牙信封、星形封蜡与酒红缎带组成的信笺吊饰。", "cream"),
    Item("fern", "银杏叶坠", 14, "bonsai", "两片银杏叶嵌在金色圆环中，末端垂落一颗珍珠。", "forest"),
    Item("prism", "曜石时砂", 10, "hourglass", "黑白与亮金相间的时砂吊坠。", "contrast"),
    Item("card", "专注收藏卡", 8, None, "留在你的收藏里，纪念一段认真投入的时间。"),
)}
ARTWORK = {"lamp": "hanging-lantern", "fern": "hanging-leaf", "ivorytea": "hanging-letter"}
CARD_SKUS = frozenset(("star", "heart", "moon", "bonsai", "tea", "bookmark"))
THEMED_SKUS = frozenset(sku for sku, item in CATALOG.items() if item.theme and item.theme != "wine")
MINUTE_MS = 60_000
LIVE_WINDOW_MS = 25_000
PREVIEW_MS = 60_000
ROUND_LIMIT_MS = 180 * MINUTE_MS
DAY_LIMIT_MS = 180 * MINUTE_MS
DAY_MS = 24 * 60 * MINUTE_MS
SHANGHAI_OFFSET_MS = 8 * 60 * MINUTE_MS

SCHEMA = """
CREATE TABLE IF NOT EXISTS shop_sessions (
    digest BLOB PRIMARY KEY, account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    expires INTEGER NOT NULL, csrf TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS shop_session_account ON shop_sessions(account_id);
CREATE TABLE IF NOT EXISTS reward_wallets (
    account_id TEXT PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
    balance INTEGER NOT NULL DEFAULT 0 CHECK(balance>=0),
    focus_ms INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS reward_inventory (
    account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    sku TEXT NOT NULL, PRIMARY KEY(account_id,sku));
CREATE TABLE IF NOT EXISTS reward_equipment (
    account_id TEXT PRIMARY KEY, sku TEXT NOT NULL,
    FOREIGN KEY(account_id,sku) REFERENCES reward_inventory(account_id,sku) ON DELETE CASCADE);
CREATE TABLE IF NOT EXISTS reward_equipment_slots (
    account_id TEXT NOT NULL, slot TEXT NOT NULL, sku TEXT NOT NULL,
    PRIMARY KEY(account_id,slot),
    FOREIGN KEY(account_id,sku) REFERENCES reward_inventory(account_id,sku) ON DELETE CASCADE);
CREATE TABLE IF NOT EXISTS reward_meter (
    account_id TEXT PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
    owner BLOB NOT NULL, run TEXT NOT NULL, client_ms INTEGER NOT NULL,
    seen_ms INTEGER NOT NULL, active INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS reward_round_totals (
    account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    owner BLOB NOT NULL, run TEXT NOT NULL, minted INTEGER NOT NULL,
    PRIMARY KEY(account_id,owner,run));
CREATE TABLE IF NOT EXISTS reward_previews (
    account_id TEXT PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
    sku TEXT NOT NULL, expires_ms INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS reward_ledger (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    created_at INTEGER NOT NULL, kind TEXT NOT NULL, detail TEXT NOT NULL,
    change INTEGER NOT NULL, balance_after INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS reward_ledger_account ON reward_ledger(account_id,id DESC);
CREATE TABLE IF NOT EXISTS reward_focus_days (
    account_id TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    day INTEGER NOT NULL, credited_ms INTEGER NOT NULL,
    PRIMARY KEY(account_id,day));
"""


class RewardError(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


def wallet(db, account_id):
    now = time.time_ns() // 1_000_000
    return db.execute("""
        SELECT coalesce(w.balance,0) AS balance, coalesce(w.focus_ms,0) AS focus_ms, e.sku,
               p.sku AS preview, coalesce(p.expires_ms-?,0) AS preview_ms
        FROM accounts a LEFT JOIN reward_wallets w ON a.id=w.account_id
        LEFT JOIN reward_equipment e ON a.id=e.account_id
        LEFT JOIN reward_previews p ON a.id=p.account_id AND p.expires_ms>?
        WHERE a.id=?
    """, (now, now, account_id)).fetchone()


def equipment(db, account_id, plaque):
    items = {"plaque": plaque} if plaque else {}
    for row in db.execute("SELECT slot,sku FROM reward_equipment_slots WHERE account_id=?", (account_id,)):
        item = CATALOG.get(row["sku"])
        if item and item.slot == row["slot"] and item.slot != "plaque":
            items[item.slot] = item.sku
    return tuple(items[slot] for slot in SLOTS if slot in items)


def inventory(db, account_id):
    return {row["sku"] for row in db.execute("SELECT sku FROM reward_inventory WHERE account_id=?", (account_id,))}


def earned_minutes(focus_ms):
    # Round the lifetime total to the nearest second so a fraction of a second of transport jitter carries no penalty.
    return (focus_ms + 500) // MINUTE_MS


def until_next_coin(focus_ms):
    return MINUTE_MS - (focus_ms + 500) % MINUTE_MS


def round_coins(db, account_id, owner, run):
    row = db.execute("SELECT minted FROM reward_round_totals WHERE account_id=? AND owner=? AND run=?",
                     (account_id, owner, run)).fetchone()
    return row["minted"] if row else 0


def ledger_entry(db, account_id, kind, detail, change, balance_after):
    db.execute("""
        INSERT INTO reward_ledger(account_id,created_at,kind,detail,change,balance_after) VALUES (?,?,?,?,?,?)
    """, (account_id, int(time.time()), kind, detail, change, balance_after))


def migrate_ledger(db):
    db.execute("""
        INSERT INTO reward_ledger(account_id,created_at,kind,detail,change,balance_after)
        SELECT w.account_id,?,'opening','升级前余额',w.balance,w.balance FROM reward_wallets w
        WHERE w.balance>0 AND NOT EXISTS (SELECT 1 FROM reward_ledger l WHERE l.account_id=w.account_id)
    """, (int(time.time()),))


def ledger(db, account_id, page, per_page=30):
    count = db.execute("SELECT count(*) FROM reward_ledger WHERE account_id=?", (account_id,)).fetchone()[0]
    pages = max(1, (count + per_page - 1) // per_page)
    page = min(page, pages)
    rows = db.execute("""
        SELECT created_at,kind,detail,change,balance_after FROM reward_ledger WHERE account_id=?
        ORDER BY id DESC LIMIT ? OFFSET ?
    """, (account_id, per_page, (page - 1) * per_page)).fetchall()
    return rows, page, pages


def day_at(ms):
    return (ms + SHANGHAI_OFFSET_MS) // DAY_MS


def day_used(db, account_id, day):
    row = db.execute("SELECT credited_ms FROM reward_focus_days WHERE account_id=? AND day=?",
                     (account_id, day)).fetchone()
    return row[0] if row else 0


def reached_daily_limit(db, account_id):
    return day_used(db, account_id, day_at(time.time_ns() // 1_000_000)) >= DAY_LIMIT_MS


def apply_daily_limit(db, account_id, now, credit):
    day = day_at(now)
    after_midnight = min(credit, now + SHANGHAI_OFFSET_MS - day * DAY_MS)
    confirmed = 0
    for bucket, amount in ((day - 1, credit - after_midnight), (day, after_midnight)):
        if not amount:
            continue
        allowed = min(amount, max(0, DAY_LIMIT_MS - day_used(db, account_id, bucket)))
        if allowed:
            db.execute("""
                INSERT INTO reward_focus_days VALUES (?,?,?)
                ON CONFLICT(account_id,day) DO UPDATE SET credited_ms=credited_ms+excluded.credited_ms
            """, (account_id, bucket, allowed))
            confirmed += allowed
    return confirmed


def checkpoint(db, account_id, owner, run, elapsed, active):
    now = time.time_ns() // 1_000_000
    meter = db.execute("SELECT * FROM reward_meter WHERE account_id=?", (account_id,)).fetchone()
    credit = 0
    if meter:
        same_run = meter["owner"] == owner and meter["run"] == run
        gap = now - meter["seen_ms"]
        if not same_run and meter["owner"] != owner and meter["active"] and 0 <= gap < LIVE_WINDOW_MS:
            raise RewardError(409, "Another device is recording focus")
        if same_run:
            if elapsed < meter["client_ms"]:
                raise RewardError(409, "Focus checkpoint is out of order")
            # Only credit changes in the client's active timer, bounded by time observed on the server.
            if meter["active"] and 0 < gap <= LIVE_WINDOW_MS:
                credit = min(elapsed - meter["client_ms"], gap)
    db.execute("""
        INSERT INTO reward_meter VALUES (?,?,?,?,?,?)
        ON CONFLICT(account_id) DO UPDATE SET owner=excluded.owner,run=excluded.run,
            client_ms=excluded.client_ms,seen_ms=excluded.seen_ms,active=excluded.active
    """, (account_id, owner, run, elapsed, now, int(active)))
    if credit:
        credit = apply_daily_limit(db, account_id, now, credit)
    if credit:
        previous = wallet(db, account_id)
        total = previous["focus_ms"]
        minted = earned_minutes(total + credit) - earned_minutes(total)
        db.execute("""
            INSERT INTO reward_wallets VALUES (?,?,?)
            ON CONFLICT(account_id) DO UPDATE SET balance=balance+excluded.balance,focus_ms=focus_ms+excluded.focus_ms
        """, (account_id, minted, credit))
        if minted:
            db.execute("""
                INSERT INTO reward_round_totals VALUES (?,?,?,?)
                ON CONFLICT(account_id,owner,run) DO UPDATE SET minted=minted+excluded.minted
            """, (account_id, owner, run, minted))
            ledger_entry(db, account_id, "focus", "有效专注", minted, previous["balance"] + minted)
    return wallet(db, account_id)


def buy(db, account_id, sku):
    item = CATALOG.get(sku)
    if not item:
        raise RewardError(404, "找不到这项商品。")
    if sku in inventory(db, account_id):
        return
    changed = db.execute("""
        UPDATE reward_wallets SET balance=balance-? WHERE account_id=? AND balance>=?
    """, (item.price, account_id, item.price)).rowcount
    if not changed:
        raise RewardError(409, "代币余额不足。")
    db.execute("INSERT INTO reward_inventory VALUES (?,?)", (account_id, sku))
    ledger_entry(db, account_id, "purchase", item.name, -item.price, wallet(db, account_id)["balance"])


def equip(db, account_id, sku):
    if sku == "-":
        db.execute("DELETE FROM reward_equipment WHERE account_id=?", (account_id,))
        preview(db, account_id, "-")
        return
    item = CATALOG.get(sku)
    if not item or not item.wearable or sku not in inventory(db, account_id):
        raise RewardError(403, "请先兑换这件桌宠配饰。")
    if item.slot == "plaque":
        db.execute("""
            INSERT INTO reward_equipment VALUES (?,?)
            ON CONFLICT(account_id) DO UPDATE SET sku=excluded.sku
        """, (account_id, sku))
    else:
        db.execute("""
            INSERT INTO reward_equipment_slots VALUES (?,?,?)
            ON CONFLICT(account_id,slot) DO UPDATE SET sku=excluded.sku
        """, (account_id, item.slot, sku))
    clear_slot_preview(db, account_id, item.slot)


def unequip(db, account_id, sku):
    item = CATALOG.get(sku)
    if not item or not item.slot:
        raise RewardError(400, "找不到这件桌宠配饰。")
    if item.slot == "plaque":
        db.execute("DELETE FROM reward_equipment WHERE account_id=? AND sku=?", (account_id, sku))
    else:
        db.execute("DELETE FROM reward_equipment_slots WHERE account_id=? AND slot=? AND sku=?",
                   (account_id, item.slot, sku))
    clear_slot_preview(db, account_id, item.slot)


def clear_slot_preview(db, account_id, slot):
    current = CATALOG.get(wallet(db, account_id)["preview"])
    if current and current.slot == slot:
        preview(db, account_id, "-")


def preview(db, account_id, sku):
    if sku == "-":
        db.execute("DELETE FROM reward_previews WHERE account_id=?", (account_id,))
        return
    item = CATALOG.get(sku)
    if not item or not item.wearable:
        raise RewardError(400, "只有桌宠配饰可以试戴。")
    expires = time.time_ns() // 1_000_000 + PREVIEW_MS
    db.execute("""
        INSERT INTO reward_previews VALUES (?,?,?)
        ON CONFLICT(account_id) DO UPDATE SET sku=excluded.sku,expires_ms=excluded.expires_ms
    """, (account_id, sku, expires))
