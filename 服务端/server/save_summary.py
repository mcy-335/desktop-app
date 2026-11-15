"""Read the Windows client's companion.ini and timetable.ini snapshots."""

from configparser import ConfigParser, Error


ACHIEVEMENTS = (
    ("初次专注", "累计完成 1 轮专注"),
    ("十轮同行", "累计完成 10 轮专注"),
    ("百轮坚持", "累计完成 100 轮专注"),
    ("今日达标", "今日专注达到每日目标"),
    ("默契伙伴", "亲密度达到 30"),
    ("任务完成者", "至少一个任务标记完成"),
    ("七日坚持", "最近七天每天专注至少 1 分钟"),
    ("专注一小时", "单日专注达到 60 分钟"),
)


def read_ini(text):
    ini = ConfigParser(interpolation=None, strict=False, delimiters=("=",), default_section="")
    try:
        ini.read_string(text)
    except Error:
        # Historical snapshots were accepted without parsing every INI entry.
        pass
    return ini


def number(section, key, maximum):
    value = section.get(key, "")
    if len(value) <= 20 and value.isascii() and value.isdecimal():
        return min(int(value), maximum)
    return 0


def text_from_hex(section, key, limit):
    value = section.get(key, "")
    if len(value) > limit * 4 or len(value) % 4:
        return ""
    try:
        text = bytes.fromhex(value).decode("utf-16-be", errors="replace")
    except ValueError:
        return ""
    return "".join(char for char in text if char >= " " and char != "\x7f")


def achievements(text):
    ini = read_ini(text)
    section = ini["extras"] if "extras" in ini else {}
    if section.get("version") != "1":
        return 0, 0
    unlocked = number(section, "unlocked", 2**32 - 1) & ((1 << len(ACHIEVEMENTS)) - 1)
    title = number(section, "title", 2**32 - 1)
    if 1 <= title <= len(ACHIEVEMENTS) and unlocked & (1 << (title - 1)):
        return unlocked, title
    return unlocked, 0


def records(ini, header, count_key, prefix, limit):
    section = ini[header] if header in ini else {}
    for index in range(number(section, count_key, limit)):
        name = f"{prefix}{index}"
        if name in ini:
            yield ini[name]


def summarize(companion, timetable):
    companion, timetable = read_ini(companion), read_ini(timetable)
    pet = companion["companion"] if "companion" in companion else {}
    tasks = [
        (text_from_hex(task, "title_hex", 100), bool(number(task, "done", 1)),
         number(task, "ms", 2**64 - 1), number(task, "rounds", 2**32 - 1))
        for task in records(companion, "study", "tasks", "task", 64)
    ]
    days = sorted([
        (number(day, "date", 99991231), number(day, "ms", 2**64 - 1),
         number(day, "rounds", 2**32 - 1))
        for day in records(companion, "study", "days", "day", 90)
    ], reverse=True)
    courses = [
        (text_from_hex(course, "name_hex", 64), text_from_hex(course, "room_hex", 80),
         number(course, "days", 127), number(course, "start", 1440),
         number(course, "end", 1440), bool(number(course, "enabled", 1)))
        for course in records(timetable, "timetable", "count", "course", 128)
    ]
    return {
        "affinity": number(pet, "affinity", 100),
        "completed": number(pet, "completed", 1_000_000),
        "mood": number(pet, "mood", 100),
        "energy": number(pet, "energy", 100),
        "tasks": tasks,
        "days": days,
        "courses": courses,
    }
