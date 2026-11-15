"""Server-rendered service status and administrator pages."""

from datetime import datetime, timezone
from html import escape
from pathlib import Path
from urllib.parse import urlencode

if __package__:
    from .save_summary import ACHIEVEMENTS
else:
    from save_summary import ACHIEVEMENTS


STYLES = Path(__file__).with_name("service.css").read_bytes()


def page(title, body, admin=None, compact=False, *, shopper=None):
    if admin:
        nav = f"""
          <nav aria-label="管理员导航">
            <a href="/admin">账户管理</a>
            <a href="/admin/audit">操作记录</a>
            <a href="/admin/backups">备份与恢复</a>
            <form action="/admin/logout" method="post">
              {hidden("csrf", admin["csrf"])}
              <button class="button small" type="submit">退出 <span>{escape(admin["username"])}</span></button>
            </form>
          </nav>"""
    elif shopper:
        nav = f"""
          <nav aria-label="商城导航">
            <a href="/shop">商城</a>
            <a href="/shop/closet">我的配饰</a>
            <a href="/shop/ledger">代币流水</a>
            <a href="/shop/saves">云端存档</a>
            <form action="/shop/logout" method="post">
              {hidden("csrf", shopper["csrf"])}
              <button class="button small" type="submit">退出 <span>{escape(shopper["username"])}</span></button>
            </form>
          </nav>"""
    else:
        nav = '<nav aria-label="服务导航"><a href="/shop">用户商城</a><a href="/admin">管理后台</a></nav>'
    layout = "wrap compact" if compact else "wrap"
    return f"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{escape(title)} · LetterPet</title>
  <link rel="stylesheet" href="/service.css">
</head>
<body>
  <header class="topbar">
    <div class="wrap bar">
      <a class="brand" href="/"><span class="mark" aria-hidden="true">L</span>LetterPet</a>
      {nav}
    </div>
  </header>
  <main class="{layout}">{body}</main>
