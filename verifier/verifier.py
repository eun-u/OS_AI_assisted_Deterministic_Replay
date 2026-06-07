#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import json
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def load_events(trace: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    path = trace / "events.jsonl"
    if not path.exists():
        return events
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError as exc:
                events.append(
                    {
                        "etype": "TRACE_ERROR",
                        "name": "json_decode_error",
                        "seq": line_no,
                        "error": str(exc),
                    }
                )
                break
    return events


def comparable_event(ev: dict[str, Any]) -> tuple[Any, ...]:
    payload = ev.get("payload_ref") or {}
    args = ev.get("args") or {}
    return (
        ev.get("etype"),
        ev.get("name"),
        args.get("syscall_no"),
        ev.get("ret"),
        ev.get("errno"),
        payload.get("sha256"),
    )


def checked_events(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    checked: list[dict[str, Any]] = []
    for ev in events:
        etype = ev.get("etype")
        name = ev.get("name")
        if etype == "DRIFT":
            checked.append(ev)
        elif etype == "SYSCALL" and name in {"getrandom", "clock_gettime", "gettimeofday"}:
            checked.append(ev)
        elif etype == "SYSCALL" and name == "read":
            payload = ev.get("payload_ref") or {}
            if payload and int(payload.get("length") or 0) <= 256:
                checked.append(ev)
    return checked


def first_sequence_mismatch(a: list[dict[str, Any]], b: list[dict[str, Any]]) -> dict[str, Any] | None:
    aa = checked_events(a)
    bb = checked_events(b)
    limit = min(len(aa), len(bb))
    for i in range(limit):
        if comparable_event(aa[i]) != comparable_event(bb[i]):
            return {
                "index": i,
                "original_seq": aa[i].get("seq"),
                "replay_seq": bb[i].get("seq"),
                "original": comparable_event(aa[i]),
                "replay": comparable_event(bb[i]),
            }
    if len(aa) != len(bb):
        return {
            "index": limit,
            "original_seq": aa[limit].get("seq") if limit < len(aa) else None,
            "replay_seq": bb[limit].get("seq") if limit < len(bb) else None,
            "original": comparable_event(aa[limit]) if limit < len(aa) else None,
            "replay": comparable_event(bb[limit]) if limit < len(bb) else None,
        }
    return None


def load_text_lines(path: Path) -> list[str]:
    if not path.exists():
        return []
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def first_output_mismatch(original: Path, replay: Path, stream: str) -> dict[str, Any] | None:
    original_lines = load_text_lines(original / f"{stream}.log")
    replay_lines = load_text_lines(replay / f"{stream}.log")
    limit = min(len(original_lines), len(replay_lines))
    for index in range(limit):
        if original_lines[index] != replay_lines[index]:
            return {
                "stream": stream,
                "line": index + 1,
                "kind": f"{stream}_line_diff",
                "original": original_lines[index],
                "replay": replay_lines[index],
            }
    if len(original_lines) != len(replay_lines):
        return {
            "stream": stream,
            "line": limit + 1,
            "kind": f"{stream}_length_diff",
            "original": original_lines[limit] if limit < len(original_lines) else None,
            "replay": replay_lines[limit] if limit < len(replay_lines) else None,
            "original_line_count": len(original_lines),
            "replay_line_count": len(replay_lines),
        }
    return None


def write_stream_diff(original: Path, replay: Path, stream: str, out_path: Path) -> dict[str, Any]:
    original_lines = load_text_lines(original / f"{stream}.log")
    replay_lines = load_text_lines(replay / f"{stream}.log")
    diff_lines = list(
        difflib.unified_diff(
            original_lines,
            replay_lines,
            fromfile=f"original/{stream}.log",
            tofile=f"replay/{stream}.log",
            lineterm="",
        )
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    body = [
        f"# {stream.upper()} Diff",
        "",
        f"- original: `{original / f'{stream}.log'}`",
        f"- replay: `{replay / f'{stream}.log'}`",
        "",
        "```diff",
        *diff_lines,
        "```",
        "",
    ]
    out_path.write_text("\n".join(body), encoding="utf-8")
    return {
        "path": str(out_path),
        "diff_line_count": len(diff_lines),
        "preview": diff_lines[:20],
    }


def diff_path_for_verify(out_path: Path | None, stream: str) -> Path | None:
    if out_path is None:
        return None
    stem = out_path.stem
    if stem.endswith("_verify"):
        stem = stem[: -len("_verify")]
    return out_path.with_name(f"{stem}_{stream}_diff.md")


def verify(original: Path, replay: Path, out_path: Path | None = None) -> dict[str, Any]:
    original_result = load_json(original / "result.json")
    replay_result = load_json(replay / "result.json")
    original_events = load_events(original)
    replay_events = load_events(replay)
    first_mismatch = first_sequence_mismatch(original_events, replay_events)

    checks = {
        "exit_code": original_result.get("exit_code") == replay_result.get("exit_code"),
        "stdout_hash": original_result.get("stdout_hash") == replay_result.get("stdout_hash"),
        "stderr_hash": original_result.get("stderr_hash") == replay_result.get("stderr_hash"),
        "event_sequence": first_mismatch is None,
    }

    drift_events = [ev for ev in replay_events if ev.get("etype") == "DRIFT"]
    if drift_events:
        checks["event_sequence"] = False
        first_mismatch = first_mismatch or {
            "index": None,
            "original_seq": None,
            "replay_seq": drift_events[0].get("seq"),
            "original": None,
            "replay": comparable_event(drift_events[0]),
        }

    output_mismatch = None
    stdout_diff = None
    stderr_diff = None
    if not checks["stdout_hash"]:
        output_mismatch = first_output_mismatch(original, replay, "stdout")
        stdout_diff_path = diff_path_for_verify(out_path, "stdout")
        if stdout_diff_path is not None:
            stdout_diff = write_stream_diff(original, replay, "stdout", stdout_diff_path)
    if not checks["stderr_hash"]:
        output_mismatch = output_mismatch or first_output_mismatch(original, replay, "stderr")
        stderr_diff_path = diff_path_for_verify(out_path, "stderr")
        if stderr_diff_path is not None:
            stderr_diff = write_stream_diff(original, replay, "stderr", stderr_diff_path)

    return {
        "schema_version": "adr.verify.v1",
        "status": "pass" if all(checks.values()) else "fail",
        "checks": checks,
        "original": {
            "trace": str(original),
            "result": original_result,
            "event_count": len(original_events),
        },
        "replay": {
            "trace": str(replay),
            "result": replay_result,
            "event_count": len(replay_events),
        },
        "first_mismatch": first_mismatch,
        "first_output_mismatch": output_mismatch,
        "stdout_diff_path": stdout_diff["path"] if stdout_diff else None,
        "stderr_diff_path": stderr_diff["path"] if stderr_diff else None,
        "stdout_diff_summary": stdout_diff,
        "stderr_diff_summary": stderr_diff,
        "drift_events": drift_events[:10],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify ADR original/replay traces.")
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--replay", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    result = verify(args.original, args.replay, args.out)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{result['status']}: wrote {args.out}")
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
