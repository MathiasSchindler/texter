# ODT Reader/Writer in C - Architecture and Scope

## 1. Objectives

- Build an ODT read/write implementation in C with zero external runtime dependencies.
- Keep reusable technical capabilities in independent subprojects.
- Keep ODT semantics isolated from generic runtime, compression, ZIP, and XML layers.

## 2. Goals

- Platform target: Linux x86_64.
- Language target: C11.
- Link strategy: static linking.
- Text model: UTF-8.
- Dependency policy:
	- No `libc` dependency at runtime.
	- No external `zlib`/compression libraries.
	- No external XML libraries.
	- OS integration through direct syscalls via internal runtime layer.

## 3. Non-Goals

- Full ODF feature parity in the current scope.
- Digital signature generation/validation.
- Encryption support.
- Full typography/shaping behavior.

## 4. Layered Architecture

Top-down dependency order:

- `projects/odt_cli` depends on `projects/odt_core`.
- `projects/odt_core` depends on `subprojects/xml`, `subprojects/zip`, `subprojects/unicode`, `subprojects/util`.
- `subprojects/zip` depends on `subprojects/deflate`, `subprojects/crc32`, `subprojects/util`.
- `subprojects/xml` depends on `subprojects/unicode`, `subprojects/util`, `subprojects/arena`.
- `subprojects/platform` and `subprojects/rt` are foundational and do not depend on higher layers.

Architecture rule:

- Generic subprojects must not depend on ODT-specific project code.

## 5. Subprojects

### 5.1 `subprojects/rt`

Purpose:

- Minimal runtime layer replacing required libc-like functionality.

Responsibilities:

- Process entry and bootstrap (`_start`).
- Syscall wrappers (`openat`, `read`, `write`, `close`, `exit`, etc.).
- Core memory/string helpers.
- Numeric formatting helpers.

### 5.2 `subprojects/platform`

Purpose:

- Thin OS-oriented abstraction for file and console operations.

Responsibilities:

- Basic file probing and output helpers.
- Shared place for portability-related platform behavior.

### 5.3 `subprojects/util`

Purpose:

- Shared low-level utility primitives.

Responsibilities:

- Numeric/bounds helper routines.
- String view and foundational utility operations.

### 5.4 `subprojects/arena`

Purpose:

- Deterministic allocation strategy for parser-style workloads.

Responsibilities:

- Arena initialization, aligned allocation, and reset semantics.

### 5.5 `subprojects/unicode`

Purpose:

- Minimal Unicode handling required by XML/ODT stack.

Responsibilities:

- UTF-8 validation.
- UTF-8 codepoint decode/encode.
- XML-relevant codepoint classification helpers.

### 5.6 `subprojects/crc32`

Purpose:

- ZIP checksum support.

Responsibilities:

- Streaming and one-shot CRC32 APIs.

### 5.7 `subprojects/deflate`

Purpose:

- Raw DEFLATE compression/decompression.

Responsibilities:

- Inflate support for STORED, fixed-Huffman, and dynamic-Huffman streams.
- Compress support with:
	- `STORE_ONLY` mode.
	- `BEST` mode selecting best available built-in strategy.

### 5.8 `subprojects/zip`

Purpose:

- ZIP container reading/writing used by ODT packaging.

Responsibilities:

- EOCD/central-directory/local-header parsing.
- Entry lookup and extraction.
- Entry writing with STORE and DEFLATE methods.
- Zip-slip path safety checks.

### 5.9 `subprojects/xml`

Purpose:

- Streaming XML parser/writer layer.

Responsibilities:

- Tokenization (`start`, `end`, `text`, `eof`).
- Namespace resolution and QName handling.
- Entity decoding/encoding.
- Deterministic escaping/serialization.

## 6. ODT-Specific Projects

### 6.1 `projects/odt_core`

Purpose:

- ODT domain logic built on generic subprojects.

Responsibilities:

- ODT package rule validation (`mimetype` first, stored).
- Manifest validation (`META-INF/manifest.xml`).
- Minimal text extraction from `content.xml`.
- Minimal ODT construction from plain text.

### 6.2 `projects/odt_cli`

Purpose:

- User-facing CLI for diagnostics and conversion tasks.

Current command set:

- `validate <in.odt>`
- `inspect <in.odt>`
- `extract-text <in.odt> [out.txt]`
- `create <in.txt> <out.odt>`
- `repack <in.odt> <out.odt>`

Note:

- `pack` remains part of broader desired command surface but is not part of the current implemented command set.

### 6.3 `projects/test`

Purpose:

- Consolidated integration and regression test runner for all implemented phases/components.

Responsibilities:

- Execute runtime, util/arena/crc, deflate, zip, xml, odt_core, odt_cli, and unicode checks in one binary.

## 7. Repository Layout

- `subprojects/rt/`
- `subprojects/platform/`
- `subprojects/util/`
- `subprojects/arena/`
- `subprojects/unicode/`
- `subprojects/crc32/`
- `subprojects/deflate/`
- `subprojects/zip/`
- `subprojects/xml/`
- `projects/odt_core/`
- `projects/odt_cli/`
- `projects/test/`

## 8. Build and Test Entry Points

- `make clean`
- `make`
- `make test`

`make` builds the consolidated test binary and `odt_cli`.

`make test` runs the consolidated test runner.

## 9. Future-Proof Conversion Strategy

To support `.md <-> .odt` now and additional formats later without repeated rewrites, conversion must be model-based, not format-to-format.