</body>
</html>"""


def hidden(name, value):
    return f'<input type="hidden" name="{escape(name)}" value="{escape(value)}">'


def home():
    return page("服务状态", """
      <section class="panel hero">
        <span class="state on">服务在线</span>
        <h1>LetterPet 账号服务</h1>
        <p class="lead">为 Windows 桌宠同步专注记录、课表和成长进度。</p>
        <div class="connection">
          <h2>连接桌宠</h2>
          <p>打开 Windows 桌宠，在启动账号窗口将本页地址填入「服务地址」，然后注册或登录。</p>
        </div>
        <a class="button primary" href="/shop">用户登录与商城</a>
        <a class="button" href="/admin">管理后台</a>
      </section>""", compact=True)


def login(nonce, message="", *, can_setup=False):
    notice = f'<p class="notice" role="alert">{escape(message)}</p>' if message else ""
    setup_link = '<p class="auth-footer">首次使用？ <a href="/admin/setup">注册管理员</a></p>' if can_setup else ""
    return page("管理员登录", f"""
      <section class="panel login">
        <h1>管理员登录</h1>
        <p class="lead">使用在服务端创建的管理员账号。</p>
        {notice}
        <form action="/admin/login" method="post" class="stack">
          {hidden("csrf", nonce)}
          <label>账号<input name="username" autocomplete="username" autocapitalize="none" maxlength="32" required></label>
          <label>密码<input name="password" type="password" autocomplete="current-password" maxlength="256" required></label>
          <button class="button primary" type="submit">登录</button>
        </form>
        {setup_link}
      </section>""", compact=True)


def setup(nonce, message="", username=""):
    notice = f'<p class="notice" role="alert">{escape(message)}</p>' if message else ""
    return page("注册管理员", f"""
      <section class="panel login">
        <h1>注册管理员</h1>
        <p class="lead">从服务端启动窗口复制注册码，创建第一个管理员账号。</p>
        {notice}
        <form action="/admin/setup" method="post" class="stack">
          {hidden("csrf", nonce)}
          <label>账号
            <input name="username" value="{escape(username[:32])}" autocomplete="username" autocapitalize="none"
                   spellcheck="false" minlength="3" maxlength="32" aria-describedby="username-hint" required>
            <small id="username-hint">3–32 位，以小写字母开头，可用小写字母、数字、点、下划线和连字符。</small>
          </label>
          <label>密码
            <input name="password" type="password" autocomplete="new-password" minlength="8" maxlength="256"
                   aria-describedby="password-hint" required>
            <small id="password-hint">8–128 个字符。</small>
          </label>
          <label>确认密码
            <input name="confirmation" type="password" autocomplete="new-password" maxlength="256" required>
          </label>
          <label>一次性注册码
            <input name="code" autocomplete="off" autocapitalize="none" spellcheck="false" maxlength="32" required>
          </label>
          <button class="button primary" type="submit">注册并登录</button>
        </form>
        <p class="auth-footer"><a href="/admin/login">返回登录</a></p>
      </section>""", compact=True)


def state(enabled):
    return '<span class="state on">启用</span>' if enabled else '<span class="state off">停用</span>'


def sync_time(row):
    if row["updated_at"] is not None:
        return datetime.fromtimestamp(row["updated_at"], timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    return "历史存档" if row["revision"] is not None else "从未同步"


def stats(items):
    return '<dl class="stats">' + "".join(
        f"<div><dt>{escape(label)}</dt><dd>{escape(str(value))}</dd></div>" for label, value in items
    ) + "</dl>"


def data_table(headings, rows, *, row_html=False, empty="暂无记录"):
    headers = "".join(f'<th scope="col">{escape(name)}</th>' for name in headings)
    if row_html:
        body = "".join(rows)
    else:
        body = "".join("<tr>" + "".join(f"<td>{escape(str(value))}</td>" for value in row) + "</tr>" for row in rows)
    if not body:
        body = f'<tr><td class="empty" colspan="{len(headings)}">{escape(empty)}</td></tr>'
    return f'<div class="table-wrap"><table><thead><tr>{headers}</tr></thead><tbody>{body}</tbody></table></div>'


def accounts(admin, counts, rows, current, pages, search):
    account_rows = (
        f'<tr><th scope="row"><a href="/admin/accounts/{escape(row["id"])}">{escape(row["username"])}</a></th>'
        f'<td>{state(row["enabled"])}</td><td>{row["unlocked"].bit_count()} / {len(ACHIEVEMENTS)}</td>'
        f'<td>{sync_time(row)}</td><td><a class="button small row-action" '
        f'href="/admin/accounts/{escape(row["id"])}/achievements" '
        f'aria-label="查看成就：{escape(row["username"])}">查看成就</a></td></tr>' for row in rows
    )
    table = data_table(("账户", "状态", "已解锁成就", "存档更新时间", "操作"), account_rows, row_html=True,
                       empty="没有符合条件的账户" if search else "暂无桌宠账户")
    summary = stats((("桌宠账户", counts["total"]), ("启用", counts["enabled"]),
                     ("停用", counts["total"] - counts["enabled"])))
    pagination = ""
    if pages > 1:
        previous = page_link(current - 1, search, "上一页") if current > 1 else ""
        following = page_link(current + 1, search, "下一页") if current < pages else ""
        pagination = f'<nav class="pagination" aria-label="账户分页">{previous}<span>{current} / {pages}</span>{following}</nav>'
    return page("账户管理", f"""
      <div class="page-head">
        <h1>账户管理</h1>
        <p class="lead">查看账户存档和成就，管理账户状态。</p>
      </div>
      {summary}
      <section class="panel data">
        <div class="section-head">
          <h2>桌宠账户</h2>
          <form class="search" action="/admin" method="get" role="search">
            <input name="q" aria-label="按账号搜索" placeholder="按账号搜索" value="{escape(search)}" maxlength="32">
            <button class="button" type="submit">搜索</button>
            {('<a href="/admin">清除</a>' if search else '')}
          </form>
        </div>
        {table}
        {pagination}
      </section>""", admin)


def page_link(index, search, label):
    query = urlencode({"page": index, "q": search})
    return f'<a class="button small" href="/admin?{escape(query)}">{label}</a>'


def achievement_panel(row):
    unlocked, title = row["unlocked"], row["title"]
    pending = row["revision"] is None
    items = []
    for index, (name, requirement) in enumerate(ACHIEVEMENTS):
        earned = bool(unlocked & (1 << index))
        label = "待同步" if pending else ("已解锁" if earned else "未解锁")
        kind = "earned" if earned else "locked"
        items.append(f'<li><div><strong>{name}</strong><span class="state {kind}">{label}</span></div>'
                     f'<p>{requirement}</p></li>')
    equipped = ACHIEVEMENTS[title - 1][0] if 1 <= title <= len(ACHIEVEMENTS) else "新朋友"
    count = f'<span class="count">{unlocked.bit_count()} / {len(ACHIEVEMENTS)}</span>' if not pending else ""
    summary = ('等待客户端首次同步' if pending else f'当前称号 <strong>{equipped}</strong>')
    return f"""
      <section class="panel">
        <div class="section-head">
          <h2>成长成就 {count}</h2>
          <span class="muted">{summary}</span>
        </div>
        <ul class="achievements">{''.join(items)}</ul>
      </section>"""


def account_achievements(admin, row):
    target = f'/admin/accounts/{escape(row["id"])}'
    return page(f'{row["username"]} · 成就', f"""
      <div class="page-head">
        <a class="back" href="/admin">← 返回账户列表</a>
        <div class="title-line">
          <h1>{escape(row["username"])} 的成就</h1>{state(row["enabled"])}
          <a class="button small" href="{target}">账户详情</a>
        </div>
        <p class="lead">云端存档 · {sync_time(row)}</p>
      </div>
      {achievement_panel(row)}""", admin)


def clock(minutes):
    return f"{minutes // 60:02}:{minutes % 60:02}"


def course_days(mask):
    return "、".join(day for index, day in enumerate(("一", "二", "三", "四", "五", "六", "日")) if mask & (1 << index))


def save_panels(data):
    if data is None:
        return '<section class="panel"><h2>账户存档</h2><p class="muted">等待客户端首次同步。</p></section>'
    xp = data["completed"] * 20 + data["affinity"] * 2
    progress = stats((("等级", f"Lv. {xp // 100 + 1}"), ("亲密度", data["affinity"]),
                      ("完成专注", f'{data["completed"]} 轮'), ("心情 / 精力", f'{data["mood"]} / {data["energy"]}')))
    tasks = data_table(("任务", "状态", "专注分钟", "轮数"),
                       ((title, "已完成" if done else "待办", f"{ms / 60000:.1f}", rounds)
                        for title, done, ms, rounds in data["tasks"]))
    focus = data_table(("日期", "专注分钟", "轮数"),
                       ((f"{date // 10000:04}-{date // 100 % 100:02}-{date % 100:02}", f"{ms / 60000:.1f}", rounds)
                        for date, ms, rounds in data["days"] if date))
    courses = data_table(("课程", "地点", "星期", "时间", "提醒"),
                         ((name, room or "—", course_days(days), f"{clock(start)}–{clock(end)}",
                           "开启" if enabled else "关闭") for name, room, days, start, end, enabled in data["courses"]))
    return f"""
      <section class="panel">
        <h2>桌宠进度</h2>
        {progress}
      </section>
      <section class="panel data"><h2>学习任务</h2>{tasks}</section>
      <section class="panel data"><h2>每日专注记录</h2>{focus}</section>
      <section class="panel data"><h2>课程表</h2>{courses}</section>"""


def account(admin, row, data):
    username = escape(row["username"])
    target = f'/admin/accounts/{escape(row["id"])}'
    csrf = hidden("csrf", admin["csrf"])
    action, verb = ("disable", "停用账户") if row["enabled"] else ("enable", "重新启用")
    sync = sync_time(row)
    if row["revision"] is not None:
        sync += f' · 版本 {row["revision"]}'
    return page(f'{row["username"]} · 账户详情', f"""
      <div class="page-head">
        <a class="back" href="/admin">← 返回账户列表</a>
        <div class="title-line">
          <h1>{username}</h1>{state(row["enabled"])}
          <a class="button small" href="{target}/achievements">查看成就</a>
          <a class="button small" href="{target}/wallet">代币流水</a>
          <a class="button small" href="#account-actions">管理账户</a>
        </div>
        <p class="lead">云端存档 · {sync}</p>
      </div>
      {achievement_panel(row)}
      {save_panels(data)}
      <section class="panel" id="account-actions">
        <h2>管理账户</h2>
        <div class="management">
          <div>
            <h3>账户状态</h3>
            <p class="muted">停用会撤销当前登录并阻止后续登录，云端数据会保留。</p>
            <form method="post" action="{target}">
              {csrf}{hidden("confirmation", "")}{hidden("action", action)}
              <button class="button" type="submit">{verb}</button>
            </form>
          </div>
          <div>
            <h3>删除账户</h3>
            <p class="muted">删除会移除该账户的云端存档和成就，无法从后台撤销。</p>
            <form class="stack" method="post" action="{target}">
              {csrf}{hidden("action", "delete")}
              <label>输入账号 <strong>{username}</strong> 确认
                <input name="confirmation" maxlength="32" autocomplete="off" required>
              </label>
              <button class="button danger" type="submit">永久删除账户</button>
            </form>
          </div>
        </div>
      </section>""", admin)


def error(status, message, path):
    messages = {
        "Account name does not match": "输入的账号与当前账户不一致。",
        "Account not found": "账户不存在或已删除。",
        "Invalid form token": "表单已失效，请返回页面刷新后重试。",
        "Invalid request origin": "请在管理后台页面内提交表单。",
        "Open the administration page on the server": "请在运行服务的电脑上打开管理后台。",
        "Storage unavailable": "数据库暂时不可用，请稍后重试。",
        "Too many attempts": "尝试次数过多，请稍后重试。",
        "Backup creation failed": "快照创建失败，请检查数据目录和磁盘空间。",
        "Backup restore failed": "无法恢复所选快照。请确认文件存在且通过校验。",
        "Confirm database restore": "请先确认要恢复所选快照。",
    }
    detail = messages.get(message, "请返回管理后台后重试。")
    back = path if path.startswith("/admin/accounts/") and status == 400 else "/admin"
    return page("操作未完成", f"""
      <section class="panel hero">
        <h1>操作未完成 <span class="count">{status}</span></h1>
        <p class="lead">{escape(detail)}</p>
        <a class="button" href="{escape(back)}">返回后台</a>
      </section>""", compact=True)
