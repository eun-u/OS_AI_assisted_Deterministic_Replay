#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def read_text(path: Path | None) -> str:
    if path is None or not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def input_hash(paths: list[Path | None]) -> str:
    h = hashlib.sha256()
    for path in paths:
        if path is None:
            continue
        h.update(str(path).encode("utf-8"))
        h.update(b"\0")
        if path.exists():
            h.update(path.read_bytes())
    return h.hexdigest()


def candidate_from_verify(verify: dict[str, Any]) -> list[dict[str, Any]]:
    checks = verify.get("checks", {})
    candidates: list[dict[str, Any]] = []
    if verify.get("status") == "pass":
        candidates.append(
            {
                "rank": 1,
                "label": "PASS",
                "confidence": "high",
                "evidence": ["all verifier checks passed"],
                "explanation": "The replay matched the checked outputs and replayable event sequence.",
            }
        )
    if not checks.get("stdout_hash", True):
        evidence = ["stdout_hash=false"]
        if verify.get("first_output_mismatch"):
            evidence.append("first_output_mismatch present")
        if verify.get("stdout_diff_path"):
            evidence.append(f"stdout_diff_path={verify.get('stdout_diff_path')}")
        candidates.append(
            {
                "rank": len(candidates) + 1,
                "label": "R-OUTPUT",
                "confidence": "medium",
                "evidence": evidence,
                "explanation": "The target produced different stdout while lower-level replay checks did not necessarily identify an event cursor drift.",
            }
        )
    if not checks.get("event_sequence", True):
        candidates.append(
            {
                "rank": len(candidates) + 1,
                "label": "R-EVENT",
                "confidence": "medium",
                "evidence": ["event_sequence=false", f"first_mismatch={verify.get('first_mismatch')}"],
                "explanation": "The replayable event sequence diverged and should be inspected before output-level symptoms.",
            }
        )
    if not checks.get("exit_code", True):
        candidates.append(
            {
                "rank": len(candidates) + 1,
                "label": "R-EXIT",
                "confidence": "medium",
                "evidence": ["exit_code=false"],
                "explanation": "The application followed a different termination path.",
            }
        )
    return candidates or [
        {
            "rank": 1,
            "label": "R-GENERIC",
            "confidence": "low",
            "evidence": ["no specific verifier signal matched"],
            "explanation": "No specific cause can be ranked from the available evidence.",
        }
    ]


def build_analysis(
    verify_path: Path,
    rule_analysis_path: Path,
    stdout_diff_path: Path | None,
    mode: str,
) -> dict[str, Any]:
    verify = load_json(verify_path)
    rule_text = read_text(rule_analysis_path)
    diff_text = read_text(stdout_diff_path)
    paths = [verify_path, rule_analysis_path, stdout_diff_path]
    checks = verify.get("checks", {})
    status = verify.get("status")
    summary = (
        "verification passed"
        if status == "pass"
        else "verification failed: "
        + ", ".join(name for name, ok in checks.items() if not ok)
    )
    limitations = [
        "LLM analysis does not override verifier status.",
        "This mock/offline mode summarizes supplied evidence only.",
    ]
    if verify.get("first_mismatch") is None:
        limitations.append("event-level first mismatch is unavailable for this run.")

    return {
        "schema_version": "adr.llm_analysis.v1",
        "status": "ok",
        "mode": mode,
        "model": "mock-evidence-summarizer" if mode == "mock" else "off",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "input_hash": input_hash(paths),
        "verifier_status": status,
        "summary": summary,
        "candidate_causes": candidate_from_verify(verify),
        "evidence_files": {
            "verify": str(verify_path),
            "rule_analysis": str(rule_analysis_path),
            "stdout_diff": str(stdout_diff_path) if stdout_diff_path else None,
        },
        "evidence_excerpt": {
            "rule_analysis": rule_text[:1200],
            "stdout_diff": diff_text[:1200],
        },
        "limitations": limitations,
        "next_actions": [
            "inspect stdout diff when stdout_hash=false",
            "inspect first_mismatch when event_sequence=false",
            "rerun full_validation.sh on Ubuntu VM or native Ubuntu before final submission",
        ],
    }


def render_markdown(analysis: dict[str, Any]) -> str:
    lines = [
        "# LLM-Assisted ADR Analysis",
        "",
        f"- mode: `{analysis['mode']}`",
        f"- model: `{analysis['model']}`",
        f"- verifier_status: `{analysis['verifier_status']}`",
        f"- input_hash: `{analysis['input_hash']}`",
        "",
        "## Summary",
        "",
        analysis["summary"],
        "",
        "## Candidate Causes",
        "",
    ]
    for candidate in analysis["candidate_causes"]:
        lines.append(f"{candidate['rank']}. `{candidate['label']}` confidence={candidate['confidence']}")
        lines.append(f"   - {candidate['explanation']}")
        lines.append(f"   - evidence: {', '.join(candidate['evidence'])}")
    lines.extend(["", "## Limitations", ""])
    for limitation in analysis["limitations"]:
        lines.append(f"- {limitation}")
    lines.extend(["", "## Next Actions", ""])
    for action in analysis["next_actions"]:
        lines.append(f"- {action}")
    lines.append("")
    return "\n".join(lines)


def analyze(
    verify_path: Path,
    rule_analysis_path: Path,
    out_md: Path,
    out_json: Path | None = None,
    stdout_diff_path: Path | None = None,
    mode: str = "mock",
) -> dict[str, Any]:
    if mode not in {"mock", "off"}:
        raise ValueError("only mock/off modes are currently supported")
    out_json = out_json or out_md.with_suffix(".json")
    analysis = build_analysis(verify_path, rule_analysis_path, stdout_diff_path, mode)
    out_md.parent.mkdir(parents=True, exist_ok=True)
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_md.write_text(render_markdown(analysis), encoding="utf-8")
    out_json.write_text(json.dumps(analysis, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {"status": "ok", "out_md": str(out_md), "out_json": str(out_json)}


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate evidence-grounded LLM-assisted ADR analysis.")
    parser.add_argument("--verify", required=True, type=Path)
    parser.add_argument("--analysis", required=True, type=Path)
    parser.add_argument("--stdout-diff", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--mode", choices=["mock", "off"], default="mock")
    args = parser.parse_args()
    result = analyze(args.verify, args.analysis, args.out, args.json_out, args.stdout_diff, args.mode)
    print(f"wrote {result['out_md']}")
    print(f"wrote {result['out_json']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
