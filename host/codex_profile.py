"""Read account-wide numeric Codex statistics with Windows-held credentials."""

from __future__ import annotations

import json
import threading
import time
from dataclasses import dataclass
from datetime import date, timedelta
from pathlib import Path
from typing import Callable, Mapping
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


DAY_COUNT = 7
DEFAULT_ENDPOINT = "https://chatgpt.com/backend-api/wham/profiles/me"
MAX_RESPONSE_BYTES = 256 * 1024
MAX_TOKEN_VALUE = (1 << 63) - 1


class CodexProfileError(RuntimeError):
    """A profile failure whose message never contains authentication data."""


@dataclass(frozen=True)
class CodexProfileSnapshot:
    days: tuple[str, ...]
    daily_tokens: tuple[int, ...]
    cumulative_tokens: int
    peak_tokens: int
    fetched_at: int


class CodexProfileClient:
    """Own the authenticated account-statistics seam for the dashboard.

    The access token and account id are reread from Codex's Windows auth file
    for each remote refresh. They are used only in HTTPS request headers and
    never included in the returned payload, exception text, or logs.
    """

    def __init__(
        self,
        auth_path: Path,
        *,
        endpoint: str = DEFAULT_ENDPOINT,
        cache_seconds: float = 300.0,
        timeout_seconds: float = 10.0,
        clock: Callable[[], float] = time.time,
        fetcher: Callable[[str, str, str, float], Mapping[str, object]] | None = None,
    ) -> None:
        if not str(endpoint).startswith("https://"):
            raise ValueError("Codex profile endpoint must use HTTPS")
        self._auth_path = Path(auth_path)
        self._endpoint = str(endpoint)
        self._cache_seconds = max(0.0, float(cache_seconds))
        self._timeout_seconds = max(1.0, float(timeout_seconds))
        self._clock = clock
        self._fetcher = fetcher or _fetch_json
        self._lock = threading.Lock()
        self._cached: CodexProfileSnapshot | None = None
        self._cached_at = 0.0
        self._last_error = ""

    def snapshot(self) -> CodexProfileSnapshot:
        now = self._clock()
        with self._lock:
            if (
                self._cached is not None
                and now - self._cached_at < self._cache_seconds
            ):
                return self._cached
            try:
                access_token, account_id = _load_auth(self._auth_path)
                response = self._fetcher(
                    self._endpoint,
                    access_token,
                    account_id,
                    self._timeout_seconds,
                )
                snapshot = _parse_snapshot(response, round(now))
            except CodexProfileError as error:
                self._last_error = str(error)
                raise
            except Exception as error:
                self._last_error = "unexpected profile response failure"
                raise CodexProfileError(self._last_error) from error
            self._cached = snapshot
            self._cached_at = now
            self._last_error = ""
            return snapshot

    def overlay(self, local_payload: Mapping[str, object]) -> dict[str, object]:
        """Replace local raw Token counters with the account-wide truth."""

        snapshot = self.snapshot()
        payload = dict(local_payload)
        daily_tokens = list(snapshot.daily_tokens)
        payload.update(
            {
                "usage_scope": "ACCOUNT",
                "daily_token_dates": list(snapshot.days),
                "daily_tokens": daily_tokens,
                "daily_usage_centi_yi": [
                    (value + 500_000) // 1_000_000 for value in daily_tokens
                ],
                "today_tokens": daily_tokens[-1],
                "week_tokens": sum(daily_tokens),
                "cumulative_tokens": snapshot.cumulative_tokens,
                "peak_tokens": snapshot.peak_tokens,
                "token_generated_at": snapshot.fetched_at,
            }
        )
        return payload

    def status(self) -> dict[str, object]:
        with self._lock:
            cached = self._cached
            last_error = self._last_error
        if cached is None:
            return {
                "enabled": True,
                "state": "error" if last_error else "waiting",
            }
        age = max(0, round(self._clock()) - cached.fetched_at)
        return {
            "enabled": True,
            "state": "fresh" if age <= self._cache_seconds else "stale",
            "age_seconds": age,
            "last_fetched_at": cached.fetched_at,
        }


