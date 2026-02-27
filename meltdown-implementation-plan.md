# Meltdown Implementation Plan

This plan defines how to add `*.meltdown` template-profile support to `odt_cli` and the conversion pipeline while preserving:
- normal Markdown authoring (`source.md` unchanged),
- deterministic output,
- declarative-only profile semantics.

Scope target for v0.1:
- `odt_cli convert --from md --to odt --template <profile.meltdown> in.md out.odt`
- single-pass role classification over Markdown block stream
- role -> ODT style binding
- diagnostics through existing `--diag-json` model

## 1. Architecture Decisions

1. Keep Markdown content and meltdown profile separate.
2. Implement meltdown as a profile interpreter, not a scripting engine.
3. Run recognition in one forward pass with fixed rule priority.
4. Keep backend binding separate from recognition (`layout` section).
5. Reuse existing conversion and diagnostics plumbing wherever possible.

## 2. Delivery Phases

### Phase M0: Grammar and Validation Foundation

Deliverables:
- A strict parser for profile header and core sections:
  - `profile`, `version`, `roles`, `recognize`, `layout`
- Parse diagnostics:
  - unknown keys
  - malformed predicates
  - invalid regex values

Likely files:
- `projects/convert_core/include/...` (if shared interfaces are needed)
- `projects/odt_cli/src/cli.c` (profile mode entry wiring)
- new parser module (recommended): `projects/fmt_meltdown/` or similar

Exit criteria:
- Invalid `.meltdown` files fail with deterministic diagnostics.

### Phase M1: Markdown Block Stream Adapter

Deliverables:
- Convert canonical Markdown doc model into a block stream abstraction for recognition:
  - `Heading(level)`, `Paragraph`, `Blockquote`, `List`, `Table`, `CodeBlock`
- Stable block indexing for predicate evaluation.

Likely files:
- `projects/fmt_markdown/src/fmt_markdown.c`
- shared model helper module (recommended)

Exit criteria:
- Recognition engine can iterate all blocks with deterministic metadata.

### Phase M2: Recognition Engine (Single-Pass)

Deliverables:
- Role classifier with:
  - `match` by block type
  - `where` predicates (v0.1 set)
  - priority conflict resolution
  - default role `Body`
- No backtracking/reclassification.

Likely files:
- new meltdown runtime module
- tests in existing phases

Exit criteria:
- Same input/profile always produces identical role assignments.

### Phase M3: Region Support and Positional Predicates

Deliverables:
- Region start tracking for `in Region`.
- Positional predicates:
  - `position == first|last`
  - `before Role`, `after Role`, `follows Role`, `precedes Role`

Exit criteria:
- Region- and order-sensitive profiles classify correctly in golden tests.

### Phase M4: Layout Binding to ODT Export

Deliverables:
- Role -> style mapping from `layout` section.
- Apply styles during `md -> odt` export without changing source markdown.
- Unmapped roles fallback to current style defaults.

Likely files:
- `projects/fmt_odt/src/fmt_odt.c`
- potentially `projects/fmt_markdown/src/fmt_markdown.c` for role metadata threading

Exit criteria:
- One markdown input rendered via different profiles produces visibly different style outcomes.

### Phase M5: CLI UX Integration

Deliverables:
- Extend `convert` handling so `--template <path>` accepts:
  - `.odt` template package (existing)
  - `.meltdown` profile (new)
- Explicit mode detection and clear errors for ambiguous/invalid cases.

Likely files:
- `projects/odt_cli/src/cli.c`

Exit criteria:
- Documented command works end-to-end with profile template.

### Phase M6: Diagnostics + Strictness Modes

Deliverables:
- Reuse `--diag-json` output for meltdown parse and runtime diagnostics.
- Strict/permissive handling policy for unresolved references and unknown fields.

Exit criteria:
- Machine-readable diagnostics include meltdown-origin errors/warnings.

### Phase M7: Test and Corpus Coverage

Deliverables:
- Extend existing tests (no new phase files required):
  - `phase6`: CLI success/failure matrix for `.meltdown`
  - `phase10`: semantic fixture assertions for role mapping
  - `phase11`: corpus stability gates with profile-driven variation checks

Exit criteria:
- `make test` passes with deterministic profile behavior and no regression.

### Phase M8: Documentation and Example Profiles

Deliverables:
- Keep `meltdown.md` as the normative language spec.
- Add canonical example profiles:
  - `eu-oj.meltdown`
  - `science-journal.meltdown`
  - `github-letter.meltdown`
- README usage examples.

Exit criteria:
- New users can run a profile-based conversion from docs only.

### Phase M9: `present` Grammar and Validation (v0.2)

Deliverables:
- Extend parser to recognize `present` subtree with validated keys:
  - `page`, `fonts`, `master`, `styles`
- Add deterministic diagnostics for unknown/invalid presentation keys.

Exit criteria:
- Profiles with `present` validate deterministically in permissive and strict modes.

### Phase M10: Presentation Runtime Model

Deliverables:
- Parse `present` into an internal normalized model.
- Keep non-procedural declarative semantics.

Exit criteria:
- Runtime has a stable in-memory presentation model available to exporters.

### Phase M11: ODT Materialization of `present`

Deliverables:
- Map `present.page` and `present.master` to ODT page/master structures.
- Map `present.styles` and `present.fonts` to style output and defaults.
- Preserve existing `layout` role mapping behavior.

Exit criteria:
- Visible page geometry/typography changes occur from `present` alone.

