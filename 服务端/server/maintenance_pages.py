"""Read-only account records and authenticated recovery forms."""

from datetime import datetime, timedelta, timezone
from html import escape

if __package__:
    from .admin_pages import data_table, hidden, page
    from .maintenance import HISTORY_LIMIT
    from .save_summary import summarize
else:
    from admin_pages import data_table, hidden, page
    from maintenance import HISTORY_LIMIT
    from save_summary import summarize


SHANGHAI = timezone(timedelta(hours=8))


def timestamp(value):
    return datetime.fromtimestamp(value, SHANGHAI).strftime("%Y-%m-%d %H:%M:%S") if value else "时间未知"


def pager(path, current, pages):
    if pages == 1:
        return ""
    prev = f'<a class="button small" href="{path}?page={current - 1}">上一页</a>' if current > 1 else ""
    following = f'<a class="button small" href="{path}?page={current + 1}">下一页</a>' if current < pages else ""
    return f'<nav class="pagination" aria-label="分页">{prev}<span>{current} / {pages}</span>{following}</nav>'


def ledger_table(records, path):
    rows, current, pages = records
    kinds = {"opening": "期初余额", "focus": "专注入账", "purchase": "商城兑换"}
    table = data_table(("时间（北京时间）", "类型", "说明", "变动", "变动后余额"),
                       ((timestamp(row["created_at"]), kinds.get(row["kind"], row["kind"]), row["detail"],
                         f'{row["change"]:+d}', row["balance_after"]) for row in rows),
                       empty="暂无代币流水")
    return table + pager(path, current, pages)


def shop_ledger(shopper, wallet, records):
    return page("代币流水", f"""
      <section class="panel data">
        <h1>代币流水</h1>
        <p class="lead">当前余额 {wallet["balance"]} 枚。升级前的已有余额记作期初余额。</p>
        {ledger_table(records, "/shop/ledger")}
      </section>""", shopper=shopper)


def admin_wallet(account_id, admin, username, wallet, records):
    path = f"/admin/accounts/{escape(account_id)}"
    return page("账户代币流水", f"""
      <a class="back" href="{path}">← 返回账户详情</a>
      <section class="panel data">
        <h1>{escape(username)} 的代币流水</h1>
        <p class="lead">当前余额 {wallet["balance"]} 枚。</p>
        {ledger_table(records, path + "/wallet")}
      </section>""", admin=admin)


def audit(admin, rows, current, pages):
    actions = {"enable": "恢复账户", "disable": "停用账户", "delete": "删除账户",
               "set-password": "重置密码", "create-admin": "创建管理员", "create-user": "创建用户",
               "backup": "创建快照", "restore": "恢复数据库"}
    results = {"ok": "成功", "refused": "已拒绝", "failed": "失败"}
    table = data_table(("时间（北京时间）", "操作者", "操作", "目标", "结果"),
                       ((timestamp(row["created_at"]), row["actor"], actions.get(row["action"], row["action"]),
                         row["target"], results.get(row["result"], row["result"])) for row in rows),
                       empty="暂无管理操作")
    return page("管理员操作记录", f"""
      <section class="panel data">
        <h1>管理员操作记录</h1>
        {table}{pager("/admin/audit", current, pages)}
      </section>""", admin=admin)


def backups(admin, files):
    csrf = hidden("csrf", admin["csrf"])
    rows = (
        f'<tr><th scope="row">{escape(name)}</th><td>{size / 1024:,.0f} KB</td><td>'
        f'<form action="/admin/backups/restore" method="post" class="stack">'
        f'{csrf}{hidden("name", name)}'
        f'<label class="confirm-restore"><input type="checkbox" name="confirm" value="restore" required> '
        f'确认恢复此快照</label><button class="button danger small" type="submit">恢复整库</button>'
        f'</form></td></tr>' for name, size in files
    )
    table = data_table(("快照文件（UTC）", "大小", "恢复"), rows, row_html=True, empty="暂无数据库快照")
    return page("备份与恢复", f"""
      <section class="panel data">
        <div class="section-head">
          <h1>备份与恢复</h1>
          <form action="/admin/backups/create" method="post">
            {csrf}<button class="button primary" type="submit">创建数据库快照</button>
          </form>
        </div>
        <p class="lead">恢复会把账号、存档和商城数据回滚到所选时间，并退出当前登录会话。系统会先备份当前数据库。</p>
        {table}
      </section>""", admin=admin)


def restored(recovery):
    return page("数据库已恢复", f"""
      <section class="panel hero">
        <h1>数据库已恢复</h1>
        <p class="lead">恢复前快照已保存为 {escape(recovery)}。请使用恢复后的账号和密码重新登录。</p>
        <a class="button primary" href="/admin/login">重新登录</a>
      </section>""", compact=True)


def save_history(shopper, head, history, *, decode):
    def row_for(entry, latest):
        revision = entry["revision"]
        data = summarize(*decode(entry["payload"]))
        details = f'{data["completed"]} 轮专注 · {len(data["tasks"])} 项任务 · {len(data["courses"])} 门课程'
        if latest:
            controls = '<span class="state on">当前版本</span>'
        else:
            controls = f"""
              <form action="/shop/saves/restore" method="post">
                {hidden("csrf", shopper["csrf"])}{hidden("revision", str(revision))}
                {hidden("expected", str(head["revision"]))}
                <button class="button small" type="submit">恢复为最新版本</button>
              </form>"""
        return (f'<tr><th scope="row">版本 {revision}</th><td>{timestamp(entry["updated_at"])}</td>'
                f'<td>{details}</td><td>{controls}</td></tr>')

    rows = [row_for(head, True), *(row_for(entry, False) for entry in history)] if head else []
    table = data_table(("版本", "时间（北京时间）", "内容摘要", "操作"), rows, row_html=True,
                       empty="等待桌宠首次同步")
    return page("云端存档", f"""
      <section class="panel data">
        <h1>云端存档</h1>
        <p class="lead">保留最近 {HISTORY_LIMIT} 个历史版本。恢复会创建一个新的云端版本，并保留恢复前的存档。</p>
        <p class="muted">恢复后在桌宠的「我的账号」点击「立即同步」。遇到冲突时可选「使用云端」。</p>
        {table}
      </section>""", shopper=shopper)
