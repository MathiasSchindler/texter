# Template-Aware ODT Conversion Plan

## Intention

Enable `odt_cli` to convert Markdown to ODT with an optional layout template so drafting can stay in plain Markdown while output inherits a reusable publication style (for example, Official Journal-like legal layout).

Primary goal:
- Add `--template <template.odt>` support to `odt_cli convert --from md --to odt ...`.

Expected outcome:
- Users keep a single Markdown source.
- Output ODT reuses template-defined styles and document settings.
- Workflow becomes reusable across similar legal/policy documents.

## Context

This project already has:
- A conversion pipeline (`convert_core`) with format adapters (`fmt_markdown`, `fmt_odt`).
- A CLI command for conversion (`odt_cli convert --from <fmt> --to <fmt> <in> <out>`).
- ODT package read/write support via internal ZIP/XML/ODT modules.

Use case context for this plan:
- Draft legal text in Markdown using a text editor.
- Produce publication-quality output with a fixed institutional layout.
- Current PDF prototype (examples) proves the target visual style.
- Desired next step is to reproduce this through ODT templating to avoid bespoke one-off scripts.

## Plan

1. Introduce optional template input at CLI level.
2. Pass template bytes into the ODT export adapter state.
3. Build ODT output by combining generated `content.xml` with reusable template package entries.
4. Keep conversion behavior unchanged when no template is provided.
5. Add tests and documentation for template mode.

Design principles:
- Backward compatibility first.
- Deterministic output.
- Reuse existing parser/writer building blocks.
- Fail clearly when template is invalid or incompatible.

## Implementation Steps

### Step 1: CLI interface extension

- Extend `odt_cli convert` argument parser to accept optional:
  - `--template <path.odt>`
- Validate combinations:
  - Template flag allowed only when target format is `odt` (first implementation).
- Read template file into memory using existing CLI file helpers.

Files likely touched:
- `projects/odt_cli/src/cli.c`

### Step 2: Adapter API and state wiring

- Extend `fmt_odt_state` with template fields:
  - `const u8* template_data`
  - `usize template_len`
  - `int use_template`
- Add setter/helper API in `fmt_odt`:
  - `fmt_odt_set_template(...)`
  - `fmt_odt_clear_template(...)`
- Ensure state reset between calls.

Files likely touched:
- `projects/fmt_odt/include/fmt_odt/fmt_odt.h`
- `projects/fmt_odt/src/fmt_odt.c`

### Step 3: Template-aware ODT package assembly (MVP)

- Continue generating `content.xml` from Markdown/doc model as today.
- If template mode is enabled:
  - Open template archive.
  - Reuse selected entries such as `styles.xml`, `meta.xml`, `settings.xml`.
  - Preserve required package invariants (`mimetype` first, stored).
  - Write generated `content.xml` into final package.
- If template mode is disabled:
  - Keep existing non-template export flow.

Notes:
- MVP should avoid complex XML AST merging.
- Prefer package-level reuse first; evolve to deeper merge later if needed.

### Step 4: Style binding strategy for Markdown authors

- Use existing `style_id` support where possible.
- Define and document stable Markdown conventions for style hints (for example paragraph/class markers).
- Map style hints to ODT `text:style-name` attributes during export.

Files likely touched:
- `projects/fmt_markdown/src/fmt_markdown.c` (if parser hints need extension)
- `projects/fmt_odt/src/fmt_odt.c`
- `README.md`

### Step 5: Validation and regression tests

- Add tests for:
  - md -> odt conversion with `--template`
  - output validates as ODT package
  - expected template style references appear in output `content.xml`
- Keep existing tests green without template input.

Files likely touched:
- `projects/test/src/phase6.c`
- `projects/test/src/phase11.c` (optional for larger corpus)
- `examples/` fixture template file(s)

### Step 6: Documentation and usage examples

- Document CLI syntax and behavior.
- Add one minimal template workflow example.
- List limitations for first release.

Files likely touched:
- `README.md`

## Status Tracker

| ID | Work Item | Owner | Status | Priority | Start Date | Target Date | Notes |
|---|---|---|---|---|---|---|---|
| T1 | Define CLI `--template` contract | Copilot | Done | High | 2026-02-26 | 2026-02-26 | Added parser and validation for template mode in `convert` |
| T2 | Add template fields/API in `fmt_odt` | Copilot | Done | High | 2026-02-26 | 2026-02-26 | Added template state fields plus `fmt_odt_set_template`/`fmt_odt_clear_template` |
| T3 | Implement template package reuse in exporter | Copilot | Done | High | 2026-02-26 | 2026-02-26 | Output now merges template ZIP entries while replacing `content.xml` and enforcing canonical `mimetype` |
| T4 | Preserve ODT package invariants in output | Copilot | Done | High | 2026-02-26 | 2026-02-26 | Templated export now regenerates `META-INF/manifest.xml` from final entries and preserves canonical `mimetype` first/stored |
| T5 | Add style hint mapping policy | Copilot | Done | Medium | 2026-02-26 | 2026-02-26 | ODT export now honors block/inline `style_id` overrides with existing defaults as fallback; tests verify style attributes in `content.xml` |
| T6 | Add integration tests for template mode | Copilot | Done | High | 2026-02-26 | 2026-02-26 | Added template success checks plus failure cases for invalid target format, missing template path, and invalid template file |
| T7 | Update README with template examples | TBD | Not Started | Medium | - | - | Include legal-draft workflow |
| T8 | Add sample template fixture and smoke test | TBD | Not Started | Medium | - | - | Small, redistributable fixture only |

Additional completed related work:

- `odt_cli convert` supports `--diag-json` for machine-readable diagnostics.

Status legend:
- `Not Started`
- `In Progress`
- `Blocked`
- `Done`

## Risks and Mitigations

- Risk: Template ODT may contain unsupported package variants.
  - Mitigation: Strict input validation + clear diagnostics.
- Risk: Style names in Markdown do not match template styles.
  - Mitigation: Document naming conventions and provide fallback behavior.
- Risk: Reusing template entries accidentally drops required output files.
  - Mitigation: Centralize final package manifest/check logic and add tests.

## Exit Criteria

This feature is complete when:
- `odt_cli convert --from md --to odt --template <template.odt> <in.md> <out.odt>` works reliably.
- Generated ODT validates with existing package validation.
- Layout-sensitive entries from template are reflected in output.
- Existing non-template conversion behavior remains unchanged.
- Tests and docs are updated.