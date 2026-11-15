"""LetterPet user sign-in, wallet and catalog."""

from html import escape

if __package__:
    from .admin_pages import hidden, page
    from .rewards import CATALOG, SLOTS, THEMES, CARD_SKUS, earned_minutes
else:
    from admin_pages import hidden, page
    from rewards import CATALOG, SLOTS, THEMES, CARD_SKUS, earned_minutes


def login(nonce, message=""):
    notice = f'<p class="notice" role="alert">{escape(message)}</p>' if message else ""
    return page("用户登录", f"""
      <section class="panel login">
        <h1>用户登录</h1>
        <p class="lead">使用 Windows 桌宠账号查看代币和兑换奖励。</p>
        {notice}
        <form action="/shop/login" method="post" class="stack">
          {hidden("csrf", nonce)}
          <label>账号<input name="username" autocomplete="username" autocapitalize="none" maxlength="32" required></label>
          <label>密码<input name="password" type="password" autocomplete="current-password" maxlength="256" required></label>
          <button class="button primary" type="submit">登录商城</button>
        </form>
        <p class="auth-footer">新账号可在 Windows 桌宠的启动窗口注册。</p>
      </section>""", compact=True)


def action(csrf, sku, path, label, primary=False):
    kind = "button primary" if primary else "button"
    return f"""<form action="/shop/{path}" method="post">
      {hidden("csrf", csrf)}{hidden("sku", sku)}
      <button class="{kind}" type="submit">{label}</button>
    </form>"""


def product(item, owned, equipped, previewing, csrf, base):
    if equipped:
        controls = action(csrf, item.sku, base + "unequip", "卸下配饰")
    elif owned and item.wearable:
        controls = action(csrf, item.sku, base + "equip", "应用到桌宠", True)
    elif not owned:
        controls = action(csrf, item.sku, "buy", f"兑换 · {item.price} 枚", True)
    else:
        controls = '<span class="state earned">已收藏</span>'
    if item.wearable and not equipped:
        controls += action(csrf, item.sku, base + "try", "重新试戴" if previewing else "在桌宠上试戴")
    detail = ("试戴中" if previewing else "使用中" if equipped else "已拥有" if owned
              else "桌宠配饰" if item.wearable else "收藏奖励")
    theme = THEMES.get(item.theme)
    position = SLOTS[item.slot] if item.slot else "收藏奖励"
    return f"""
      <article class="panel shop-item">
        <div class="shop-art {item.sku} {item.theme} {'card-art' if item.sku in CARD_SKUS else 'hanging-art'}"><img src="/shop-art/{item.sku}.png" alt="" width="144" height="132"></div>
        <div class="shop-item-title"><h2>{item.name}</h2><span class="state">{detail}</span></div>
        <p class="shop-position">{position}{f" · {theme}" if theme else ""}</p>
        <p class="muted">{item.description}</p>
        <div class="shop-item-action">{controls}</div>
      </article>"""


def mall(shopper, wallet, inventory, *, closet=False):
    equipped = set(wallet["equipped"])
    previewing = CATALOG.get(wallet["preview"])
    base = "closet/" if closet else ""
    cards = "".join(product(item, item.sku in inventory, item.sku in equipped, item == previewing, shopper["csrf"], base)
                    for item in CATALOG.values() if not closet or item.sku in inventory)
    title = "我的配饰" if closet else "专注商城"
    subtitle = "查看已兑换的配饰和收藏奖励。" if closet else "有效专注每累计满 1 分钟获得 1 枚代币，每天最多计入 180 分钟。"
    items = (f'<section class="shop-grid" aria-label="{title}商品">{cards}</section>' if cards else
             '<section class="panel"><p class="lead">你还没有兑换物品。</p><a class="button" href="/shop">去商城</a></section>')
    names_by_slot = {CATALOG[sku].slot: CATALOG[sku].name for sku in equipped}
    slots = "".join(
        f'<li><span>{label}</span><strong>{names_by_slot.get(slot, "空位")}</strong></li>'
        for slot, label in SLOTS.items())
    preview_notice = ""
    if previewing:
        preview_notice = f"""
          <section class="panel shop-preview" aria-label="当前试戴">
            <p><strong>已发送试戴 · {previewing.name}</strong><br>
            <span class="muted">同一账号的桌宠会在下次同步时于{SLOTS[previewing.slot]}显示，约 1 分钟后恢复该位置。</span></p>
            {action(shopper["csrf"], "-", base + "try", "结束试戴")}
          </section>"""
    return page(title, f"""
      <section class="panel shop-wallet">
        <div>
          <h1>{title}</h1>
          <p class="lead">{subtitle}</p>
          <p class="muted">已记账 {earned_minutes(wallet["focus_ms"])} 分钟 · 当前配饰 {len(equipped)} / {len(SLOTS)} 件</p>
        </div>
        <div class="shop-balance"><span>代币余额</span><strong>{wallet["balance"]}</strong></div>
      </section>
      <section class="panel shop-layout">
        <h2>当前摆放</h2>
        <p class="muted">不同位置可以同时摆放，同一位置可更换配饰。</p>
        <ul class="shop-slots">{slots}</ul>
      </section>
      {preview_notice}
      {items}
      <p class="muted">打开同一账号的新版桌宠即可试戴。兑换后点「应用到桌宠」可保存佩戴状态。</p>
      """, shopper=shopper)


def error(status, message):
    translations = {
        "Invalid form token": "表单已失效，请返回商城刷新页面。",
        "Too many attempts": "尝试次数过多，请稍后再试。",
        "Open the administration page on the server": "请使用服务端电脑上的本机地址，或使用 HTTPS 访问。",
    }
    detail = translations.get(message, message if not message.isascii() else "请返回商城后重试。")
    return page("商城操作未完成", f"""
      <section class="panel hero">
        <h1>操作未完成 <span class="count">{status}</span></h1>
        <p class="lead" role="alert">{escape(detail)}</p>
        <a class="button" href="/shop">返回商城</a>
      </section>""", compact=True)
