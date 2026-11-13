# Cogwheel

**Configuration written as Markdown.** A Cogwheel file (`.cw`) is an ordinary Markdown
document, so it reads, renders, and highlights as Markdown everywhere. Headings are sections,
and quote lines with `=` in them are values:

```markdown
Edit fields after >

# Main

## User Section

This section contains information and documentation related to the `user` functionality
within the `main` module.

> name = danny

> email = user@example.com
```

Everything else is documentation that sits right next to the settings it describes. Programs
read the values, change them, and write the file back exactly as it was apart from the lines
they changed. The format is specified in [SPEC.md](SPEC.md); [main.cw](main.cw) is an example.

| Language | Library | Install |
|---|---|---|
| [Flywheel](https://flywheel.polymorphia.dev) | `cogwheel` in the standard library (compiler, Flywheel Lite, flywheel.js) | built in; `pulley add danny.cogwheel` for the latest |
| Python | [pycogwheel](https://github.com/pmorphia/pycogwheel) | `pip install pycogwheel` |
| C99 | [c/cogwheel.h](c/cogwheel.h), [c/cogwheel.c](c/cogwheel.c) | copy the two files |
| C++17 | [cpp/cogwheel.hpp](cpp/cogwheel.hpp) (header only) | copy the file |

## Flywheel

```flywheel
import cogwheel

var conf = cogwheel.confile("main.cw")
print conf["#Main"]["#User Section"][">name"]             # danny
print conf.getById("Main")["#User Section"].get("name")   # danny
print conf.selectorAll()[">name"]                         # danny, found at any level

var user = conf.getById("User Section")
user.set("name", "ann").set("age", "30")
conf.save()                                                # back to main.cw
```

Selectors: `["#Name"]` is a section, `[">key"]` (or `["key"]`) an entry; both search the
node's direct children and raise `KeyError` when nothing matches. `getById(name)` finds a
section at any depth, and `selectorAll()` returns a view whose selectors search every level.
An entry prints as its value.

| | |
|---|---|
| `cogwheel.confile(path)`, `cogwheel.parse(text)` | read a document (a `cogwheel.Node`) |
| `node[selector]`, `node.find(selector)` (`Node?`), `node.find_all(selector)`, `node.has(selector)` | look things up |
| `node.getById(name)` / `get_by_id`, `node.selectorAll()` / `selector_all` | at any depth |
| `node.get(key)`, `get_or(key, fallback)`, `get_int`, `get_float`, `get_bool`, `get_list` | values (`ValueError` when they don't convert) |
| `node.sections()`, `entries()`, `keys()`, `text()`, `to_map()` | contents |
| `node.set(key, value)`, `add_section(name)`, `add_text(text)`, `remove(selector)` | changes |
| `node.source()`, `string(node)`, `node.save(path = "")` | `.cw` text |
| `cogwheel.quote(value)`, `cogwheel.unquote(text)` | how values are written and read |
| `cogwheel.from_markup(element)`, `cogwheel.render(element)` | documents from markup |

Nodes have `kind` (`"document"`, `"section"`, `"entry"`, `"text"`), `name`, `value`, `level`,
and `children`.

Documents can also be built with markup. `conf` takes no children, so it needs no closing tag:

```flywheel
import cogwheel

var doc = cogwheel.from_markup(<cogwheel.cw><cogwheel.heading name="Main"><cogwheel.rawstr>Hello world!</cogwheel.rawstr><cogwheel.conf key="name" value="danny"><cogwheel.conf key="email" value="user@example.com"></cogwheel.heading></cogwheel.cw>)
print doc
```

```markdown
# Main

Hello world!

> name = danny

> email = user@example.com
```

`cogwheel` is a library module of Flywheel, Flywheel Lite, and flywheel.js from their releases
after Flywheel 2.17 (in flywheel.js, `confile` and `save` need Node, since browsers have no
files). This repository is also the Pulley package `danny.cogwheel`, which takes precedence
over the built-in module when it is installed, and works with earlier releases that support
the markup it uses: `pulley add danny.cogwheel`.

## C

```c
#include "cogwheel.h"

cw_node *doc = cw_confile("main.cw");
const char *name = cw_get(cw_select(doc, "#Main/#User Section"), "name");
const char *email = cw_get_deep(doc, "email");            /* selectorAll()[">email"] */
long long port;
if (cw_get_int(cw_get_by_id(doc, "Server"), "port", &port) == 0) { /* ... */ }
cw_set(cw_get_by_id(doc, "User Section"), "name", "ann");
cw_save(doc, NULL);
cw_free(doc);
```

See [cogwheel.h](c/cogwheel.h) for the whole API. Strings belong to the document; functions
return `NULL` (or `-1`) instead of failing.

## C++

```cpp
#include "cogwheel.hpp"

auto conf = cogwheel::confile("main.cw");
std::string name = conf["#Main"]["#User Section"][">name"];
auto email = conf.getById("Main")["#User Section"].get("email");
auto port = conf.selectorAll()[">port"];          // a node; converts to std::string
conf.getById("User Section").set("name", "ann").set("age", 30);
conf.save();
```

Missing parts throw `cogwheel::not_found` (an `std::out_of_range`), and values that don't
convert `cogwheel::bad_value`.

## Python

```python
import cogwheel

conf = cogwheel.confile("main.cw")
conf["#Main"]["#User Section"][">name"]           # 'danny'
conf.getById("Main")["#User Section"].get("name")
conf.selectorAll()[">email"]
conf.getById("User Section")["name"] = "ann"
conf.save()
```

See [pycogwheel](https://github.com/pmorphia/pycogwheel).

## The cogwheel command

`pulley install polymorphia.cogwheel` installs the `cogwheel` command, which reads and edits
`.cw` files from the shell (in a project that depends on the package, `pulley run cogwheel`
runs it). Entries are named by their path of sections and key, as `to_map()` names them:

```sh
cogwheel list main.cw                                # Main.User Section.name = danny, ...
cogwheel get main.cw "Main.User Section.name"        # danny
cogwheel set main.cw "Main.User Section.name" ann    # in place; adds missing sections
cogwheel json main.cw                                # every entry as a JSON object
cogwheel sections main.cw                            # the section outline
```

Section names may contain dots: each part of a path is matched against the longest section
name it can be.

## Tests

`tests/run.sh` runs each port's tests and checks every port against the conformance cases in
`tests/cases` (each `.cw` file with the tree it must read as, in `.out`):

```sh
FLYWHEEL=../build/flywheel FLYWHEEL_LITE=flywheel-lite FLYWHEEL_JS=../flywheel.js/bin/flywheel-js.js tests/run.sh
```

The Flywheel library here (`cogwheel.fw`) is the same file as the compiler's
`stdlib/fw/cogwheel.fw`.

## License

MIT
