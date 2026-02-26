# Meltdown Language Specification (Draft v0.1)

Meltdown is a declarative profile language for interpreting plain Markdown documents as structured discourse and rendering them with backend-specific layouts.

Core objective:
- Keep `source.md` completely normal Markdown.
- Apply a `.meltdown` profile to infer discourse roles.
- Map inferred roles to output styles (for example ODT styles) without changing source text.

Example target workflow:

```bash
./build/odt_cli convert \
  --from md \
  --to odt \
  --template eu-oj.meltdown \
  draft-delegated-act.md \
  draft-delegated-act.odt
```

## Part A: Rationale and Design Choices

### A.1 The Problem Meltdown Solves

Markdown expresses textual and structural relationships between blocks such as headings, paragraphs, lists, and blockquotes. It does not encode discourse function.

Institutional document families (for example EU delegated acts, scientific papers, and consultation letters) require layout decisions that depend on discourse role, not only on structure.

Examples:
- In a delegated act, the first top-level heading is often a document title.
- The phrase `Having regard to` introduces legal basis.
- `Whereas:` introduces recital context.
- Paragraphs beginning with `(1)`, `(2)`, ... are recitals.
- Later headings may introduce Articles rather than titles.

Meltdown solves this by introducing declarative recognition rules that infer discourse roles from normal Markdown without source modification.

Different profiles can interpret the same Markdown differently:
- `eu-oj.meltdown`
- `science-journal.meltdown`
- `github-letter.meltdown`

### A.2 Separation of Concerns

Meltdown enforces strict boundaries:

| Layer | Responsibility |
| --- | --- |
| Markdown source | Textual content |
| Meltdown profile | Discourse role recognition |
| Renderer | Typographic realization |

Implications:
- Markdown source says what is written.
- Meltdown profile says what parts the document contains.
- Renderer says how those parts look in a backend/layout.

This enables one source to render into multiple institutional outputs without author-side markup changes.

### A.3 Declarative Constraint

Meltdown rules are declarative only.

Forbidden in v0.1:
- procedural predicates
- loops
- user-defined functions
- runtime scripting
- evaluation-order programming

Rationale:
- preserve determinism
- preserve analyzability
- keep profiles portable and diffable

### A.4 Single-Pass Requirement

Meltdown classification is single-pass over a Markdown block stream.

Rules:
- rules are evaluated by priority
- earlier assignment is final
- no backtracking/reclassification

Guarantees:
- deterministic output
- linear complexity
- reproducibility

## Part B: Language Specification

### B.1 File Extension

Meltdown profiles use:
- `*.meltdown`

### B.2 Input Model

Markdown is parsed into a block stream. Supported abstract block kinds:
- `Heading(level)`
- `Paragraph`
- `Blockquote`
- `List`
- `Table`
- `CodeBlock`

Each block receives one semantic role.

Default role if unmatched:
- `Body`

### B.3 Profile Header

Each profile begins with:

```yaml
profile: <profile-name>
version: 1
```

### B.4 Role Declaration

Roles are explicit semantic labels and contain no layout information.

```yaml
roles:
  Title:
  Subtitle:
  Institution:
  LegalBasis:
  RecitalIntro:
  Recital:
  ArticleHeading:
```

### B.5 Recognition Rules

Rules classify blocks into roles:

```yaml
recognize:

  Title:
    match:
      - heading(level=1)
    where:
      - position == first
    priority: 100

  Institution:
    match:
      - paragraph
    where:
      - uppercase == true
      - endswith ","
    priority: 80

  LegalBasis:
    match:
      - paragraph
    where:
      - startswith "Having regard to"
    priority: 70

  RecitalIntro:
    match:
      - paragraph
    where:
      - equals "Whereas:"
    priority: 60

  Recital:
    match:
      - paragraph
    where:
      - regex '^\\(\\d+\\)'
      - after RecitalIntro
    priority: 50
```

### B.6 Matching Predicates

Supported predicate groups in v0.1.

Block type:
- `heading(level=n)`
- `paragraph`
- `blockquote`
- `list`
- `table`
- `codeblock`

Text predicates:
- `startswith "..."`
- `equals "..."`
- `endswith "..."`
- `regex "..."`
- `uppercase == true|false`

