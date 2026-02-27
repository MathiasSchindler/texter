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

## Part G: Presentation Extension (Draft v0.2)

This part defines a forward-compatible extension for layout-level controls while preserving the core separation of concerns:

- `recognize`: infer discourse roles.
- `layout`: map roles to named styles.
- `present`: define page, typography, and master-page presentation primitives.

### G.1 Design intent

The `present` block is declarative and non-procedural. It is intended to cover the class of choices visible in institutional publication layouts, including:

- page size and orientation,
- margins and print geometry,
- header/footer content,
- default font families,
- style-level text and paragraph properties.

### G.2 Top-level shape

```yaml
present:
  page:
    ...
  fonts:
    ...
  master:
    ...
  styles:
    ...
```

### G.3 `present.page`

```yaml
present:
  page:
    size: A4
    orientation: portrait
    margins:
      top: 20mm
      right: 20mm
      bottom: 20mm
      left: 20mm
```

Required constraints:

- units must be explicit (`mm`, `cm`, `pt`).
- orientation must be `portrait` or `landscape`.

### G.4 `present.fonts`

```yaml
present:
  fonts:
    body: "Liberation Serif"
    heading: "Liberation Serif"
    mono: "Liberation Mono"
```

These are profile-level defaults and may be overridden per-style.

### G.5 `present.master`

```yaml
present:
  master:
    first-page:
      header:
        left: "Not the Official Journal of the European Union"
        right: "EN L series"
      footer:
        left: "ELI: model draft (non-official)"
        right: "{{page.number}}/{{page.count}}"
```

Header/footer values are textual templates, not executable scripts.

### G.6 `present.styles`

```yaml
present:
  styles:
    OJTitle:
      family: paragraph
      based-on: Heading_20_1
      font-size: 18pt
      font-weight: bold
      align: center
      margin-top: 4mm
      margin-bottom: 3mm

    OJRecital:
      family: paragraph
      based-on: Text_20_body
      font-size: 11pt
      line-height: 1.25
      align: justify
      margin-left: 12mm
      text-indent: -8mm
```

Style names should match or intentionally override names referenced in `layout` mappings.

### G.7 Compatibility contract

- v0.1 engines may ignore `present` while still honoring `recognize` and `layout`.
- v0.2-capable engines interpret `present` and materialize style/page definitions in backend output.
- Unsupported `present` keys should emit diagnostics in permissive mode and fail in strict mode.

### G.8 Non-goals for v0.2 extension

- procedural logic,
- loops/functions/macros,
- arbitrary script embedding,
- backend-specific imperative commands.

### G.9 Example (integrated)

```yaml
profile: eu-oj
version: 1

roles:
  Title:
  Recital:

recognize:
  Title:
    match:
      - heading(level=1)
    where:
      - position == first
    priority: 100

layout:
  Title:
    style: OJTitle
  Recital:
    style: OJRecital

present:
  page:
    size: A4
    orientation: portrait
    margins:
      top: 20mm
      right: 20mm
      bottom: 20mm
      left: 20mm
  fonts:
    body: "Liberation Serif"
    mono: "Liberation Mono"
  styles:
    OJTitle:
      family: paragraph
      font-size: 18pt
      font-weight: bold
      align: center
```

## Part H: Presentation Cookbook (M13)

This cookbook provides practical profile recipes that combine semantic recognition with `present` controls.

### H.1 Legal / Official Journal recipe

Use this when you need:

- strong title hierarchy,
- recital indentation,
- publication-style margins.

```yaml
profile: eu-oj-cookbook
version: 1

roles:
  Title:
  RecitalIntro:
  Recital:
  ArticleHeading:

regions:
  Recitals:
    start:
      - RecitalIntro

recognize:
  Title:
    match:
      - heading(level=1)
    where:
      - position == first
    priority: 100

  RecitalIntro:
    match:
      - heading(level=2)
    where:
      - after Title
      - before ArticleHeading
    priority: 90

  Recital:
    match:
      - paragraph
    where:
      - in Recitals
    priority: 80

  ArticleHeading:
    match:
      - heading(level=2)
    where:
      - after RecitalIntro
    priority: 70

layout:
  Title:
    style: OJTitle
  Recital:
    style: OJRecital
  ArticleHeading:
    style: Heading_20_2

present:
  page:
    orientation: portrait
    margins:
      top: 20mm
      right: 20mm
      bottom: 20mm
      left: 20mm
  styles:
    OJTitle:
      family: paragraph
      based-on: Heading_20_1
      align: center
      font-size: 18pt
      font-weight: bold
    OJRecital:
      family: paragraph
      based-on: Text_20_body
      margin-left: 12mm
      text-indent: -8mm
      line-height: 1.25
```

