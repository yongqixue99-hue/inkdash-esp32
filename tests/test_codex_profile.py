from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest


HOST_DIR = Path(__file__).resolve().parents[1] / "host"
sys.path.insert(0, str(HOST_DIR))

from codex_profile import CodexProfileClient, CodexProfileError  # noqa: E402


class CodexProfileClientTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.auth_path = Path(self.temporary.name) / "auth.json"
        self.auth_path.write_text(
            json.dumps(
                {
                    "tokens": {
                        "access_token": "private-access-token",
                        "account_id": "account-123",
                    }
                }
            ),
            encoding="utf-8",
        )
        self.calls: list[tuple[str, str, str, float]] = []

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_account_snapshot_overrides_local_raw_token_totals(self):
        client = self._client(self._valid_response())
        local = {
            "remaining_percent": 85,
            "used_percent": 15,
            "reset_date": "2026-08-28",
            "reset_at": 1787861400,
            "usage_scope": "WIN+MAC",
            "daily_tokens": [900] * 7,
            "daily_usage_centi_yi": [0] * 7,
            "today_tokens": 900,
            "week_tokens": 6300,
            "generated_at": 1787449500,
        }

        merged = client.overlay(local)

        self.assertEqual("ACCOUNT", merged["usage_scope"])
        self.assertEqual(
            [
                "2026-08-16",
                "2026-08-17",
                "2026-08-18",
                "2026-08-19",
                "2026-08-20",
                "2026-08-21",
                "2026-08-22",
            ],
            merged["daily_token_dates"],
        )
        self.assertEqual(
            [219_588_462, 442_039_874, 359_105_258, 754_774_206,
             323_421_425, 208_767_556, 81_203_490],
            merged["daily_tokens"],
        )
        self.assertEqual(81_203_490, merged["today_tokens"])
        self.assertEqual(2_388_900_271, merged["week_tokens"])
        self.assertEqual(2_388_900_271, merged["cumulative_tokens"])
        self.assertEqual(754_774_206, merged["peak_tokens"])
        self.assertEqual([220, 442, 359, 755, 323, 209, 81], merged["daily_usage_centi_yi"])
        self.assertEqual(1787449500, merged["token_generated_at"])
        self.assertEqual(1787449500, merged["generated_at"])
        self.assertEqual(1, len(self.calls))
        self.assertEqual("private-access-token", self.calls[0][1])
        self.assertEqual("account-123", self.calls[0][2])

    def test_snapshot_is_cached_without_rereading_remote_endpoint(self):
        client = self._client(self._valid_response(), cache_seconds=300)

        first = client.snapshot()
        second = client.snapshot()

        self.assertIs(first, second)
        self.assertEqual(1, len(self.calls))

    def test_rejects_lifetime_total_smaller_than_recent_buckets(self):
        response = self._valid_response()
        response["stats"]["lifetime_tokens"] = 1
        client = self._client(response)

        with self.assertRaisesRegex(CodexProfileError, "lifetime_tokens"):
            client.snapshot()

    def test_missing_auth_is_reported_without_secret_material(self):
        self.auth_path.unlink()
        client = self._client(self._valid_response())

        with self.assertRaises(CodexProfileError) as raised:
            client.snapshot()

        self.assertNotIn("private-access-token", str(raised.exception))

    def _client(self, response, *, cache_seconds: int = 0):
        def fetcher(endpoint: str, access_token: str, account_id: str, timeout: float):
            self.calls.append((endpoint, access_token, account_id, timeout))
            return response

        return CodexProfileClient(
            self.auth_path,
            cache_seconds=cache_seconds,
            clock=lambda: 1787449500,
            fetcher=fetcher,
        )

    @staticmethod
    def _valid_response():
        values = [
            219_588_462,
            442_039_874,
            359_105_258,
            754_774_206,
            323_421_425,
            208_767_556,
            81_203_490,
        ]
        return {
            "metadata": {"stats_error": ""},
            "stats": {
                "lifetime_tokens": sum(values),
                "peak_daily_tokens": max(values),
                "daily_usage_buckets": [
                    {"start_date": f"2026-08-{day:02d}", "tokens": value}
                    for day, value in zip(range(16, 23), values, strict=True)
                ],
            },
        }


if __name__ == "__main__":
    unittest.main()
