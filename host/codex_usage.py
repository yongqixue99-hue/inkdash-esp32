"""Read numeric Codex quota and token usage from local session telemetry."""

from __future__ import annotations

import json
import logging
import threading
import time
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Callable


LOGGER = logging.getLogger("inkdash.codex_usage")
DAY_COUNT = 7
TOKENS_PER_CENTI_YI = 1_000_000


@dataclass(frozen=True)
class CodexUsageSnapshot:
    """A seven-day numeric snapshot suitable for the embedded dashboard."""

    remaining_percent: int
    used_percent: int
    reset_date: str
    reset_at: int
    daily_tokens: tuple[int, ...]
    generated_at: int
    usage_scope: str = "WIN"

    def __post_init__(self) -> None:
        if len(self.daily_tokens) != DAY_COUNT:
            raise ValueError("daily_tokens must contain seven values")
        if not 0 <= self.remaining_percent <= 100:
            raise ValueError("remaining_percent must be between 0 and 100")
        if not 0 <= self.used_percent <= 100:
            raise ValueError("used_percent must be between 0 and 100")
        if self.reset_at <= 0:
            raise ValueError("reset_at must be a positive Unix timestamp")
        if any(value < 0 for value in self.daily_tokens):
            raise ValueError("daily token values must be non-negative")
        if self.usage_scope not in {"WIN", "WIN+MAC"}:
            raise ValueError("usage_scope must be WIN or WIN+MAC")

    @property
    def daily_usage_centi_yi(self) -> tuple[int, ...]:
        return tuple(
            round(value / TOKENS_PER_CENTI_YI)
            for value in self.daily_tokens
        )

    @property
    def today_tokens(self) -> int:
        return self.daily_tokens[-1]

    @property
    def week_tokens(self) -> int:
        return sum(self.daily_tokens)

    def to_payload(self) -> dict[str, object]:
        return {
            "remaining_percent": self.remaining_percent,
            "used_percent": self.used_percent,
            "reset_date": self.reset_date,
            "reset_at": self.reset_at,
            "usage_scope": self.usage_scope,
            "daily_tokens": list(self.daily_tokens),
            "daily_usage_centi_yi": list(self.daily_usage_centi_yi),
            "today_tokens": self.today_tokens,
            "week_tokens": self.week_tokens,
            "generated_at": self.generated_at,
        }


class CodexUsageReader:
    """Build and cache snapshots from ``~/.codex/sessions`` JSONL files.

    Lines are discarded before JSON decoding unless they are numeric
    ``token_count`` events. This deliberately excludes chat text from the data
    path used by the dashboard.
    """

    def __init__(
        self,
        sessions_root: Path,
        cache_seconds: float = 120.0,
        timezone_offset_hours: float = 8.0,
        clock: Callable[[], float] = time.time,
    ) -> None:
        self.sessions_root = Path(sessions_root)
        self.cache_seconds = max(0.0, float(cache_seconds))
        self.timezone = timezone(timedelta(hours=timezone_offset_hours))
        self.clock = clock
        self._lock = threading.Lock()
        self._cached: CodexUsageSnapshot | None = None
        self._cached_at = 0.0

    def snapshot(self) -> CodexUsageSnapshot:
        now = self.clock()
        with self._lock:
            if (
                self._cached is not None
                and now - self._cached_at < self.cache_seconds
            ):
                return self._cached
            snapshot = self._scan(now)
            self._cached = snapshot
            self._cached_at = now
            return snapshot

    def _scan(self, now: float) -> CodexUsageSnapshot:
        if not self.sessions_root.is_dir():
            raise FileNotFoundError(
                f"Codex sessions directory is missing: {self.sessions_root}"
            )

        today = datetime.fromtimestamp(now, self.timezone).date()
        first_day = today - timedelta(days=DAY_COUNT - 1)
        scan_first_day = first_day - timedelta(days=1)
        totals = {first_day + timedelta(days=index): 0 for index in range(DAY_COUNT)}
        selected_rate: tuple[float, dict[str, object]] | None = None
        matching_events = 0

        for path in self._session_paths(scan_first_day, today):
            previous_total: int | None = None
            try:
                with path.open("r", encoding="utf-8") as session_file:
                    for line in session_file:
                        if '"token_count"' not in line:
                            continue
                        try:
                            event = json.loads(line)
                            if event.get("type") != "event_msg":
                                continue
                            payload = event.get("payload", {})
                            if payload.get("type") != "token_count":
                                continue
                            timestamp = _parse_timestamp(event.get("timestamp"))
                            total = int(
                                payload["info"]["total_token_usage"]["total_tokens"]
                            )
                        except (
                            KeyError,
                            TypeError,
                            ValueError,
                            json.JSONDecodeError,
                        ):
                            continue

                        matching_events += 1
                        if previous_total is None or total < previous_total:
                            delta = max(0, total)
                        else:
                            delta = total - previous_total
                        previous_total = total

                        local_day = timestamp.astimezone(self.timezone).date()
                        if local_day in totals:
                            totals[local_day] += delta

                        rate_limits = payload.get("rate_limits")
                        if not isinstance(rate_limits, dict):
                            continue
                        primary = rate_limits.get("primary")
                        if not isinstance(primary, dict):
                            continue
                        if rate_limits.get("limit_id") != "codex":
                            continue
                        try:
                            used_percent = float(primary["used_percent"])
                            resets_at = int(primary["resets_at"])
                            window_minutes = int(primary.get("window_minutes", 0))
                        except (KeyError, TypeError, ValueError):
                            continue
                        if window_minutes != 7 * 24 * 60:
                            continue
                        candidate = {
                            "used_percent": used_percent,
                            "resets_at": resets_at,
                        }
                        event_time = timestamp.timestamp()
                        if selected_rate is None or event_time > selected_rate[0]:
                            selected_rate = (event_time, candidate)
            except OSError as error:
                LOGGER.debug("unable to read Codex session %s: %s", path, error)

        if matching_events == 0:
            raise RuntimeError("no Codex token telemetry was found")
        if selected_rate is None:
            raise RuntimeError("no Codex quota telemetry was found")

        rate = selected_rate[1]
        used = max(0, min(100, round(float(rate["used_percent"]))))
        reset_at = int(rate["resets_at"])
        reset = datetime.fromtimestamp(reset_at, self.timezone).date()
        daily_tokens = tuple(totals[day] for day in sorted(totals))
        return CodexUsageSnapshot(
            remaining_percent=100 - used,
            used_percent=used,
            reset_date=reset.isoformat(),
            reset_at=reset_at,
            daily_tokens=daily_tokens,
            generated_at=round(now),
        )

    def _session_paths(self, first_day, last_day):
        cutoff = datetime.combine(
            first_day,
            datetime.min.time(),
            tzinfo=self.timezone,
        ).timestamp()
        paths = []
        for path in self.sessions_root.rglob("*.jsonl"):
            try:
                if path.stat().st_mtime >= cutoff:
                    paths.append(path)
            except OSError:
                continue
        yield from sorted(paths)


def _parse_timestamp(value: object) -> datetime:
    if not isinstance(value, str) or not value:
        raise ValueError("missing event timestamp")
    normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
    parsed = datetime.fromisoformat(normalized)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed
