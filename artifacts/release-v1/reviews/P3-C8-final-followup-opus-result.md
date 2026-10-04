# P3-C8 final Opus blocker review

- Model: Claude Opus 5.5 Medium
- Route: firstParty Claude.ai Pro through Claude Code 2.1.285
- Outcome: completed after one controlled retry; the first attempt returned `EAI_AGAIN` before model tokens were exchanged.
- Review scope: final A–L dispositions, release claims, benchmark exclusion/inference behavior, resource soak, sanitizers, and indexed evidence integrity.

## Material finding and disposition

Opus found five malformed 38-character historical source commit IDs in `manifest.json` and `test-summary.json`. A bounded exact-reference check found the same malformed value in three more provenance records: `resource-soak-index.json`, `clean-worktree-summary.json`, and `commands.txt`. This was the same finding, not a separate defect. The authoritative source-binding records use the valid 40-character commit `7f27cf33b6c452015f922d8a8a3d8deb8e0a7f79`.

All eight malformed occurrences across five files were corrected. `git cat-file -e` confirmed the corrected commit exists. The corrected values now match `resource-soak-summary.json` and the other source-binding records. The `test-summary.json` and `resource-soak-index.json` derived hashes in `benchmark/raw-index.json`, plus the integrity summary, were refreshed. Verification covered 1,174 hash references across 1,112 unique paths: no missing paths and no hash mismatches.

No executable source changed, so no code test rerun was required. Opus found no other material blocker. Optional wording suggestions were not release blockers and were left unchanged.

## Review limitation

The final Sonnet diff review was unavailable after its documented controlled retry. This is recorded in the review ledger and is not an objective release blocker.
