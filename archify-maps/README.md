# Native InferScale architecture overviews

These seven Archify architecture specs complement the detailed root Markdown maps. GitHub renders the Markdown Mermaid diagrams directly. Each spec is a compact overview of its matching guide, not a replacement for its detailed diagrams and file table.

**Storage strategy.** The standalone interactive HTML viewers (about 750 KB each, 5.2 MB in total) are generated artifacts and are deliberately not committed: they would dominate GitHub's language statistics and cannot be reviewed as source. The tracked sources of truth are the `*.json` specs, with their `*.delivery.json`, `*.finalize*.json` and `*.browser-check.json` receipts retained as provenance. The HTML files above were previously committed in history (for example at the `v1.0-resume-grade` tag) and remain retrievable from there; to render fresh copies locally, use the commands below. Rendered files are ignored by `.gitignore`.

| Root guide | Archify source spec |
|---|---|
| [System](../ARCHIFY_SYSTEM_MAP.md) | [system-map.json](system-map.json) |
| [Request](../ARCHIFY_REQUEST_FLOW.md) | [request-flow.json](request-flow.json) |
| [Scheduler](../ARCHIFY_SCHEDULER_MAP.md) | [scheduler-map.json](scheduler-map.json) |
| [Resources](../ARCHIFY_RESOURCE_LIFECYCLE.md) | [resource-lifecycle.json](resource-lifecycle.json) |
| [Failures](../ARCHIFY_FAILURE_AND_CANCELLATION_MAP.md) | [failure-and-cancellation-map.json](failure-and-cancellation-map.json) |
| [Evidence](../ARCHIFY_BENCHMARK_AND_EVIDENCE_MAP.md) | [benchmark-and-evidence-map.json](benchmark-and-evidence-map.json) |
| [Codebase](../ARCHIFY_CODEBASE_MAP.md) | [codebase-map.json](codebase-map.json) |

All specifications use architecture mode, showcase quality, and source references pinned to release commit `cd7daf96479044ea81237fcde8f482aae5c7dd23`. Their adjacent JSON candidates and delivery receipts retain artifact provenance; final resource-map finalize/browser receipts are in resource-review-2 after its citation repair, and the other final receipts are adjacent. All seven passed validate, deliver, strict artifact check and browser-check. Automated browser containment checks cover 1440, 1600, 1920 and 2048 pixel desktop widths in the light theme; perceptual visual review was not performed. No motion was requested.

Tool: installed Archify 3.0.1 at `/home/neel/.agents/skills/archify/bin/archify.mjs`. No packages were installed. Existing Graphify output was read for navigation and preserved unchanged; Graphify extraction was not rerun.

Render the native viewers from the repository root with the installed skill CLI (substitute its path on another host). The outputs land next to the specs and are ignored by Git:

```sh
ARCHIFY=/home/neel/.agents/skills/archify/bin/archify.mjs
for m in system-map request-flow scheduler-map resource-lifecycle \
         failure-and-cancellation-map benchmark-and-evidence-map codebase-map; do
  node "$ARCHIFY" finalize architecture archify-maps/$m.json archify-maps/$m.html \
    --repo-root . --quality showcase --json
done
```

Root Mermaid blocks received structural and manual syntax review; no Mermaid renderer/parser was available locally. This is distinct from the passing native Archify browser checks. The focused internal read-only reviews checked implementation order, ownership, lifecycle, scheduling and claim boundaries. External Claude review was blocked by automatic approval review, so no repository payload was sent to Claude.

The directory-local `.gitattributes` exempts generated HTML from blank-at-end-of-line checks because the bundled viewer emits whitespace-only lines, so a locally rendered viewer never trips `git diff --check`. Hand-authored Markdown and JSON retain normal whitespace checks. The `delivery.json`/`finalize*.json` receipts record the hashes of the originally committed HTML; re-rendered output may differ byte-for-byte.
