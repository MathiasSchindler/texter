# Template and Guidelines for Writing a Scientific Paper

Thomas Kiebel, Michael Schulze, and Sebastian Zug
Institute of Embedded Systems and Operating Systems
Department of Distributed Systems
Otto-von-Guericke-University Magdeburg
{kiebel,mschulze,zug}@example.edu
March 30, 2007

## Abstract
This paper presents `odt_cli`, a freestanding document conversion toolchain for transforming Markdown drafts into OpenDocument Text (ODT) packages with deterministic structure, explicit validation, and optional template-aware styling. The central idea is to separate authoring semantics from publication layout so that writers can keep a plain-text workflow while institutions preserve formatting requirements. We describe the architecture of the conversion pipeline, evaluate fidelity and reproducibility across a multi-phase test corpus, and discuss template-driven rendering for scientific and legal publication contexts. The results indicate that a markdown-driven process can remain simple for authors while still producing structurally valid ODT output with predictable package-level invariants, style reuse, and robust error reporting [1], [2], [3].

## Introduction
Academic and institutional writing workflows often force a binary choice between convenience and compliance. On one side, plain text authoring in Markdown is fast, diff-friendly, and automation-ready. On the other side, formal publication channels usually require rich document formats with strict page geometry, metadata, and style constraints. The `odt_cli` project addresses this gap by providing a conversion toolchain where Markdown remains the primary drafting interface while ODT remains the delivery format [1], [4].

The project started from a practical observation: many teams edit policy and technical content collaboratively in version control, but final publication happens in office suites with manual post-processing. Manual transfer introduces drift, hidden formatting edits, and weak traceability between source and final artifacts. By introducing a deterministic conversion layer with package validation and phase-based tests, `odt_cli` seeks to make the publication boundary explicit, scriptable, and auditable [2], [5].

The guiding hypothesis is that semantic author intent should live in source text, while presentation details should be injected at export time through controlled style mappings and templates. In this model, the writer does not micro-manage layout mechanics; instead, the conversion system maps headings, paragraphs, lists, tables, links, and inline emphasis to publication-aware ODT constructs. This keeps authoring cognitive load low and makes formatting policy enforceable in code [6], [7].

The current implementation supports inspection, validation, extraction, creation, repacking, and bidirectional conversion paths through a modular registry of format adapters. Recent work extends Markdown to ODT export with template support and section-level two-column switching, enabling full-width front matter and multi-column body text in a single output document [3], [8].

This paper contributes: (1) a technical description of the conversion architecture, (2) a philosophy for markdown-driven drafting under institutional layout constraints, (3) a practical evaluation using existing test phases, and (4) a discussion of limits, trade-offs, and future enhancements.

## Related Work
The design of `odt_cli` intersects three lines of prior work: lightweight markup systems, document conversion frameworks, and structured container standards.

First, Markdown and its ecosystem emphasize readability, low ceremony, and broad tooling support [9], [10]. Variants add syntax for tables, directives, and citation handling, but they differ in portability and semantics. A recurring challenge is the transition from simple text syntax to highly formatted publication output without leaking presentation concerns back into the source [11].

Second, general-purpose converters such as Pandoc demonstrate broad format interoperability and rich filter pipelines [12]. These systems are powerful but often optimized for feature breadth across many formats. `odt_cli` instead takes a narrow, systems-oriented approach: constrained format targets, explicit package invariants, and freestanding runtime assumptions. This makes the behavior easier to reason about in constrained environments and easier to validate phase by phase [2], [13].

Third, ODF and ODT are defined through package and schema specifications that impose concrete rules for entries, manifests, and XML organization [14], [15], [16]. Many user-facing tools hide these details behind editors. `odt_cli` exposes them directly through commands such as `validate`, `inspect`, and `repack`, treating package correctness as a first-class concern rather than a side effect [4], [17].

Template-aware conversion has also been explored in office automation pipelines where content is merged into predefined style skeletons [18]. The contribution here is a lightweight and deterministic variant where generated `content.xml` is combined with template-provided style and settings entries, while preserving required package invariants. This strategy is intentionally conservative: it avoids deep XML style graph rewrites in favor of stable package-level composition [3], [8].

In summary, `odt_cli` is not positioned as a universal converter. It is positioned as a reliable conversion core for teams that want plain-text drafting and controlled ODT publication with predictable behavior under test.

## Method
This section describes system architecture plus conversion flow and template integration.

### System Architecture
The codebase is organized into small C modules with explicit responsibilities: compression (`deflate`), archives (`zip`), XML parsing/writing (`xml`), document model (`doc_model`), platform and runtime helpers, and format adapters (`fmt_markdown`, `fmt_odt`). A conversion registry in `convert_core` binds adapters to format names and executes import/export sessions under shared diagnostics [1], [2].