def _load_auth(path: Path) -> tuple[str, str]:
    try:
        body = json.loads(Path(path).read_text(encoding="utf-8"))
        tokens = body["tokens"]
        access_token = tokens["access_token"]
        account_id = tokens["account_id"]
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise CodexProfileError("Codex Windows authentication is unavailable") from error
    if not isinstance(access_token, str) or not access_token.strip():
        raise CodexProfileError("Codex Windows authentication is unavailable")
    if not isinstance(account_id, str) or not account_id.strip():
        raise CodexProfileError("Codex account id is unavailable")
    return access_token.strip(), account_id.strip()


def _fetch_json(
    endpoint: str,
    access_token: str,
    account_id: str,
    timeout_seconds: float,
) -> Mapping[str, object]:
    request = Request(
        endpoint,
        headers={
            "Accept": "application/json",
            "Authorization": f"Bearer {access_token}",
            "ChatGPT-Account-Id": account_id,
            "User-Agent": "InkDash-Windows/1",
        },
        method="GET",
    )
    try:
        with urlopen(request, timeout=timeout_seconds) as response:
            body = response.read(MAX_RESPONSE_BYTES + 1)
    except HTTPError as error:
        raise CodexProfileError(
            f"Codex profile request returned HTTP {error.code}"
        ) from None
    except (URLError, TimeoutError, OSError) as error:
        raise CodexProfileError("Codex profile request could not connect") from error
    if len(body) > MAX_RESPONSE_BYTES:
        raise CodexProfileError("Codex profile response is too large")
    try:
        parsed = json.loads(body)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CodexProfileError("Codex profile response is invalid JSON") from error
    if not isinstance(parsed, dict):
        raise CodexProfileError("Codex profile response root is invalid")
    return parsed


def _parse_snapshot(
    response: Mapping[str, object], fetched_at: int
) -> CodexProfileSnapshot:
    metadata = response.get("metadata")
    if isinstance(metadata, Mapping) and str(metadata.get("stats_error") or "").strip():
        raise CodexProfileError("Codex profile reports unavailable statistics")
    stats = response.get("stats")
    if not isinstance(stats, Mapping):
        raise CodexProfileError("Codex profile stats are missing")

    cumulative = _plain_token(stats.get("lifetime_tokens"), "lifetime_tokens")
    peak = _plain_token(stats.get("peak_daily_tokens"), "peak_daily_tokens")
    buckets = stats.get("daily_usage_buckets")
    if not isinstance(buckets, list) or not buckets:
        raise CodexProfileError("daily_usage_buckets are missing")

    by_day: dict[date, int] = {}
    for bucket in buckets:
        if not isinstance(bucket, Mapping):
            raise CodexProfileError("daily_usage_buckets contain an invalid item")
        try:
            day = date.fromisoformat(str(bucket.get("start_date")))
        except ValueError as error:
            raise CodexProfileError("daily_usage_buckets contain an invalid date") from error
        if day in by_day:
            raise CodexProfileError("daily_usage_buckets contain a duplicate date")
        by_day[day] = _plain_token(bucket.get("tokens"), "daily bucket tokens")

    end_day = max(by_day)
    days = tuple(end_day - timedelta(days=DAY_COUNT - 1 - index) for index in range(DAY_COUNT))
    daily_tokens = tuple(by_day.get(day, 0) for day in days)
    recent_total = sum(daily_tokens)
    if cumulative < recent_total:
        raise CodexProfileError("lifetime_tokens are smaller than recent usage")
    if peak < max(daily_tokens):
        raise CodexProfileError("peak_daily_tokens are smaller than a daily bucket")
    return CodexProfileSnapshot(
        days=tuple(day.isoformat() for day in days),
        daily_tokens=daily_tokens,
        cumulative_tokens=cumulative,
        peak_tokens=peak,
        fetched_at=fetched_at,
    )


def _plain_token(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise CodexProfileError(f"{label} must be an integer")
    if value < 0 or value > MAX_TOKEN_VALUE:
        raise CodexProfileError(f"{label} is outside the supported range")
    return value
