# texter

`texter` is a C11 codebase for reading, validating, creating, and converting OpenDocument Text (`.odt`) files.

The project builds static binaries with a freestanding runtime (`-nostdlib -static`) and uses in-repo implementations for runtime, ZIP, DEFLATE, XML, and document conversion.

## Repository Layout

- `subprojects/`: low-level libraries
- `subprojects/rt`: syscall/runtime helpers and startup
- `subprojects/platform`: file I/O wrappers
- `subprojects/util`, `subprojects/arena`, `subprojects/unicode`
- `subprojects/crc32`, `subprojects/deflate`, `subprojects/zip`, `subprojects/xml`
- `subprojects/doc_model`: canonical document model
- `projects/odt_core`: ODT package validation, plain-text extraction, minimal ODT creation
- `projects/convert_core`: format registry/session/diagnostics conversion pipeline
- `projects/fmt_markdown`: Markdown adapter
- `projects/fmt_odt`: ODT adapter
- `projects/odt_cli`: command-line interface
- `projects/test`: phased test runner (`phase0` ... `phase10`)
- `standard/`: local ODF 1.4 specification and schema reference files
- `examples/`: sample input files

## Build

From repository root:

```bash
make clean
make
```

This builds:

- `build/test`
- `build/odt_cli`

## Test

```bash
make test
```

`make test` runs all test phases currently wired in `projects/test/src/main.c`.

## CLI Commands

`build/odt_cli` supports:

- `validate <in.odt>`
- `inspect <in.odt>`
- `extract-text <in.odt> [out.txt]`
- `create <in.txt> <out.odt>`
- `repack <in.odt> <out.odt>`
- `convert --from <fmt> --to <fmt> <in> <out>`

Current format adapters registered by CLI:

- `md` (Markdown)
- `odt` (OpenDocument Text)

## Current Capabilities

### ODT core

- Validates package structure and required entries
- Reads ZIP entries and extracts plain text from ODT content
- Builds a minimal valid ODT package from plain text

### Markdown -> ODT conversion

Structured export includes:

- Headings (`text:h`, heading styles)
- Paragraphs
- Bullet/ordered lists (including nested list structure)
- Inline emphasis/strong/code spans
- Links
- Code blocks
- Block quotes
- Generated `styles.xml` includes style definitions used by exported `content.xml`

### ODT -> Markdown conversion

Semantic import path reads `content.xml` and maps common structures back into the canonical model and Markdown export, including:

- Headings
- Paragraphs
- Lists
- Inline emphasis/strong/code/link
- Code blocks
- Block quotes

If semantic parsing fails for a document, importer falls back to plain-text extraction.

## Conversion Behavior and Limits

- CLI conversion currently runs with `CONVERT_POLICY_LOSSY`.
- Full round-trip equivalence is not guaranteed.
- Round-trip for common Markdown structures is covered by integration tests, but exact style/layout fidelity is not guaranteed.
- Nested list rehydration in `odt -> md` is functional for common cases but not equivalent to full Writer style semantics.
- ODT import still has fallback-to-plain-text behavior for unsupported or unexpected constructs.

## Example

```bash
./build/odt_cli convert --from md --to odt ./examples/test-markup.md ./examples/out-odt-markup.odt
./build/odt_cli convert --from odt --to md ./examples/out-odt-markup.odt ./examples/roundtrip-check.md
```

## Notes

- The codebase currently targets Linux-like environments (syscall-based runtime).
- Binary size and static-footprint work is ongoing.

## Attribution and License

- Most of the code in this repository was created with GPT-5.3-Codex assistance.
- The software is released under CC0.
