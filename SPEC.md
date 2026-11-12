# The Cogwheel format (version 1)

A Cogwheel file (`.cw`, media type `text/markdown`) is a [CommonMark](https://commonmark.org)
document. Any Markdown viewer or editor shows it as Markdown, and Markdown syntax highlighting
works. A Cogwheel reader also takes **configuration** from it: sections from headings and
entries from quote lines that contain `=`.

```markdown
Edit fields after >

# Main

## User Section

This section is about the `user` settings of the `main` module.

> name = danny

> email = user@example.com
```

This document has one section, `Main`, with one subsection, `User Section`, that has the
entries `name` (`danny`) and `email`.

## Lines

The text is read line by line. Lines end with LF or CRLF. The text is UTF-8; a leading
byte order mark is ignored. Each line is one of these:

| Line | Example | Is |
|---|---|---|
| an ATX heading | `## User Section` | a **section** |
| a quote line with `=` | `> name = danny` | an **entry** |
| anything else | `Some text.`, `> a quote`, a blank line | **text** |

### Headings

A heading line is up to three spaces, then 1 to 6 `#` characters, then a space or tab (or the
end of the line). The section's **name** is the rest of the line with surrounding whitespace
removed, and without an optional closing sequence of `#` characters that follows a space
(`## Name ##` is named `Name`). Setext headings (text underlined with `===` or `---`) are text.

A section contains every line after its heading up to the next heading of the same or a lower
level (fewer `#`), so sections nest by level: a `###` after a `#` is a subsection of it even
without a `##` between them. Lines before the first heading belong to the **document**, the
root of the tree.

### Entries

An entry line is up to three spaces, then `>`, then a **key**, `=`, and a **value**:

```markdown
> key = value
```

- The key is the text between `>` and the first `=`, with surrounding whitespace removed. It
  must not be empty; `> = x` is text.
- The value is the text after the first `=`, with surrounding whitespace removed, so it may
  itself contain `=` (`> url = https://example.com/?a=b`).
- A value in double quotes is a **quoted value**: the quotes are removed and `\"`, `\\`, `\n`,
  `\r`, and `\t` are read as a quote, a backslash, a line feed, a carriage return, and a tab.
  Any other `\x` is `x`. Quote a value to keep surrounding spaces, to write an empty value
  (`> empty = ""`), or to put line breaks in it.
- A quote line without `=` is text (an ordinary Markdown quote).

Separate entries with blank lines. Markdown joins consecutive quote lines into one paragraph,
so `> a = 1` directly followed by `> b = 2` is two entries for Cogwheel, but reads as one line
in a Markdown viewer.

An entry belongs to the section it is in. Keys are case-sensitive. When a section has two
entries with the same key, a lookup finds the first; readers keep both.

### Code blocks

A fenced code block (a line of three or more `` ` `` or `~`, up to the closing fence of the same
character and at least the same length) is text, including any headings and entry lines in it.
Indented code blocks are not recognised: a line indented by four spaces or more is never a
heading or an entry, so they are text too.

### Text

Every other line is text that belongs to the current section. Text has no meaning for
configuration; readers keep it so that a document is written back as it was read, and expose
it (for instance as a section's `text()`) for documentation.

## Values

All values are text. Readers convert on request:

| Request | Accepts |
|---|---|
| integer | an optional sign and decimal digits; `_` between digits is ignored (`1_000`) |
| number | a decimal or exponent number (`1.5`, `-2e3`); `_` is ignored |
| boolean | `true`, `yes`, `on`, `1` and `false`, `no`, `off`, `0`, in any case |
| list | items separated by commas, optionally inside `[` `]`; each item has surrounding whitespace removed and may be quoted (`> tags = [a, "b, c", d]`) |

A value that cannot be converted is an error.

## Selectors

Libraries address the tree with selectors:

| Selector | Finds |
|---|---|
| `#Name` | the section named `Name` |
| `>key` | the entry `key` |
| `key` | the entry `key` |

`node[selector]` searches a node's direct children and fails when nothing matches.
`getById(name)` finds a section at any depth (the first, in document order).
`selectorAll()` returns a view of a node whose selectors search every level below it, in
document order: `doc.selectorAll()[">name"]` is the first entry `name` anywhere.

## Writing

A document written back (`source()`, `save()`) is the text it was read from, byte for byte,
except for what was changed. A changed entry is written as `> key = value`, quoting the value
when it is empty, has surrounding whitespace, starts with `"`, or contains a line break. An
added entry goes after the last entry or text of its section (before its subsections), with
a blank line before and after it. An added section gets a heading one level below its parent
(at most 6).

## Markup

Flywheel programs can build a document with markup, which `cogwheel.from_markup` reads and
`cogwheel.render` writes as `.cw` text:

```flywheel
import cogwheel
var doc = cogwheel.from_markup(<cogwheel.cw>
    <cogwheel.heading name="Main">
        <cogwheel.rawstr>Hello world!</cogwheel.rawstr>
        <cogwheel.conf key="name" value="danny">
        <cogwheel.conf key="email" value="user@example.com">
    </cogwheel.heading>
</cogwheel.cw>)
```

`cw` is the document, `heading` a section (nested headings are subsections), `rawstr` a text
paragraph, and `conf` an entry. `conf` takes no children and needs no closing tag.