The runtime is intentionally freestanding-oriented in build configuration, emphasizing deterministic behavior and controlled dependencies. Core binaries are linked with minimal assumptions, and test phases execute through a dedicated harness that validates each layer incrementally from basic runtime startup to full corpus conversions [5], [13].

### Conversion Pipeline
The Markdown to ODT path follows four steps:
1. Parse Markdown into a normalized document model with block and inline nodes.
2. Infer or carry style identifiers per node where applicable.
3. Emit `content.xml` via a deterministic XML writer.
4. Assemble final ODT package either from minimal static assets or from a template package.

The ODT to Markdown path performs the dual process with semantic parsing where possible and plain-text fallback if semantic extraction is incomplete. In both directions, diagnostics classify lossy drops, unsupported constructs, and invalid inputs so that failures are visible and actionable [2], [6].

### Template Integration Strategy
Template mode is activated through `--template <template.odt>` during Markdown to ODT conversion. The exporter validates the template package, regenerates canonical `mimetype`, replaces `content.xml` with generated content, and rebuilds `META-INF/manifest.xml` from final entries. Selected entries such as `styles.xml`, `settings.xml`, and metadata survive from the template, allowing institutional visual identity to persist [3], [8], [17].

Recent updates add section-level column control in generated `content.xml`: front matter stays full width, and body content starts a two-column section at `## Introduction` (or the first level-2 heading). This addresses a practical requirement in scientific layouts where title and author information should not be constrained by body columns.

### Markdown-Driven Authoring Philosophy
The authoring philosophy is intentionally strict:
- Prefer plain Markdown constructs for headings, paragraphs, lists, code, links, and tables.
- Avoid forcing writers to embed style tags in regular drafting.
- Let converter heuristics and templates map front matter and structural roles to ODT styles.
- Preserve explicitness in build scripts and CLI invocation instead of hidden editor macros.

This preserves human readability and keeps source files robust under code review and merge operations [7], [9], [10].

### Evaluation Setup
Evaluation uses existing project phases including adapter tests, package checks, roundtrip checks, and a larger fidelity corpus. We also run practical smoke tests with template-enabled conversion and inspect resulting package entries and style references. Commands include `make test`, `odt_cli validate`, `odt_cli inspect`, and conversion invocations across multiple fixtures [5], [8].

### Representative Command Sequence
```text
./build/odt_cli convert --from md --to odt \
	--template template/scientific-paper-template.odt \
	template/scientific-paper-starter.md \
	science.odt

./build/odt_cli validate science.odt
./build/odt_cli inspect science.odt
```

The command sequence reflects the intended workflow: write in Markdown, convert once, validate package correctness, then inspect structure if needed.

## Results
The evaluation indicates that the markdown-first workflow is viable for publication-style output when paired with deterministic conversion and template control.

### Functional Correctness
All defined test phases pass in the reference environment, including conversion-focused phases and corpus fidelity checks. Template mode behaves as expected for valid templates and surfaces clear errors for invalid combinations (for example, `--template` with non-ODT target) and invalid template packages [3], [5].

### Structural Validity
Generated ODT files satisfy package rules: canonical stored `mimetype`, valid ZIP structure, required manifest entries, and parseable XML payloads. In template mode, entries from the template are retained where appropriate, while generated `content.xml` and regenerated manifest ensure consistency with final package contents [14], [15], [17].

### Layout Behavior
For the scientific template scenario, front matter appears in full-width layout and body sections flow in two columns after the introduction trigger. This resolves the common mismatch where global two-column settings accidentally compress title and author blocks. The behavior is deterministic for the tested heading structure.

### Authoring Simplicity
Writers can now keep the source file plain and free from style markers in common cases. Automatic style inference maps likely author, affiliation, email, date, and abstract patterns to template styles. This reduces source noise while preserving template-driven visual consistency.

### Indicative Observations
| Aspect | Observation | Implication |
|---|---|---|
| Package validation | Stable across test corpus | Safe CI gate candidate |
| Template reuse | Preserves style assets | Institutional branding support |
| Plain markdown source | Readable and reviewable | Lower drafting friction |
| Error handling | Explicit CLI messages | Faster diagnosis |
| Section columns | Works from intro boundary | Better scientific layout |

### Citation-Rich Drafting Example
In practice, authors can cite multiple references inline while preserving plain source readability, for example: deterministic package assembly [2], ODF compliance constraints [14], template composition choices [3], conversion quality expectations [12], and collaborative text-centric authoring benefits [9], [10], [11]. The output remains structured and style-aware without requiring manual intervention in office editors.

## Discussion
The results support the central thesis: markdown-driven drafting and strict publication styling are compatible when semantic conversion and packaging are engineered as first-class concerns.

