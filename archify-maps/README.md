# Native InferScale architecture overviews

These seven standalone Archify HTML files complement the detailed root Markdown maps. Download/open the HTML locally for the interactive SVG viewer, themes and exports; GitHub renders the Markdown Mermaid diagrams directly. Each native diagram is a compact overview of its matching guide, not a replacement for its detailed diagrams and file table.

| Root guide | Native architecture overview |
|---|---|
| [System](../ARCHIFY_SYSTEM_MAP.md) | [system-map.html](system-map.html) |
| [Request](../ARCHIFY_REQUEST_FLOW.md) | [request-flow.html](request-flow.html) |
| [Scheduler](../ARCHIFY_SCHEDULER_MAP.md) | [scheduler-map.html](scheduler-map.html) |
| [Resources](../ARCHIFY_RESOURCE_LIFECYCLE.md) | [resource-lifecycle.html](resource-lifecycle.html) |
| [Failures](../ARCHIFY_FAILURE_AND_CANCELLATION_MAP.md) | [failure-and-cancellation-map.html](failure-and-cancellation-map.html) |
| [Evidence](../ARCHIFY_BENCHMARK_AND_EVIDENCE_MAP.md) | [benchmark-and-evidence-map.html](benchmark-and-evidence-map.html) |
| [Codebase](../ARCHIFY_CODEBASE_MAP.md) | [codebase-map.html](codebase-map.html) |

All specifications use architecture mode, showcase quality, and source references pinned to release commit `cd7daf96479044ea81237fcde8f482aae5c7dd23`. Their adjacent JSON candidates and delivery receipts retain artifact provenance; final resource-map finalize/browser receipts are in resource-review-2 after its citation repair, and the other final receipts are adjacent. All seven passed validate, deliver, strict artifact check and browser-check. Automated browser containment checks cover 1440, 1600, 1920 and 2048 pixel desktop widths in the light theme; perceptual visual review was not performed. No motion was requested.

Tool: installed Archify 3.0.1 at `/home/neel/.agents/skills/archify/bin/archify.mjs`. No packages were installed. Existing Graphify output was read for navigation and preserved unchanged; Graphify extraction was not rerun.

Reproduce an individual native artifact from the repository root with the installed skill CLI (substitute its path on another host):

```sh
node /home/neel/.agents/skills/archify/bin/archify.mjs finalize architecture \
  archify-maps/system-map.json archify-maps/system-map.html \
  --repo-root . --quality showcase --json
```

Root Mermaid blocks received structural and manual syntax review; no Mermaid renderer/parser was available locally. This is distinct from the passing native Archify browser checks. The focused internal read-only reviews checked implementation order, ownership, lifecycle, scheduling and claim boundaries. External Claude review was blocked by automatic approval review, so no repository payload was sent to Claude.

The directory-local `.gitattributes` exempts generated HTML from blank-at-end-of-line checks because the bundled viewer emits whitespace-only lines. Keeping the renderer bytes intact preserves strict artifact hashes and browser provenance; hand-authored Markdown and JSON retain normal whitespace checks.