Core rule:

- Never implement direct pairs like `md -> odt`, `odt -> html`, `docx -> mediawiki` separately.
- Every format imports to and exports from one shared canonical document model.

Canonical conversion pipeline:

| Stage | Responsibility | Example (`.md -> .odt`) |
|---|---|---|
| Parse | Read format bytes into format-specific AST/package model | Markdown parser reads headings/lists/inline tokens |
| Normalize In | Map format-specific model into canonical document model | Markdown nodes map to canonical blocks/inlines |
| Transform | Apply shared transforms and policy checks | Normalize spacing, list nesting, style semantics |
| Normalize Out | Map canonical model into target format-specific model | Canonical nodes map to ODT XML/package structures |
| Emit | Serialize target format bytes | ODT package writer outputs `.odt` |

## 10. Canonical Document Model Requirements

The canonical model should represent document semantics, not one format's syntax.

Required model areas:

- Metadata: title, author, language, timestamps.
- Blocks: paragraph, heading, list, list item, quote, code block, table.
- Inlines: text, emphasis, strong, code span, link, image, line break.
- Styles: logical style identifiers separate from renderer-specific details.
- Assets: references to embedded/external media.
- Extension payloads: unknown feature buckets for forward compatibility.

Conversion policy modes:

| Mode | Behavior |
|---|---|
| `strict` | Fail when unsupported constructs are encountered |
| `lossy` | Continue conversion and report warnings |
| `roundtrip-safe` | Preserve unknown constructs in extension payloads when possible |

## 11. Proposed Project Additions

| Project | Purpose |
|---|---|
| `subprojects/doc_model` | Canonical document model types and helpers |
| `projects/convert_core` | Conversion orchestration pipeline and diagnostics |
| `projects/fmt_markdown` | Markdown parser/importer and emitter/exporter |
| `projects/fmt_odt` | ODT importer/exporter built on `odt_core` + XML/ZIP layers |
| `projects/fmt_docx` | Future DOCX adapter |
| `projects/fmt_html` | Future HTML adapter |
| `projects/fmt_mediawiki` | Future MediaWiki adapter |

## 12. Milestone Implementation Plan

Primary near-term goal:

- Implement `.md` import/export first while establishing conversion architecture that can absorb new formats later.

Milestones:

| Milestone | Scope | Deliverables | Status |
|---|---|---|---|
| M1 - Canonical Model | Add format-neutral document model foundation | `subprojects/doc_model` types for metadata/blocks/inlines/styles/assets + validation helpers | `Completed` |
| M2 - Conversion Core | Add format-agnostic conversion pipeline | `projects/convert_core` with import/export interfaces, diagnostics, policy modes (`strict`, `lossy`, `roundtrip-safe`) | `In Progress` |
| M3 - Markdown Adapter MVP | Implement first full adapter pair | `projects/fmt_markdown` importer/exporter for headings, paragraphs, lists, emphasis, strong, code span, links | `In Progress` |
| M4 - ODT Adapter Bridge | Connect canonical model to ODT structures | `projects/fmt_odt` importer/exporter using `odt_core` for package handling and XML mapping for content semantics | `Not Started` |
| M5 - CLI Convert Command | Expose user-facing conversion flow | `odt_cli convert --from <fmt> --to <fmt> <in> <out>` with `.md <-> .odt` first | `Implemented (MVP)` |
| M6 - Fidelity and Roundtrip | Improve preservation behavior and reporting | Loss reports, diagnostics for dropped features, roundtrip-safe extension payload handling | `Not Started` |
| M7 - Additional Formats | Add new adapters without core rewrites | First future adapter (`html` or `docx`), proving architecture scalability | `Not Started` |

## 13. Status Tracker

Use this table as the single source of progress for conversion work.

| Area | Target | Current Status | Notes |
|---|---|---|---|
| Canonical model | Stable format-neutral model | `Implemented (MVP)` | `subprojects/doc_model` added with validation helpers; extend for richer semantics in M2+ |
| Conversion core | Format-agnostic import/validate/export orchestration | `Implemented (Foundation+)` | `projects/convert_core` now includes handler interfaces, diagnostics, policy modes, registry lookup, and reusable conversion sessions; real format adapters land in M3/M4 |
| `.md` import | Markdown -> canonical | `Implemented (Expanded MVP)` | `fmt_markdown` supports headings (levels 1-6), paragraphs, ordered/unordered lists, emphasis, strong, code spans, links, and images |
| `.md` export | Canonical -> Markdown | `Implemented (Expanded MVP)` | `fmt_markdown` exports headings, paragraphs, lists, emphasis, strong, code spans, links, and images; unsupported nodes still emit diagnostics |
| `.odt` import | ODT -> canonical | `Implemented (Bridge MVP)` | `fmt_odt` maps extracted plain text to canonical paragraphs; semantic style mapping deferred to M4 |
| `.odt` export | Canonical -> ODT | `Implemented (Bridge MVP+)` | `fmt_odt` emits structured `content.xml` for headings/lists/paragraphs plus inline emphasis/strong/code/link semantics, references explicit heading/list styles, and packages ODT directly via XML+ZIP layers |
| CLI conversion UX | `odt_cli convert` command | `Implemented (MVP)` | Supports `--from md --to odt` and `--from odt --to md` via registry-based adapters |
| Future format readiness | Add adapters without touching core model | `Planned` | Validated by adding at least one new format adapter |
