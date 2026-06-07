#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def load_events(trace: Path) -> list[dict[str, Any]]:
    path = trace / "events.jsonl"
    events: list[dict[str, Any]] = []
    if not path.exists():
        return events
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    events.append(json.loads(line))
                except json.JSONDecodeError:
                    break
    return events


def score_candidates(original: Path, replay: Path, verify: dict[str, Any], top_k: int) -> list[dict[str, Any]]:
    original_events = load_events(original)
    replay_events = load_events(replay)
    mismatch = verify.get("first_mismatch") or {}
    if verify.get("status") == "pass" and not mismatch:
        signals = []
        if any(ev.get("name") in {"read", "getrandom", "clock_gettime", "gettimeofday"} for ev in original_events + replay_events):
            signals.append("replayable I/O, time, or random events were observed")
        if any(ev.get("etype") == "SYNC" for ev in original_events + replay_events):
            signals.append("pthread synchronization events were recorded")
        reason = "; ".join(signals) if signals else "no drift was detected in checked outputs and replayable events"
        return [
            {
                "rule": "PASS",
                "score": 1.0,
                "reason": reason,
            }
        ][:top_k]
    original_seq = mismatch.get("original_seq")
    replay_seq = mismatch.get("replay_seq")

    def window(events: list[dict[str, Any]], seq: int | None) -> list[dict[str, Any]]:
        if seq is None:
            return events[-20:]
        return [ev for ev in events if abs(int(ev.get("seq", 0)) - int(seq)) <= 10]

    combined = window(original_events, original_seq) + window(replay_events, replay_seq)
    candidates: list[dict[str, Any]] = []

    if any(ev.get("name") in {"read", "getrandom", "clock_gettime", "gettimeofday"} for ev in combined):
        candidates.append(
            {
                "rule": "R-IO",
                "score": 0.95,
                "reason": "read/getrandom/time events appear near the drift window; external input, time, or randomness may have affected control flow",
            }
        )

    sync_events = [ev for ev in combined if ev.get("etype") == "SYNC"]
    if sync_events:
        candidates.append(
            {
                "rule": "R-SYNC",
                "score": 0.88,
                "reason": "pthread synchronization events appear near the drift window; scheduling or lock ordering may have changed",
            }
        )

    if not verify.get("checks", {}).get("stdout_hash", True):
        candidates.append(
            {
                "rule": "R-OUTPUT",
                "score": 0.82,
                "reason": "stdout hash changed; the output path likely diverged near the first drift",
            }
        )

    if any("mutex" in str(ev.get("name", "")) for ev in combined) and not verify.get("checks", {}).get("event_sequence", True):
        candidates.append(
            {
                "rule": "R-DEADLOCK",
                "score": 0.7,
                "reason": "mutex event ordering changed; check for deadlock or missed wakeup behavior",
            }
        )

    if not verify.get("checks", {}).get("exit_code", True):
        candidates.append(
            {
                "rule": "R-SYSCALL",
                "score": 0.65,
                "reason": "exit code changed; syscall return/errno or exception paths may differ",
            }
        )

    if not candidates:
        candidates.append(
            {
                "rule": "R-GENERIC",
                "score": 0.4,
                "reason": "no specific rule matched; inspect the event window before the first mismatch manually",
            }
        )

    return sorted(candidates, key=lambda item: item["score"], reverse=True)[:top_k]


def render_markdown(original: Path, replay: Path, verify: dict[str, Any], candidates: list[dict[str, Any]]) -> str:
    lines = [
        "# ADR Analysis Report",
        "",
        f"- original: `{original}`",
        f"- replay: `{replay}`",
        f"- verify_status: `{verify.get('status')}`",
        "",
        "## First Mismatch",
        "",
        "```json",
        json.dumps(verify.get("first_mismatch"), indent=2, ensure_ascii=False),
        "```",
        "",
        "## Top Signals",
        "",
    ]
    for i, candidate in enumerate(candidates, 1):
        lines.append(f"{i}. `{candidate['rule']}` score={candidate['score']:.2f}")
        lines.append(f"   - {candidate['reason']}")
    lines.append("")
    lines.append("Analyzer output is an investigation priority, not a confirmed root cause.")
    return "\n".join(lines) + "\n"


def analyze(original: Path, replay: Path, verify_path: Path, out: Path, top_k: int) -> dict[str, Any]:
    verify = json.loads(verify_path.read_text(encoding="utf-8"))
    candidates = score_candidates(original, replay, verify, top_k)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(render_markdown(original, replay, verify, candidates), encoding="utf-8")
    return {"status": "ok", "out": str(out), "candidates": candidates}


def main() -> int:
    parser = argparse.ArgumentParser(description="Run ADR rule-based analyzer.")
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--replay", required=True, type=Path)
    parser.add_argument("--verify", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--top-k", type=int, default=3)
    args = parser.parse_args()

    result = analyze(args.original, args.replay, args.verify, args.out, args.top_k)
    print(f"wrote {result['out']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
