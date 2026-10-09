"""User-confirmed statistics and week policy; no floating-point score path."""
from datetime import datetime, timedelta, timezone
import re

MAX_INT64 = 9223372036854775807
# Hong Kong has no DST in the supported current gameplay date range.
HONG_KONG = timezone(timedelta(hours=8), "Asia/Hong_Kong")


def identifier(value):
    if not isinstance(value, str) or not 1 <= len(value) <= 256 or any(ord(c) < 32 for c in value):
        raise ValueError("invalid_identifier")
    return value


def integer(value, *, string_only=False, positive=False):
    if isinstance(value, str) and re.fullmatch(r"0|[1-9][0-9]{0,18}", value):
        value = int(value)
    elif string_only or type(value) is not int:
        raise ValueError("invalid_integer")
    if value < (1 if positive else 0) or value > MAX_INT64:
        raise ValueError("integer_range")
    return value


def week_period(value):
    value = integer(value)
    local = datetime.fromtimestamp(value, HONG_KONG)
    sunday = (local - timedelta(days=(local.weekday() + 1) % 7)).replace(hour=23, minute=0, second=0, microsecond=0)
    if local < sunday:
        sunday -= timedelta(days=7)
    return {"version": "gw-week-" + sunday.strftime("%Y%m%dT%H%M%S"), "start": int(sunday.timestamp()), "end": int((sunday + timedelta(days=7)).timestamp())}


def reduce_results(history):
    score = wins = streak = 0
    for item in history:
        result = item["contribution"]
        score += result["score"]
        won = result["result"] == 1
        wins += int(won)
        streak = streak + 1 if won else 0
    return {"score": score, "winning_points": wins, "winning_streak_count": streak}


def receipt_users(payload, histories):
    target = (payload["end_time"], payload["room_id"], int(payload["round_id"]))
    result = []
    for user in payload["users"]:
        if user["result"] == 3:
            streak = 0
        else:
            relevant = [h for h in histories.get(user["open_id"], []) if (h["end_time"], h["room_id"], int(h["round_id"])) <= target]
            streak = reduce_results(relevant)["winning_streak_count"]
        result.append({"open_id": user["open_id"], "win_points": int(user["result"] == 1), "win_streak": streak})
    return result


def ranked_users(totals):
    ordered = sorted(totals.items(), key=lambda item: (-item[1]["score"], item[0]))
    return [{"open_id": open_id, "rank": rank, **metrics} for rank, (open_id, metrics) in enumerate(ordered, 1)]


def publication_jobs(app_id, online, version, totals, changed=None, *, complete_time=None):
    common = {"app_id": app_id, "is_online_version": online, "world_rank_version": version}
    ranked = ranked_users(totals)
    # Rankings are separate from cumulative results. Changed users are included
    # even if outside Top150; final close uploads every accumulated user.
    upload = ranked if changed is None else [u for u in ranked if u["open_id"] in changed]
    jobs = [{"operation": "upload_user_result", "body": {**common, "user_list": upload[start:start + 50]}} for start in range(0, len(upload), 50)]
    jobs.append({"operation": "upload_rank_list", "body": {**common, "rank_list": ranked[:150]}})
    if complete_time is not None:
        jobs.append({"operation": "complete_upload_user_result", "body": {**common, "complete_time": integer(complete_time)}})
    return jobs


def validate_round(body, groups, *, now):
    expected = {"app_id", "room_id", "round_id", "start_time", "end_time", "users"}
    if not isinstance(body, dict) or set(body) != expected:
        raise ValueError("invalid_round_fields")
    if len(set(groups.values())) != 3 or any(not value for value in groups.values()):
        raise ValueError("invalid_server_groups")
    app_id = identifier(body["app_id"])
    room_id = str(integer(body["room_id"], string_only=True, positive=True))
    round_id = str(integer(body["round_id"], string_only=True, positive=True))
    end = integer(body["end_time"], positive=True)
    start = integer(body["start_time"], positive=True)
    if end > now + 60 or start < 1577836800 or start > end or end - start > 86400:
        raise ValueError("invalid_round_time")
    users = body["users"]
    if not isinstance(users, list) or len(users) > 5000:
        raise ValueError("user_limit")
    normalized, seen, contributions, group_results = [], set(), {}, {}
    for user in users:
        if not isinstance(user, dict) or set(user) != {"open_id", "group_id", "score", "result"}:
            raise ValueError("invalid_user_fields")
        open_id, group = identifier(user["open_id"]), identifier(user["group_id"])
        if open_id in seen:
            raise ValueError("duplicate_user")
        seen.add(open_id)
        score, result = integer(user["score"], string_only=True), integer(user["result"])
        if group == groups["gray"]:
            if result != 3:
                raise ValueError("gray_result")
        elif group in (groups["red"], groups["blue"]):
            if result not in (1, 2) or group_results.get(group, result) != result:
                raise ValueError("group_result_conflict")
            group_results[group] = result
            contributions[open_id] = {"score": score, "result": result}
        else:
            raise ValueError("unknown_group")
        normalized.append({"open_id": open_id, "group_id": group, "score": str(score), "result": result})
    if len(group_results) == 2 and len(set(group_results.values())) != 2:
        raise ValueError("winner_conflict")
    normalized.sort(key=lambda user: user["open_id"])
    return {"app_id": app_id, "room_id": room_id, "round_id": round_id, "start_time": start, "end_time": end, "users": normalized}, contributions