Positional predicates:
- `position == first|last`
- `before Role`
- `after Role`
- `follows Role`
- `precedes Role`
- `in Region`

All predicates are side-effect free.

### B.7 Regions

Regions model phases such as recital blocks.

```yaml
regions:

  Recitals:
    start:
      - RecitalIntro
```

Example region-restricted recognition:

```yaml
Recital:
  match:
    - paragraph
  where:
    - regex '^\\(\\d+\\)'
    - in Recitals
```

### B.8 Conflict Resolution

If multiple rules match a block:
1. Higher `priority` wins.
2. Assignment is final (no re-evaluation).

### B.9 Layout Mapping

Layout mapping is defined separately from recognition:

```yaml
layout:

  Title:
    style: OJTitle

  Recital:
    style: OJRecital
```

Recognition section must not include layout instructions.

### B.10 Renderer Binding

Renderer maps role->backend style application.

For ODT backend this typically means:
- role -> `text:style-name`
- optional backend-specific style/materialization rules

## Part C: Sample Meltdown Profiles

### C.1 Official Journal Profile

```yaml
profile: eu-oj
version: 1

roles:
  Title:
  Subtitle:
  Institution:
  LegalBasis:
  RecitalIntro:
  Recital:

recognize:

  Title:
    match:
      - heading(level=1)
    where:
      - position == first
    priority: 100

  Institution:
    match:
      - paragraph
    where:
      - uppercase == true
      - endswith ","
    priority: 80

  LegalBasis:
    match:
      - paragraph
    where:
      - startswith "Having regard to"
    priority: 70

  RecitalIntro:
    match:
      - paragraph
    where:
      - equals "Whereas:"
    priority: 60

  Recital:
    match:
      - paragraph
    where:
      - regex '^\\(\\d+\\)'
      - after RecitalIntro
    priority: 50

layout:

  Title:
    style: OJTitle

  Recital:
    style: OJRecital
```

### C.2 Scientific Journal Profile

```yaml
profile: science-journal
version: 1

roles:
  Abstract:
  Section:

recognize:

  Abstract:
    match:
      - blockquote
    where:
      - position == first
    priority: 90

  Section:
    match:
      - heading(level=1)
    priority: 80

layout:

  Section:
    style: PaperSection
```

### C.3 GitHub Letter Profile

```yaml
profile: github-letter
version: 1

roles:
  Sender:
  Body:

recognize:

  Sender:
    match:
      - paragraph
    where:
      - position == first
    priority: 100
```

Result:
- `--template eu-oj.meltdown`
- `--template science-journal.meltdown`
- `--template github-letter.meltdown`

can produce different renderings from the same Markdown source.

## Part D: Quick Start and Operational Guidance

### D.1 Fastest path to first useful output

MVP implementation order:
1. Parse profile header (`profile`, `version`).
2. Parse `roles` and `recognize`.
3. Classify block stream in one pass.
4. Apply `layout` role->style mapping in ODT export.
5. Emit diagnostics via existing `--diag-json` path.

### D.2 Strict vs permissive mode

Recommended defaults:
- Unknown role in `layout`: warning.
- Unknown predicate key: error.
- Invalid regex: error.
- Unresolved role reference in positional predicate: error.

### D.3 Security profile

If a future raw backend escape is added:
- disable by default
- require explicit opt-in flag (`--allow-raw-template`)

## Part E: Annex (Typical Families)

### E.1 Legal / Official Journal

Use when document contains legal basis + recital patterns.

Recognition focus:
- `Having regard to` -> `LegalBasis`
- `Whereas:` -> `RecitalIntro`
- `^(n)` recital numbering -> `Recital`

### E.2 Scientific paper

Use when document follows abstract/sections style.

Recognition focus:
- first blockquote -> `Abstract`
- level-1 headings -> `Section`

### E.3 GitHub letter

Use when source is correspondence-like.

Recognition focus:
- first paragraph -> `Sender`
- remaining text -> `Body`

## Part F: Open Questions

1. Should extension remain only `.meltdown`, or also allow `.melt.md` alias?
2. Should `position == first` be global or role-local by default?
3. Should regions support explicit `end` rules in v0.1 or later?
4. Should renderer binding permit backend conditionals in v0.1 (`if backend == odt`)?

