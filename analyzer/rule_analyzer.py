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


def trace_stdout(trace: Path) -> str:
    path = trace / "stdout.log"
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def has_timeout_marker(original: Path, replay: Path) -> bool:
    text = trace_stdout(original) + "\n" + trace_stdout(replay)
    return "timeout_or_deadlock_detected" in text


def score_candidates(original: Path, replay: Path, verify: dict[str, Any], top_k: int) -> list[dict[str, Any]]:
    original_events = load_events(original)
    replay_events = load_events(replay)
    mismatch = verify.get("first_mismatch") or {}
    timeout_marker = has_timeout_marker(original, replay)
    if verify.get("status") == "pass" and not mismatch:
        if timeout_marker:
            return [
                {
                    "rule": "R-DEADLOCK",
                    "score": 0.90,
                    "reason": "timeout marker was observed while record/replay output remained stable; this is a no-progress/deadlock smoke signal",
                    "evidence": ["timeout_or_deadlock_detected in stdout", "verifier_status=pass"],
                    "score_breakdown": {"base": 0.55, "timeout_marker": 0.30, "stable_replay": 0.05},
                }
            ][:top_k]
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
                "evidence": signals or ["all verifier checks passed"],
                "score_breakdown": {"base": 1.0},
            }
        ][:top_k]
    if verify.get("status") == "fail" and not mismatch:
        candidates: list[dict[str, Any]] = []
        checks = verify.get("checks", {})
        if timeout_marker:
            candidates.append(
                {
                    "rule": "R-DEADLOCK",
                    "score": 0.94,
                    "reason": "timeout marker was observed and high-level replay checks failed; inspect no-progress or deadlock behavior first",
                    "evidence": ["timeout_or_deadlock_detected in stdout", "first_mismatch=null", f"failed_checks={[name for name, ok in checks.items() if not ok]}"],
                    "score_breakdown": {"base": 0.55, "timeout_marker": 0.30, "high_level_drift": 0.09},
                }
            )
        if not checks.get("stdout_hash", True):
            candidates.append(
                {
                    "rule": "R-OUTPUT",
                    "score": 0.92,
                    "reason": "stdout changed even though replayable syscall alignment passed; inspect output-producing code and non-replayed process/environment values",
                    "evidence": [
                        "stdout_hash=false",
                        f"first_output_mismatch={bool(verify.get('first_output_mismatch'))}",
                        f"stdout_diff_path={verify.get('stdout_diff_path')}",
                    ],
                    "score_breakdown": {"base": 0.55, "stdout_changed": 0.27, "event_alignment_passed": 0.10},
                }
            )
        if not checks.get("exit_code", True):
            candidates.append(
                {
                    "rule": "R-EXIT",
                    "score": 0.86,
                    "reason": "exit code changed without a replayable syscall cursor mismatch; inspect application-level branch and error paths",
                    "evidence": ["exit_code=false", "event_sequence=true"],
                    "score_breakdown": {"base": 0.55, "exit_changed": 0.31},
                }
            )
        if any(ev.get("etype") == "SYNC" for ev in original_events + replay_events):
            candidates.append(
                {
                    "rule": "R-SYNC",
                    "score": 0.72,
                    "reason": "pthread synchronization events were present; scheduling-dependent state remains a possible cause for output drift",
                    "evidence": ["SYNC events observed", f"original_events={verify.get('original', {}).get('event_count')}"],
                    "score_breakdown": {"base": 0.50, "sync_present": 0.22},
                }
            )
        if not candidates:
            candidates.append(
                {
                    "rule": "R-HIGHLEVEL",
                    "score": 0.5,
                    "reason": "high-level result checks failed, but no replayable syscall mismatch was found",
                    "evidence": ["one or more verifier checks failed", "first_mismatch=null"],
                    "score_breakdown": {"base": 0.50},
                }
            )
        return candidates[:top_k]
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
                "evidence": ["read/getrandom/time event observed near mismatch window"],
                "score_breakdown": {"base": 0.50, "io_event_near_drift": 0.35, "payload_sensitive": 0.10},
            }
        )

    sync_events = [ev for ev in combined if ev.get("etype") == "SYNC"]
    if sync_events:
        candidates.append(
            {
                "rule": "R-SYNC",
                "score": 0.88,
                "reason": "pthread synchronization events appear near the drift window; scheduling or lock ordering may have changed",
                "evidence": [f"sync_events_in_window={len(sync_events)}"],
                "score_breakdown": {"base": 0.50, "sync_near_drift": 0.25, "ordering_uncertainty": 0.13},
            }
        )

    if not verify.get("checks", {}).get("stdout_hash", True):
        candidates.append(
            {
                "rule": "R-OUTPUT",
                "score": 0.82,
                "reason": "stdout hash changed; the output path likely diverged near the first drift",
                "evidence": ["stdout_hash=false", f"stdout_diff_path={verify.get('stdout_diff_path')}"],
                "score_breakdown": {"base": 0.55, "stdout_changed": 0.27},
            }
        )

    if any("mutex" in str(ev.get("name", "")) for ev in combined) and not verify.get("checks", {}).get("event_sequence", True):
        candidates.append(
            {
                "rule": "R-DEADLOCK",
                "score": 0.7,
                "reason": "mutex event ordering changed; check for deadlock or missed wakeup behavior",
                "evidence": ["mutex event observed", "event_sequence=false"],
                "score_breakdown": {"base": 0.45, "mutex_event": 0.15, "sequence_changed": 0.10},
            }
        )

    if not verify.get("checks", {}).get("exit_code", True):
        candidates.append(
            {
                "rule": "R-SYSCALL",
                "score": 0.65,
                "reason": "exit code changed; syscall return/errno or exception paths may differ",
                "evidence": ["exit_code=false"],
                "score_breakdown": {"base": 0.45, "exit_changed": 0.20},
            }
        )

    if not candidates:
        candidates.append(
            {
                "rule": "R-GENERIC",
                "score": 0.4,
                "reason": "no specific rule matched; inspect the event window before the first mismatch manually",
                "evidence": ["no rule-specific evidence matched"],
                "score_breakdown": {"base": 0.40},
            }
        )

    return sorted(candidates, key=lambda item: item["score"], reverse=True)[:top_k]


