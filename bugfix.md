# bugfix.md

This document lists the next 5 improvements to prioritize for robust ODT handling and higher-fidelity Markdown <-> ODT conversion for documents like the ODF standard files.

## 1. Remove hard size caps in ODT read/import paths (streaming or chunked processing)

Why:
- Current fixed buffers fail on large `content.xml` and `META-INF/manifest.xml` in `standard/` documents.
- This blocks `extract-text`, `odt -> md`, and sometimes validation.

Do:
- Replace fixed-size buffers in ODT core and ODT adapter import path with chunked/streamed extraction.
- Introduce explicit error codes for "document too large" only when a true configured limit is hit.
- Add tests using `standard/part2-packages` and `standard/part3-schema`.

Done when:
- `validate`, `extract-text`, and `convert --from odt --to md` succeed on all 4 standard ODT files.

## 2. Scale ZIP writer/repack limits for real-world ODT packages

Why:
- Repack currently fails on high-entry or large-entry archives (standard docs exceed current writer limits).

Do:
- Remove/raise `ZIP_WRITER_MAX_ENTRIES` bottleneck.
- Support repack of entries larger than the current per-entry staging buffer.
- Preserve method/metadata correctness and keep output ODT-valid.

Done when:
- `odt_cli repack` succeeds for all standard ODT files.

## 3. Build a style-aware ODT -> doc model importer (not plain-text fallback)

Why:
- Current semantic import is intentionally narrow (`text:p`, `text:h`, `text:list`), so complex docs lose structure.
- For standard-like documents, this destroys layout intent and rich structure.

Do:
- Parse and map at least: tables, captions, code/preformatted blocks, footnotes/endnotes, links/bookmarks, images/frames, section boundaries.
- Parse style references (`text:style-name`, automatic styles) and carry them into the internal model.
- Keep unknown constructs as explicit placeholders instead of silently flattening where possible.

Done when:
- Converting standard ODT files to MD yields structured output (tables/notes/sections retained, not collapsed into plain text).

## 4. Extend doc model + Markdown adapter for style/layout fidelity

Why:
- Roundtrip quality is limited by model expressiveness and Markdown adapter constraints.
- "Best possible" layout/style preservation requires a richer intermediate model and deterministic Markdown encoding rules.

Do:
- Add model support for: table model, block attributes/classes, captioned figures, admonition-like blocks, inline spans with style metadata.
- Define canonical Markdown extensions (fenced attributes, table syntax policy, footnote format, block directives).
- Ensure stable export/import rules so repeated roundtrips do not drift.

Done when:
- ODT -> MD -> ODT keeps document structure and key style semantics with minimal loss on standard docs.

## 5. Introduce golden corpus + fidelity scoring for conversion quality

Why:
- Without measurable quality gates, regressions are hard to detect and improvements are hard to evaluate.

Do:
- Add a corpus based on `standard/` documents plus representative smaller fixtures.
- Add automated checks for:
  - command success matrix (`validate`, `extract-text`, `convert`, `repack`)
  - structural fidelity (headings, lists, tables, links, notes counts)
  - stability (roundtrip drift thresholds)
- Emit conversion diagnostics summary in tests.

Done when:
- CI reports objective quality metrics and blocks regressions in conversion fidelity.

---

## Suggested execution order

1. Size/streaming fixes (Item 1)
2. Repack scalability (Item 2)
3. Style-aware semantic importer (Item 3)
4. Model + Markdown fidelity extensions (Item 4)
5. Golden corpus and scoring in CI (Item 5)
