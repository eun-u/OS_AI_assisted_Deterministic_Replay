#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import os
import subprocess
import sys
import time
from pathlib import Path


def discover_root() -> Path:
    here = Path(__file__).resolve()
    candidates = [Path.cwd(), here.parent, *here.parents]
    for candidate in candidates:
        if (candidate / "CMakeLists.txt").exists():
            return candidate
    return Path.cwd()


ROOT = discover_root()


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_helper(name: str) -> Path:
    exe = name + (".exe" if os.name == "nt" else "")
    candidates = [
        Path(__file__).resolve().parent / exe,
        ROOT / "build" / exe,
        Path.cwd() / "build" / exe,
        Path.cwd() / exe,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return Path(name)


def hook_path() -> Path | None:
    candidates = [
        ROOT / "build" / "libadr_sync_hook.so",
        Path.cwd() / "build" / "libadr_sync_hook.so",
        Path(__file__).resolve().parent / "libadr_sync_hook.so",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def normalize_remainder(items: list[str]) -> list[str]:
    if items and items[0] == "--":
        return items[1:]
    return items


def run_cmd(cmd: list[str], env: dict[str, str] | None = None, check: bool = True) -> int:
    print("+ " + " ".join(str(x) for x in cmd))
    completed = subprocess.run(cmd, env=env)
    if check and completed.returncode != 0:
        raise SystemExit(completed.returncode)
    return completed.returncode


def cmd_record(args: argparse.Namespace) -> int:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    if args.sync_hook:
        hook = hook_path()
        if hook:
            existing = env.get("LD_PRELOAD")
            env["LD_PRELOAD"] = str(hook) if not existing else f"{hook}:{existing}"
    target_args = normalize_remainder(args.args)
    cmd = [str(find_helper("adr-recorder")), "--out", str(out), "--", args.target, *target_args]
    return run_cmd(cmd, env=env)


def cmd_replay(args: argparse.Namespace) -> int:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    cmd = [str(find_helper("adr-replayer")), "--trace", args.trace, "--out", str(out)]
    if args.target:
        cmd.extend(["--target", args.target])
    return run_cmd(cmd)


def cmd_verify(args: argparse.Namespace) -> int:
    verifier = load_module("adr_verifier", ROOT / "verifier" / "verifier.py")
    result = verifier.verify(Path(args.original), Path(args.replay))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    import json

    out.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{result['status']}: wrote {out}")
    return 0 if result["status"] == "pass" else 1


def cmd_analyze(args: argparse.Namespace) -> int:
    analyzer = load_module("adr_analyzer", ROOT / "analyzer" / "rule_analyzer.py")
    result = analyzer.analyze(
        Path(args.original),
        Path(args.replay),
        Path(args.verify),
        Path(args.out),
        args.top_k,
    )
    print(f"wrote {result['out']}")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    trace_root = Path(args.trace_root)
    report = Path(args.report)
    trace_root.mkdir(parents=True, exist_ok=True)
    report.parent.mkdir(parents=True, exist_ok=True)

    final_verify = None
    final_analysis = report
    for index in range(args.repeat):
        stamp = time.strftime("%Y%m%d_%H%M%S")
        suffix = f"{stamp}_{time.time_ns() % 1_000_000_000:09d}_{index + 1:02d}"
        original = trace_root / f"run_{suffix}_record"
        replay = trace_root / f"run_{suffix}_replay"
        verify_path = report.parent / f"{report.stem}_{suffix}_verify.json"

        record_args = argparse.Namespace(
            target=args.target,
            out=str(original),
            args=normalize_remainder(args.args),
            sync_hook=args.sync_hook,
        )
        cmd_record(record_args)
        replay_args = argparse.Namespace(trace=str(original), out=str(replay), target=args.target)
        cmd_replay(replay_args)
        verify_args = argparse.Namespace(original=str(original), replay=str(replay), out=str(verify_path))
        verify_rc = cmd_verify(verify_args)
        analyze_args = argparse.Namespace(
            original=str(original),
            replay=str(replay),
            verify=str(verify_path),
            out=str(final_analysis),
            top_k=3,
        )
        cmd_analyze(analyze_args)
        final_verify = verify_path
        if verify_rc != 0 and args.stop_on_fail:
            return verify_rc

    print(f"verify: {final_verify}")
    print(f"analysis: {final_analysis}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="adr", description="AI-assisted deterministic replay CLI")
    sub = parser.add_subparsers(dest="command", required=True)

    record = sub.add_parser("record", help="record target execution")
    record.add_argument("--target", required=True)
    record.add_argument("--out", required=True)
    record.add_argument("--no-sync-hook", dest="sync_hook", action="store_false")
    record.set_defaults(sync_hook=True, func=cmd_record)
    record.add_argument("args", nargs=argparse.REMAINDER)

    replay = sub.add_parser("replay", help="replay from a trace")
    replay.add_argument("--trace", required=True)
    replay.add_argument("--out", required=True)
    replay.add_argument("--target")
    replay.set_defaults(func=cmd_replay)

    verify = sub.add_parser("verify", help="compare original and replay traces")
    verify.add_argument("--original", required=True)
    verify.add_argument("--replay", required=True)
    verify.add_argument("--out", required=True)
    verify.set_defaults(func=cmd_verify)

    analyze = sub.add_parser("analyze", help="rank drift root-cause candidates")
    analyze.add_argument("--original", required=True)
    analyze.add_argument("--replay", required=True)
    analyze.add_argument("--verify", required=True)
    analyze.add_argument("--out", required=True)
    analyze.add_argument("--top-k", type=int, default=3)
    analyze.set_defaults(func=cmd_analyze)

    run = sub.add_parser("run", help="record, replay, verify, and analyze")
    run.add_argument("--target", required=True)
    run.add_argument("--trace-root", default="traces")
    run.add_argument("--report", default="reports/analysis.md")
    run.add_argument("--repeat", type=int, default=1)
    run.add_argument("--stop-on-fail", action="store_true")
    run.add_argument("--no-sync-hook", dest="sync_hook", action="store_false")
    run.set_defaults(sync_hook=True, func=cmd_run)
    run.add_argument("args", nargs=argparse.REMAINDER)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
