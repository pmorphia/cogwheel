// Cogwheel for C++17: configuration written as Markdown (.cw files). Header only.
//
//     auto conf = cogwheel::confile("main.cw");
//     std::string name = conf["#Main"]["#User Section"][">name"];
//     conf.getById("Main")["#User Section"].get("name");
//     conf.selectorAll()[">email"];
//     conf.getById("User Section").set("name", "ann");
//     conf.save();                     // only the changed line is rewritten
//
// See https://github.com/pmorphia/cogwheel for the format.
#ifndef COGWHEEL_HPP
#define COGWHEEL_HPP

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cogwheel {

inline constexpr const char *version = "1.0.0";

// A missing section or entry (also std::out_of_range), or a value that does not convert.
struct not_found : std::out_of_range {
    using std::out_of_range::out_of_range;
};
struct bad_value : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

enum class kind { document, section, entry, text, all };

namespace detail {
inline bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && space(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && space(s.back()))
        s.remove_suffix(1);
    return s;
}
inline bool starts(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
inline std::string lower(std::string_view s) {
    std::string out(s);
    for (auto &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
// the line without up to three spaces of indentation; nullopt for deeper indentation
inline std::optional<std::string_view> unindent(std::string_view line) {
    size_t n = 0;
    while (n < line.size() && line[n] == ' ' && n < 4)
        ++n;
    if (n > 3)
        return std::nullopt;
    return line.substr(n);
}
} // namespace detail

// The value an entry's text stands for: quoted values have their escapes read.
inline std::string unquote(std::string_view text) {
    auto t = detail::trim(text);
    if (t.size() < 2 || t.front() != '"' || t.back() != '"')
        return std::string(t);
    std::string out;
    auto body = t.substr(1, t.size() - 2);
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '\\' && i + 1 < body.size()) {
            c = body[++i];
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
        }
        out += c;
    }
    return out;
}

// A value as an entry writes it: as is, or quoted when it is empty, has surrounding
// whitespace, starts with a quote, or contains a line break.
inline std::string quote(std::string_view v) {
    bool plain = !v.empty() && detail::trim(v).size() == v.size() && v.front() != '"' &&
                 v.find('\n') == std::string_view::npos && v.find('\r') == std::string_view::npos;
    if (plain)
        return std::string(v);
    std::string out = "\"";
    for (char c : v) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    return out + "\"";
}

// A part of a document. An entry converts to its value (std::string), so
// `std::string name = doc["#Main"][">name"];` reads a value.
class node {
  public:
    cogwheel::kind kind = kind::document;
    std::string name;  // a section's heading text, or an entry's key
    std::string value; // an entry's value
    int level = 0;     // a section's heading level; 0 for the document
    std::string raw;   // this node's own source lines; empty once changed
    std::vector<std::unique_ptr<node>> children;
    std::string file;  // the file a document was read from

    node() = default;
    node(cogwheel::kind k, std::string n = "", std::string v = "", int l = 0, std::string r = "")
        : kind(k), name(std::move(n)), value(std::move(v)), level(l), raw(std::move(r)) {}
    node(const node &) = delete;
    node &operator=(const node &) = delete;
    node(node &&) = default;
    node &operator=(node &&) = default;

    // ------------------------------------------------------------ selectors
    // "#Name" is a section, ">key" or "key" an entry; nullptr when nothing matches.
    node *find(std::string_view selector) const {
        auto [k, n] = parse_selector(selector);
        return search(k, n, kind == kind::all);
    }
    bool has(std::string_view selector) const { return find(selector) != nullptr; }
    std::vector<node *> find_all(std::string_view selector) const {
        auto [k, n] = parse_selector(selector);
        std::vector<node *> out;
        collect(k, n, kind == kind::all, out);
        return out;
    }
    node &operator[](std::string_view selector) const {
        if (auto *found = find(selector))
            return *found;
        auto [k, n] = parse_selector(selector);
        throw not_found("no " + std::string(k == kind::section ? "section" : "entry") + " '" + n +
                        "' in " + label());
    }
    // The section with this name at any depth.
    node &getById(std::string_view id) const {
        if (auto *found = search(kind::section, std::string(detail::trim(id)), true))
            return *found;
        throw not_found("no section '" + std::string(id) + "' in " + label());
    }
    node &get_by_id(std::string_view id) const { return getById(id); }
    // A view of this node whose selectors search every level below it. It refers to this node,
    // which must outlive it.
    node selectorAll() const {
        node view(kind::all, name, "", level);
        view.target_ = this;
        return view;
    }
    node selector_all() const { return selectorAll(); }

    // ------------------------------------------------------------ values
    const std::string &get(std::string_view key) const { return (*this)[entry_selector(key)].value; }
    std::string get_or(std::string_view key, std::string fallback) const {
        auto *found = find(entry_selector(key));
        return found ? found->value : std::move(fallback);
    }
    long long get_int(std::string_view key) const {
        auto v = without_underscores(get(key));
        try {
            size_t used = 0;
            long long out = std::stoll(v, &used, 10);
            if (used == v.size() && !v.empty() && !detail::space(v[0]))
                return out;
        } catch (const std::logic_error &) {
        }
        throw bad_value("cogwheel entry '" + std::string(key) + "' is not an integer: " + v);
    }
    double get_double(std::string_view key) const {
        auto v = without_underscores(get(key));
        try {
            size_t used = 0;
            double out = std::stod(v, &used);
            if (used == v.size() && !v.empty() && !detail::space(v[0]))
                return out;
        } catch (const std::logic_error &) {
        }
        throw bad_value("cogwheel entry '" + std::string(key) + "' is not a number: " + v);
    }
    bool get_bool(std::string_view key) const {
        auto v = detail::lower(get(key));
        if (v == "true" || v == "yes" || v == "on" || v == "1")
            return true;
        if (v == "false" || v == "no" || v == "off" || v == "0")
            return false;
        throw bad_value("cogwheel entry '" + std::string(key) + "' is not true or false: " + v);
    }
    // A comma-separated value, optionally in brackets: `> tags = [a, "b, c"]`.
    std::vector<std::string> get_list(std::string_view key) const {
        auto v = detail::trim(get(key));
        if (v.size() >= 2 && v.front() == '[' && v.back() == ']')
            v = v.substr(1, v.size() - 2);
        std::vector<std::string> out;
        if (detail::trim(v).empty())
            return out;
        bool quoted = false, escaped = false;
        size_t start = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            char c = v[i];
            if (escaped)
                escaped = false;
            else if (quoted && c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = !quoted;
            else if (c == ',' && !quoted) {
                out.push_back(unquote(v.substr(start, i - start)));
                start = i + 1;
            }
        }
        out.push_back(unquote(v.substr(start)));
        return out;
    }
    operator const std::string &() const { return value; }

    // ------------------------------------------------------------ structure
    std::vector<node *> sections() const { return of(kind::section); }
    std::vector<node *> entries() const { return of(kind::entry); }
    std::vector<std::string> keys() const {
        std::vector<std::string> out;
        for (auto *e : entries())
            out.push_back(e->name);
        return out;
    }
    // The text directly in this section (or a text block's own text), without entries.
    std::string text() const {
        if (kind == kind::text)
            return std::string(detail::trim(raw));
        std::string out;
        for (auto &c : children)
            if (c->kind == kind::text && !detail::trim(c->raw).empty())
                out += (out.empty() ? "" : "\n\n") + std::string(detail::trim(c->raw));
        return out;
    }
    // Every entry at every level, keyed by section path: "Main.User Section.name".
    std::map<std::string, std::string> to_map() const {
        std::map<std::string, std::string> out;
        flatten(target(), "", out);
        return out;
    }

    // ------------------------------------------------------------ changes
    // Sets an entry of this section, adding it after the last entry or text when missing.
    node &set(std::string_view key, std::string_view value) {
        if (auto *found = find(entry_selector(key))) {
            found->value = std::string(value);
            found->raw.clear();
            return *this;
        }
        if (kind == kind::all)
            throw bad_value("set needs a section, not a selectorAll view");
        auto k = detail::trim(key);
        if (!k.empty() && k.front() == '>')
            k = detail::trim(k.substr(1));
        insert_block(content_end(), std::make_unique<node>(kind::entry, std::string(k), std::string(value)));
        return *this;
    }
    template <class T, class = std::enable_if_t<std::is_arithmetic_v<T>>>
    node &set(std::string_view key, T value) {
        if constexpr (std::is_same_v<T, bool>)
            return set(key, std::string_view(value ? "true" : "false"));
        else {
            std::ostringstream s;
            s << value;
            return set(key, std::string_view(s.str()));
        }
    }
    node &set(std::string_view key, const char *value) { return set(key, std::string_view(value)); }
    // Adds a subsection at the end of this one and returns it.
    node &add_section(std::string_view name) {
        auto s = std::make_unique<node>(kind::section, std::string(detail::trim(name)), "", std::min(level + 1, 6));
        auto *out = s.get();
        insert_block(children.size(), std::move(s));
        return *out;
    }
    node &add_text(std::string_view text) {
        insert_block(content_end(), std::make_unique<node>(kind::text, "", "", 0, std::string(detail::trim(text)) + "\n"));
        return *this;
    }
    // Removes the first match of a selector; whether there was one.
    bool remove(std::string_view selector) {
        auto [k, n] = parse_selector(selector);
        return remove_from(const_cast<node &>(target()), k, n, kind == kind::all);
    }

    // ------------------------------------------------------------ writing
    std::string source() const {
        const node &t = target();
        if (t.kind == kind::entry)
            return t.raw.empty() ? "> " + t.name + " = " + quote(t.value) + "\n" : t.raw;
        if (t.kind == kind::text)
            return t.raw;
        std::string out = t.raw;
        if (t.kind == kind::section)
            out = t.raw.empty() ? std::string(static_cast<size_t>(t.level), '#') + " " + t.name + "\n" : t.raw;
        for (auto &c : t.children)
            out += c->source();
        return out;
    }
    void save(const std::string &path = "") const {
        const std::string &target_path = path.empty() ? file : path;
        if (target_path.empty())
            throw bad_value("save needs a path for a document that was not read from a file");
        std::ofstream f(target_path, std::ios::binary);
        f << source();
        if (!f)
            throw std::runtime_error("cannot write " + target_path);
    }
    // An entry prints as its value, a text block as its text, anything else as .cw text.
    friend std::ostream &operator<<(std::ostream &o, const node &n) {
        if (n.kind == kind::entry)
            return o << n.value;
        if (n.kind == kind::text)
            return o << n.text();
        return o << n.source();
    }

  private:
    const node *target_ = nullptr; // what a selectorAll view searches

    const node &target() const { return kind == kind::all && target_ ? *target_ : *this; }
    static std::pair<cogwheel::kind, std::string> parse_selector(std::string_view selector) {
        auto s = detail::trim(selector);
        if (!s.empty() && s.front() == '#')
            return {kind::section, std::string(detail::trim(s.substr(1)))};
        if (!s.empty() && s.front() == '>')
            return {kind::entry, std::string(detail::trim(s.substr(1)))};
        return {kind::entry, std::string(s)};
    }
    static std::string entry_selector(std::string_view key) {
        auto k = detail::trim(key);
        return !k.empty() && k.front() == '>' ? std::string(k) : ">" + std::string(k);
    }
    static std::string without_underscores(std::string v) {
        v.erase(std::remove(v.begin(), v.end(), '_'), v.end());
        return v;
    }
    // a selectorAll view searches the node it views, at every level
    node *search(cogwheel::kind k, const std::string &n, bool deep) const {
        const node &t = target();
        for (auto &c : t.children)
            if (c->kind == k && c->name == n)
                return c.get();
        if (deep)
            for (auto &c : t.children)
                if (c->kind == kind::section)
                    if (auto *found = c->search(k, n, true))
                        return found;
        return nullptr;
    }
    void collect(cogwheel::kind k, const std::string &n, bool deep, std::vector<node *> &out) const {
        for (auto &c : target().children) {
            if (c->kind == k && c->name == n)
                out.push_back(c.get());
            if (deep && c->kind == kind::section)
                c->collect(k, n, true, out);
        }
    }
    static bool remove_from(node &p, cogwheel::kind k, const std::string &n, bool deep) {
        for (auto it = p.children.begin(); it != p.children.end(); ++it)
            if ((*it)->kind == k && (*it)->name == n) {
                p.children.erase(it);
                return true;
            }
        if (deep)
            for (auto &c : p.children)
                if (c->kind == kind::section && remove_from(*c, k, n, true))
                    return true;
        return false;
    }
    static void flatten(const node &n, const std::string &prefix, std::map<std::string, std::string> &out) {
        for (auto &c : n.children) {
            if (c->kind == kind::entry)
                out.emplace(prefix + c->name, c->value);
            else if (c->kind == kind::section)
                flatten(*c, prefix + c->name + ".", out);
        }
    }
    std::vector<node *> of(cogwheel::kind k) const {
        std::vector<node *> out;
        for (auto &c : target().children)
            if (c->kind == k)
                out.push_back(c.get());
        return out;
    }
    std::string label() const {
        const node &t = target();
        if (t.kind == kind::section)
            return "section '" + t.name + "'";
        return t.file.empty() ? "the document" : t.file;
    }
    size_t content_end() const {
        size_t at = 0;
        for (size_t i = 0; i < children.size() && children[i]->kind != kind::section; ++i)
            at = i + 1;
        return at;
    }
    static bool ends_blank(const node &n) {
        if (n.kind == kind::section)
            return !n.children.empty() && ends_blank(*n.children.back());
        if (n.kind != kind::text)
            return false;
        std::string_view r = n.raw;
        return detail::trim(r).empty() || (r.size() >= 2 && r.substr(r.size() - 2) == "\n\n") ||
               (r.size() >= 4 && r.substr(r.size() - 4) == "\r\n\r\n");
    }
    static bool starts_blank(const node &n) {
        if (n.kind != kind::text)
            return false;
        std::string_view r = n.raw;
        return detail::trim(r.substr(0, r.find('\n'))).empty();
    }
    void insert_block(size_t at, std::unique_ptr<node> n) {
        auto gap = [] { return std::make_unique<node>(kind::text, "", "", 0, "\n"); };
        if ((at > 0 && !ends_blank(*children[at - 1])) || (at == 0 && kind == kind::section))
            children.insert(children.begin() + static_cast<std::ptrdiff_t>(at++), gap());
        children.insert(children.begin() + static_cast<std::ptrdiff_t>(at++), std::move(n));
        if (at < children.size() && !starts_blank(*children[at]))
            children.insert(children.begin() + static_cast<std::ptrdiff_t>(at), gap());
    }
};

namespace detail {
inline int heading_of(std::string_view line, std::string &name) {
    auto t = unindent(line);
    if (!t || t->empty() || t->front() != '#')
        return 0;
    size_t level = 0;
    while (level < t->size() && (*t)[level] == '#')
        ++level;
    if (level > 6 || (level < t->size() && (*t)[level] != ' ' && (*t)[level] != '\t'))
        return 0;
    auto n = trim(t->substr(level));
    auto s = n;
    while (!s.empty() && s.back() == '#')
        s.remove_suffix(1);
    if (s.empty() || space(s.back()))
        n = trim(s);
    name = std::string(n);
    return static_cast<int>(level);
}
inline bool entry_of(std::string_view line, std::string &key, std::string &value) {
    auto t = unindent(line);
    if (!t || t->empty() || t->front() != '>')
        return false;
    auto body = t->substr(1);
    auto eq = body.find('=');
    if (eq == std::string_view::npos || trim(body.substr(0, eq)).empty())
        return false;
    key = std::string(trim(body.substr(0, eq)));
    value = unquote(body.substr(eq + 1));
    return true;
}
inline std::string fence_of(std::string_view line) {
    auto t = unindent(line);
    if (!t || t->size() < 3)
        return "";
    for (char mark : {'`', '~'})
        if (starts(*t, std::string(3, mark))) {
            size_t n = 0;
            while (n < t->size() && (*t)[n] == mark)
                ++n;
            return std::string(n, mark);
        }
    return "";
}
} // namespace detail

// Reads .cw text.
inline node parse(std::string_view text) {
    node doc(kind::document);
    if (detail::starts(text, "\xEF\xBB\xBF")) {
        doc.raw = "\xEF\xBB\xBF"; // written back with the document
        text.remove_prefix(3);
    }
    std::vector<node *> stack{&doc};
    std::string block, fence;
    auto flush = [&] {
        if (!block.empty()) {
            stack.back()->children.push_back(std::make_unique<node>(kind::text, "", "", 0, block));
            block.clear();
        }
    };
    size_t p = 0;
    while (p < text.size()) {
        size_t nl = text.find('\n', p);
        size_t next = nl == std::string_view::npos ? text.size() : nl + 1;
        auto line = text.substr(p, next - p);
        auto bare = line;
        if (!bare.empty() && bare.back() == '\n')
            bare.remove_suffix(1);
        if (!bare.empty() && bare.back() == '\r')
            bare.remove_suffix(1);
        p = next;
        if (!fence.empty()) {
            block += line;
            auto f = detail::fence_of(bare);
            if (!f.empty() && detail::starts(f, fence) && detail::trim(bare) == f)
                fence.clear();
            continue;
        }
        if (auto f = detail::fence_of(bare); !f.empty()) {
            fence = f;
            block += line;
            continue;
        }
        std::string name, key, value;
        int level = detail::heading_of(bare, name);
        bool entry = !level && detail::entry_of(bare, key, value);
        if (!level && !entry) {
            block += line;
            continue;
        }
        flush();
        if (level) {
            while (stack.size() > 1 && stack.back()->level >= level)
                stack.pop_back();
            auto s = std::make_unique<node>(kind::section, name, "", level, std::string(line));
            auto *ptr = s.get();
            stack.back()->children.push_back(std::move(s));
            stack.push_back(ptr);
        } else
            stack.back()->children.push_back(std::make_unique<node>(kind::entry, key, value, 0, std::string(line)));
    }
    flush();
    return doc;
}

// Reads a .cw file; std::runtime_error when it cannot be read.
inline node confile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot read " + path);
    std::ostringstream s;
    s << f.rdbuf();
    node doc = parse(s.str());
    doc.file = path;
    return doc;
}

} // namespace cogwheel

#endif