### H.2 Scientific journal recipe

Use this when you need:

- compact page geometry,
- explicit abstract style,
- consistent section heading style.

```yaml
profile: science-journal-cookbook
version: 1

roles:
  Title:
  AbstractHeading:
  AbstractParagraph:
  SectionHeading:

regions:
  AbstractRegion:
    start:
      - AbstractHeading

recognize:
  Title:
    match:
      - heading(level=1)
    where:
      - position == first
    priority: 100

  AbstractHeading:
    match:
      - heading(level=2)
    where:
      - after Title
    priority: 90

  AbstractParagraph:
    match:
      - paragraph
    where:
      - in AbstractRegion
    priority: 80

  SectionHeading:
    match:
      - heading(level=2)
    where:
      - after AbstractHeading
    priority: 70

layout:
  Title:
    style: Heading_20_1
  AbstractParagraph:
    style: AbstractParagraph
  SectionHeading:
    style: Heading_20_2

present:
  page:
    orientation: portrait
    margins:
      top: 18mm
      right: 18mm
      bottom: 20mm
      left: 18mm
  styles:
    AbstractParagraph:
      family: paragraph
      based-on: Text_20_body
      align: justify
      line-height: 1.2
      margin-bottom: 2mm
```

### H.3 Letter recipe

Use this when you need:

- sender/date/greeting/body/closing flow,
- compact correspondence spacing,
- landscape variant for branded letterheads.

```yaml
profile: github-letter-cookbook
version: 1

roles:
  LetterTitle:
  Sender:
  DateLine:
  Greeting:
  Body:
  Closing:

recognize:
  LetterTitle:
    match:
      - heading(level=1)
    where:
      - position == first
    priority: 100
  Sender:
    match:
      - paragraph
    where:
      - follows LetterTitle
    priority: 90
  DateLine:
    match:
      - paragraph
    where:
      - follows Sender
    priority: 80
  Greeting:
    match:
      - paragraph
    where:
      - follows DateLine
    priority: 70
  Body:
    match:
      - paragraph
    where:
      - after Greeting
      - before Closing
    priority: 60
  Closing:
    match:
      - heading(level=2)
    where:
      - after Greeting
    priority: 50

layout:
  LetterTitle:
    style: Heading_20_1
  Sender:
    style: AuthorLine
  DateLine:
    style: DateLine
  Greeting:
    style: LetterSalutation
  Body:
    style: Text_20_body
  Closing:
    style: LetterClosing

present:
  page:
    orientation: landscape
    margins:
      top: 16mm
      right: 16mm
      bottom: 18mm
      left: 16mm
  styles:
    LetterClosing:
      family: paragraph
      based-on: Text_20_body
      margin-top: 4mm
```

## Part I: Migration Notes (v0.1 -> v0.2)

### I.1 Compatibility matrix

| Feature | v0.1 baseline | current implementation state |
| --- | --- | --- |
| `recognize` + `layout` | required | fully interpreted |
| `present.page.orientation` | extension | materialized into `styles.xml` |
| `present.page.margins` | extension | materialized into `styles.xml` |
| `present.styles` | extension | materialized into `styles.xml` |
| `present.fonts` | extension | parsed into runtime model; not fully emitted yet |
| `present.master` | extension | parsed into runtime model; not fully emitted yet |

### I.2 Recommended migration strategy

1. Keep existing v0.1 profiles unchanged (`recognize` + `layout` only).
2. Add `present.page` and `present.styles` first for visible results.
3. Introduce `present.fonts` and `present.master` fields for forward compatibility.
4. Run with `--diag-json` to confirm parse/model/materialization diagnostics.
5. Use `--strict` in CI after profiles stabilize.

### I.3 Operational guidance

- If you need guaranteed pixel-perfect page headers/footers immediately, keep using `.odt` templates for those assets while gradually adding `present` fields.
- Prefer style names in `layout` that are explicitly defined in `present.styles` for portability across templates.
- Avoid procedural workarounds; express differences as profile data only.