def render_markdown(original: Path, replay: Path, verify: dict[str, Any], candidates: list[dict[str, Any]]) -> str:
    first_mismatch = verify.get("first_mismatch")
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
        json.dumps(first_mismatch, indent=2, ensure_ascii=False),
        "```",
        "",
    ]
    checks = verify.get("checks", {})
    if verify.get("status") == "fail" and not first_mismatch:
        failed = [name for name, ok in checks.items() if not ok]
        lines.extend(
            [
                "## Drift Summary",
                "",
                "No replayable syscall cursor mismatch was found, but one or more high-level checks failed.",
                f"- failed_checks: `{', '.join(failed) if failed else 'unknown'}`",
                "",
            ]
        )
        if "stdout_hash" in failed:
            lines.extend(
                [
                    "The target completed with a different stdout hash. This indicates high-level output drift even though the checked replayable syscall sequence aligned.",
                    "",
                ]
            )
    lines.extend(["## Top Signals", ""])
    for i, candidate in enumerate(candidates, 1):
        lines.append(f"{i}. `{candidate['rule']}` score={candidate['score']:.2f}")
        lines.append(f"   - {candidate['reason']}")
        evidence = candidate.get("evidence") or []
        if evidence:
            lines.append(f"   - evidence: {', '.join(str(item) for item in evidence)}")
        breakdown = candidate.get("score_breakdown") or {}
        if breakdown:
            parts = [f"{key}={value:.2f}" if isinstance(value, (int, float)) else f"{key}={value}" for key, value in breakdown.items()]
            lines.append(f"   - score_breakdown: {', '.join(parts)}")
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