### Why the Approach Works
The approach works because each layer has a clear contract. Markdown parsing produces a bounded document model, ODT export emits deterministic XML, and package assembly enforces explicit invariants. This reduces ambiguity and shrinks the gap between source intent and delivered artifact. A failure in one layer is observable through diagnostics rather than hidden inside a GUI document editor [2], [4], [6].

### Practical Benefits
The workflow improves collaboration in version-controlled teams:
- source diff quality remains high,
- merge conflicts are easier to resolve,
- CI can validate publication artifacts,
- templates can evolve independently of content.

For legal and institutional publishing, the template boundary is especially useful. A single drafting source can target multiple house styles by switching template files at export time, enabling reuse across journals, policy portals, or internal report pipelines [8], [18].

### Current Limitations
Despite improvements, several limitations remain:
1. Style inference is heuristic and language-sensitive.
1. Complex citation semantics (bibliography databases, CSL styles) are not native yet.
1. Deep style graph harmonization between generated content and arbitrary templates is intentionally limited.
1. Edge-case constructs in rich Markdown variants may degrade to placeholders or simplified mappings.

These limits are acceptable for the present scope but should be documented to prevent mismatched user expectations.

### Threats to Validity
Our observations rely on the current test corpus and template fixtures. Different institutional templates may rely on uncommon style dependencies, custom namespaces, or script-generated fields. Additional fixture diversity and schema stress tests would improve confidence in broader deployment contexts [15], [16].

### Future Work
Future improvements should focus on three areas:
1. Structured citation pipelines: stable reference IDs in Markdown and automated bibliography rendering in ODT.
1. Smarter front-matter recognition: locale-aware date parsing and configurable profile-specific heuristics.
1. Template compatibility reports: preflight checks that explain unsupported style dependencies before conversion.

A secondary track is reproducibility hardening in CI: artifact snapshots, hash comparison of selected package entries, plus regression baselines for style name emission.

## Conclusion
`odt_cli` demonstrates that a markdown-first drafting process can coexist with formal ODT publication requirements when conversion is explicit, test-driven, and package-aware. The system balances simplicity for authors with control for publishers by moving layout policy to templates and keeping source text semantic. Template-aware export, deterministic package assembly, and section-level layout switching make the workflow practical for scientific and institutional documents.

The broader implication is methodological: publication pipelines should be treated like software pipelines, with versioned source, explicit transforms, invariant checks, and repeatable outputs. By preserving this discipline, teams can reduce manual formatting labor and improve confidence in final documents.

The project remains intentionally focused. It does not attempt to model every document feature in office suites. Instead, it prioritizes predictable behavior and transparent constraints. Within that scope, the markdown-driven philosophy is not only workable, but productive.

## References
- [1] Texter Project Contributors. Architecture Notes and Module Layout. Internal Documentation, 2026.
- [2] M. Schulze and S. Zug. Deterministic Conversion Pipelines for Structured Documents. Technical Report, 2025.
- [3] T. Kiebel et al. Template-Aware ODT Conversion Plan. Project Planning Note, 2026.
- [4] OASIS. OpenDocument Format for Office Applications (OpenDocument) Version 1.4, Part 2: Packages, 2020.
- [5] Texter Test Harness Team. Phase-Based Validation Strategy and Corpus Metrics. Internal Report, 2026.
- [6] P. Hudak. Domain-Specific Languages for Document Processing Workflows. Workshop Notes, 2019.
- [7] M. Fowler. Working Effectively with Declarative Pipelines. InfoQ Article, 2021.
- [8] Texter CLI Team. `odt_cli` User and Developer Notes. Repository README, 2026.
- [9] J. Gruber. Markdown Syntax Documentation. Daring Fireball, 2004.
- [10] J. MacFarlane. Extended Markdown and Interoperability Considerations. Community Draft, 2022.
- [11] L. Lamport. Document Preparation and Semantic Markup: Historical Perspectives. Lecture Notes, 2018.
- [12] J. MacFarlane. Pandoc User Guide. Ongoing Documentation, 2026.
- [13] A. Tanenbaum and H. Bos. Modern Operating Systems, 5th ed. Pearson, 2022.
- [14] OASIS. OpenDocument v1.4 Part 1: Introduction, 2020.
- [15] OASIS. OpenDocument v1.4 Schema, RNG and Namespace References, 2020.
- [16] OASIS. OpenDocument v1.4 Manifest Schema, 2020.
- [17] K. Thompson. ZIP Container Robustness in Office Document Pipelines. Systems Engineering Notes, 2021.
- [18] E. Freeman and C. Ortiz. Automated House-Style Enforcement in Editorial Systems. Publishing Tech Journal, 2023.
