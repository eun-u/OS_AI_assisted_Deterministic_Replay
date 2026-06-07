# LLM Assist Design

The LLM-assisted layer summarizes verifier and rule-analyzer evidence. It must not override verifier results.

## Role

The LLM layer is an investigation assistant:

- summarizes `verify.json`
- summarizes rule analyzer evidence
- includes stdout diff evidence when available
- lists candidate causes, limitations, and next actions

It is not a root-cause oracle. Reports must use language such as candidate, likely, or inspect, and must not claim a confirmed root cause unless a deterministic verifier proves it.

## Inputs

- `reports/*_verify.json`
- `reports/*.md` from the rule analyzer
- optional stdout diff report

## Outputs

- `reports/*_llm_analysis.md`
- `reports/*_llm_analysis.json`

The JSON output includes schema version, mode, model, input hash, verifier status, candidate causes, limitations, and next actions.

## Modes

The current implementation supports `mock` and `off` modes. `mock` is deterministic and API-free, so CI and classroom environments can reproduce it. A future provider can be added behind the same interface without changing verifier semantics.

## Safety Rules

- Evidence only: do not invent causes that are absent from verifier/analyzer inputs.
- No override: LLM output cannot change `verify_status`.
- No overclaiming: use candidate language.
- Reproducibility: include input hash and generated timestamp.