### Phase M12: Presentation Regression Gates

Deliverables:
- Extend tests for presentation behavior:
  - style name presence and overrides,
  - page geometry markers,
  - header/footer serialization checks.

Exit criteria:
- `make test` enforces presentation invariants and blocks regressions.

### Phase M13: Presentation Cookbook and Migration Notes

Deliverables:
- Add recipes for legal/science/letter profiles using `present`.
- Document v0.1 compatibility behavior (ignored vs interpreted).

Exit criteria:
- Users can author practical publication-like profiles with `present` examples only.

## 3. Risks and Mitigations

1. Risk: Language drifts into procedural behavior.
- Mitigation: Reject non-declarative constructs in parser by design.

2. Risk: Predicate complexity causes non-determinism.
- Mitigation: single-pass + fixed priority + no reclassification.

3. Risk: Role metadata threading causes adapter churn.
- Mitigation: introduce minimal shared representation and phased rollout.

4. Risk: Backward compatibility with existing `--template` ODT behavior.
- Mitigation: explicit extension-based routing and tests for both paths.

## 4. Suggested Execution Order

1. M0 grammar/parser
2. M1 block stream adapter
3. M2 recognition engine
4. M4 layout binding (basic)
5. M5 CLI integration
6. M6 diagnostics strictness
7. M3 region/positional expansion
8. M7 test hardening
9. M8 docs/examples
10. M9 present grammar
11. M10 present runtime model
12. M11 odt materialization
13. M12 presentation regression gates
14. M13 presentation cookbook

## 5. Implementation Status Tracker

| ID | Work Item | Status | Priority | Owner | Start Date | Target Date | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M0 | Parser for profile/version/roles/recognize/layout | Done | High | Copilot | 2026-02-26 | 2026-02-26 | Implemented in `odt_cli` convert path with deterministic schema/predicate validation and explicit parser-only runtime status |
| M1 | Markdown block stream adapter | Done | High | Copilot | 2026-02-27 | 2026-02-27 | Implemented deterministic top-level block stream extraction (`Heading/Paragraph/Blockquote/List/Table/CodeBlock`) in `.meltdown` template path before M2 runtime |
| M2 | Single-pass recognition engine + priority resolution | Done | High | Copilot | 2026-02-27 | 2026-02-27 | Implemented priority-based single-pass classifier over M1 block stream with deterministic final assignment and default Body behavior for non-matching rules |
| M3 | Regions + positional predicates | Done | Medium | Copilot | 2026-02-27 | 2026-02-27 | Added `regions.start` parsing and runtime support for `in`, `before`, `after`, `follows`, `precedes` under single-pass constraints |
| M4 | Role->ODT style mapping in exporter | Done | High | Copilot | 2026-02-27 | 2026-02-27 | Classifier role assignments now apply `layout` style mappings to canonical blocks prior to ODT export, with fallback to existing default styles when unmapped |
| M5 | CLI integration for `.meltdown` in `--template` | Done | High | Copilot | 2026-02-27 | 2026-02-27 | `convert --template <profile.meltdown>` now runs end-to-end for `md -> odt` while preserving existing `.odt` template behavior and compatibility |
| M6 | Diagnostics integration (`--diag-json`) | Done | High | Copilot | 2026-02-27 | 2026-02-27 | Added meltdown parse/schema/runtime diagnostics to `--diag-json` and strict-policy failure handling for unsupported predicates (`--strict`) |
| M7 | Test coverage in existing phases (6/10/11) | Done | High | Copilot | 2026-02-27 | 2026-02-27 | Extended phase6 strict+diag-json meltdown CLI checks, phase10 role-mapping assertion, and phase11 profile-driven variation checks |
| M8 | Docs + example profiles | Done | Medium | Copilot | 2026-02-27 | 2026-02-27 | Added canonical runnable profiles under `template/meltdown/` and updated README with profile-based CLI usage, strict mode notes, and examples |
| M9 | `present` grammar and validation | Done | High | Copilot | 2026-02-27 | 2026-02-27 | Parser now validates `present.page/fonts/master/styles` structure and keys with deterministic errors; phase6 covers invalid `present` subsection rejection |
| M10 | `present` runtime model | Done | High | Copilot | 2026-02-27 | 2026-02-27 | Added normalized in-memory `present` model (page/fonts/master/styles) in meltdown runtime parser and surfaced parse-time model availability diagnostics |
| M11 | ODT materialization for `present` | Done | High | Copilot | 2026-02-27 | 2026-02-27 | Materialized `present` into generated ODT `styles.xml` (style injection and page orientation/margins patching or fallback page-layout injection) while preserving role mapping |
| M12 | Presentation regression gates | Done | Medium | Copilot | 2026-02-27 | 2026-02-27 | Added deterministic phase10/phase11 assertions for present-driven style serialization and page-orientation markers |
| M13 | Presentation cookbook + migration docs | Done | Medium | Copilot | 2026-02-27 | 2026-02-27 | Added practical legal/science/letter `present` recipes and explicit v0.1->v0.2 compatibility/migration guidance in `meltdown.md`; linked from README |

## 6. Done Definition (v0.1)

v0.1 is complete when:
- `.meltdown` profiles are parsed and validated with deterministic diagnostics.
- role assignment is deterministic and single-pass.
- role->style mapping is applied in `md -> odt` conversion.
- `odt_cli convert --template <profile.meltdown>` works end-to-end.
- existing `.odt` template mode remains compatible.
- all existing tests pass with added meltdown coverage.
