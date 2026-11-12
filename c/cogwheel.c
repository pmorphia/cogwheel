/* Cogwheel for C. See cogwheel.h. */
#include "cogwheel.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cw_node {
    cw_kind kind;
    char *name, *value;
    int level;
    char *raw; /* this node's own source lines; NULL once changed */
    cw_node **children;
    size_t count, cap;
    cw_node *parent;
    char *file;
};

/* ------------------------------------------------------------------ small helpers */
static char *dup_n(const char *s, size_t n) {
    char *d = malloc(n + 1);
    if (!d)
        return NULL;
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}
static char *dup(const char *s) { return dup_n(s ? s : "", strlen(s ? s : "")); }
static int space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
/* [*b, *e) with surrounding whitespace removed */
static void trim(const char **b, const char **e) {
    while (*b < *e && space(**b))
        ++*b;
    while (*e > *b && space((*e)[-1]))
        --*e;
}
static char *trimmed(const char *b, const char *e) {
    trim(&b, &e);
    return dup_n(b, (size_t)(e - b));
}

typedef struct {
    char *d;
    size_t n, cap;
} buf;
static int put_n(buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (cap < b->n + n + 1)
            cap *= 2;
        char *d = realloc(b->d, cap);
        if (!d)
            return -1;
        b->d = d;
        b->cap = cap;
    }
    memcpy(b->d + b->n, s, n);
    b->n += n;
    b->d[b->n] = 0;
    return 0;
}
static int put(buf *b, const char *s) { return put_n(b, s, strlen(s)); }
static char *done(buf *b) { return b->d ? b->d : dup(""); }

static cw_node *node_new(cw_kind kind, const char *name, const char *value, int level, const char *raw, size_t raw_n) {
    cw_node *n = calloc(1, sizeof *n);
    if (!n)
        return NULL;
    n->kind = kind;
    n->name = dup(name);
    n->value = dup(value);
    n->level = level;
    n->raw = raw ? dup_n(raw, raw_n) : NULL;
    if (!n->name || !n->value || (raw && !n->raw)) {
        cw_free(n);
        return NULL;
    }
    return n;
}
static int insert_at(cw_node *parent, size_t at, cw_node *child) {
    if (!child)
        return -1;
    if (parent->count == parent->cap) {
        size_t cap = parent->cap ? parent->cap * 2 : 8;
        cw_node **c = realloc(parent->children, cap * sizeof *c);
        if (!c)
            return -1;
        parent->children = c;
        parent->cap = cap;
    }
    memmove(parent->children + at + 1, parent->children + at, (parent->count - at) * sizeof *parent->children);
    parent->children[at] = child;
    parent->count++;
    child->parent = parent;
    return 0;
}
static int append(cw_node *parent, cw_node *child) { return insert_at(parent, parent->count, child); }

void cw_free(cw_node *n) {
    if (!n)
        return;
    for (size_t i = 0; i < n->count; i++)
        cw_free(n->children[i]);
    free(n->children);
    free(n->name);
    free(n->value);
    free(n->raw);
    free(n->file);
    free(n);
}

/* ------------------------------------------------------------------ values */
char *cw_unquote(const char *text) {
    const char *b = text, *e = text + strlen(text);
    trim(&b, &e);
    if (e - b < 2 || *b != '"' || e[-1] != '"')
        return dup_n(b, (size_t)(e - b));
    buf out = {0};
    for (const char *p = b + 1; p < e - 1; p++) {
        char c = *p;
        if (c == '\\' && p + 1 < e - 1) {
            c = *++p;
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
        }
        if (put_n(&out, &c, 1)) {
            free(out.d);
            return NULL;
        }
    }
    return done(&out);
}

char *cw_quote(const char *v) {
    size_t n = strlen(v);
    int plain = n > 0 && !space(v[0]) && !space(v[n - 1]) && v[0] != '"' && !strchr(v, '\n') && !strchr(v, '\r');
    if (plain)
        return dup(v);
    buf out = {0};
    int bad = put(&out, "\"");
    for (const char *p = v; *p && !bad; p++) {
        switch (*p) {
        case '\\': bad = put(&out, "\\\\"); break;
        case '"': bad = put(&out, "\\\""); break;
        case '\n': bad = put(&out, "\\n"); break;
        case '\r': bad = put(&out, "\\r"); break;
        case '\t': bad = put(&out, "\\t"); break;
        default: bad = put_n(&out, p, 1);
        }
    }
    if (bad || put(&out, "\"")) {
        free(out.d);
        return NULL;
    }
    return done(&out);
}

/* ------------------------------------------------------------------ lines */
/* the line without up to three spaces of indentation, or NULL for deeper indentation */
static const char *unindent(const char *b, const char *e) {
    int n = 0;
    while (b < e && *b == ' ' && n < 4)
        b++, n++;
    return n > 3 ? NULL : b;
}
/* the heading level of [b, e) and its name (malloc'd), or 0 */
static int heading_of(const char *b, const char *e, char **name) {
    const char *t = unindent(b, e);
    if (!t || t >= e || *t != '#')
        return 0;
    int level = 0;
    while (t + level < e && t[level] == '#')
        level++;
    if (level > 6 || (t + level < e && t[level] != ' ' && t[level] != '\t'))
        return 0;
    const char *nb = t + level, *ne = e;
    trim(&nb, &ne);
    const char *s = ne;
    while (s > nb && s[-1] == '#')
        s--;
    if (s == nb || space(s[-1]))
        ne = s;
    *name = trimmed(nb, ne);
    return *name ? level : 0;
}
/* the key and value of an entry line (malloc'd), or 0 */
static int entry_of(const char *b, const char *e, char **key, char **value) {
    const char *t = unindent(b, e);
    if (!t || t >= e || *t != '>')
        return 0;
    const char *eq = memchr(t + 1, '=', (size_t)(e - t - 1));
    if (!eq)
        return 0;
    const char *kb = t + 1, *ke = eq;
    trim(&kb, &ke);
    if (kb == ke)
        return 0;
    char *raw = dup_n(eq + 1, (size_t)(e - eq - 1));
    *key = dup_n(kb, (size_t)(ke - kb));
    *value = raw ? cw_unquote(raw) : NULL;
    free(raw);
    if (!*key || !*value) {
        free(*key);
        free(*value);
        return 0;
    }
    return 1;
}
/* the fence a line opens or closes: its character and length, or 0 */
static size_t fence_of(const char *b, const char *e, char *mark) {
    const char *t = unindent(b, e);
    if (!t || e - t < 3 || (*t != '`' && *t != '~') || t[1] != *t || t[2] != *t)
        return 0;
    size_t n = 0;
    while (t + n < e && t[n] == *t)
        n++;
    *mark = *t;
    return n;
}
static int only_fence(const char *b, const char *e) {
    trim(&b, &e);
    for (const char *p = b; p < e; p++)
        if (*p != *b)
            return 0;
    return 1;
}

cw_node *cw_parse(const char *text, size_t length) {
    const char *p = text, *end = text + length;
    int bom = length >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0;
    if (bom)
        p += 3;
    /* a byte order mark is the document's own raw text, so it is written back */
    cw_node *doc = node_new(CW_DOCUMENT, "", "", 0, bom ? text : NULL, bom ? 3 : 0);
    if (!doc)
        return NULL;
    cw_node *current = doc;
    const char *block = NULL; /* start of the pending text block */
    char fence = 0;
    size_t fence_n = 0;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *next = nl ? nl + 1 : end;
        const char *be = nl ? nl : end; /* the line without its break */
        if (be > p && be[-1] == '\r')
            be--;
        char mark = 0;
        size_t fn;
        if (fence) {
            if (!block)
                block = p;
            if ((fn = fence_of(p, be, &mark)) && mark == fence && fn >= fence_n && only_fence(p, be))
                fence = 0;
            p = next;
            continue;
        }
        if ((fn = fence_of(p, be, &mark))) {
            fence = mark;
            fence_n = fn;
            if (!block)
                block = p;
            p = next;
            continue;
        }
        char *name = NULL, *key = NULL, *value = NULL;
        int level = heading_of(p, be, &name);
        int entry = !level && entry_of(p, be, &key, &value);
        if (!level && !entry) {
            if (!block)
                block = p;
            p = next;
            continue;
        }
        int bad = 0;
        if (block) {
            bad |= append(current, node_new(CW_TEXT, "", "", 0, block, (size_t)(p - block)));
            block = NULL;
        }
        if (level) {
            while (current != doc && current->level >= level)
                current = current->parent;
            cw_node *s = node_new(CW_SECTION, name, "", level, p, (size_t)(next - p));
            bad |= append(current, s);
            if (!bad)
                current = s;
        } else
            bad |= append(current, node_new(CW_ENTRY, key, value, 0, p, (size_t)(next - p)));
        free(name);
        free(key);
        free(value);
        if (bad) {
            cw_free(doc);
            return NULL;
        }
        p = next;
    }
    if (block && append(current, node_new(CW_TEXT, "", "", 0, block, (size_t)(end - block)))) {
        cw_free(doc);
        return NULL;
    }
    return doc;
}

cw_node *cw_confile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    buf b = {0};
    char chunk[8192];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
        if (put_n(&b, chunk, n)) {
            free(b.d);
            fclose(f);
            errno = ENOMEM;
            return NULL;
        }
    int failed = ferror(f);
    fclose(f);
    if (failed) {
        free(b.d);
        errno = EIO;
        return NULL;
    }
    cw_node *doc = cw_parse(b.d ? b.d : "", b.n);
    free(b.d);
    if (doc && !(doc->file = dup(path))) {
        cw_free(doc);
        doc = NULL;
    }
    return doc;
}

/* ------------------------------------------------------------------ queries */
cw_kind cw_kind_of(const cw_node *n) { return n->kind; }
const char *cw_name(const cw_node *n) { return n->name; }
const char *cw_value(const cw_node *n) { return n->value; }
int cw_level(const cw_node *n) { return n->level; }
size_t cw_count(const cw_node *n) { return n->count; }
cw_node *cw_child(const cw_node *n, size_t i) { return i < n->count ? n->children[i] : NULL; }
cw_node *cw_parent(const cw_node *n) { return n->parent; }

/* the kind and name a selector asks for; name is malloc'd */
static cw_kind selector(const char *sel, char **name) {
    const char *b = sel, *e = sel + strlen(sel);
    trim(&b, &e);
    cw_kind kind = CW_ENTRY;
    if (b < e && (*b == '#' || *b == '>')) {
        kind = *b == '#' ? CW_SECTION : CW_ENTRY;
        b++;
    }
    *name = trimmed(b, e);
    return kind;
}
static cw_node *search(const cw_node *n, cw_kind kind, const char *name, int deep) {
    for (size_t i = 0; i < n->count; i++)
        if (n->children[i]->kind == kind && strcmp(n->children[i]->name, name) == 0)
            return n->children[i];
    if (deep)
        for (size_t i = 0; i < n->count; i++)
            if (n->children[i]->kind == CW_SECTION) {
                cw_node *found = search(n->children[i], kind, name, 1);
                if (found)
                    return found;
            }
    return NULL;
}
static cw_node *find(const cw_node *n, const char *sel, int deep) {
    if (!n || !sel)
        return NULL;
    char *name;
    cw_kind kind = selector(sel, &name);
    if (!name)
        return NULL;
    cw_node *found = search(n, kind, name, deep);
    free(name);
    return found;
}
cw_node *cw_find(const cw_node *n, const char *sel) { return find(n, sel, 0); }
cw_node *cw_find_deep(const cw_node *n, const char *sel) { return find(n, sel, 1); }
cw_node *cw_get_by_id(const cw_node *n, const char *id) {
    if (!n || !id)
        return NULL;
    char *name = trimmed(id, id + strlen(id));
    cw_node *found = name ? search(n, CW_SECTION, name, 1) : NULL;
    free(name);
    return found;
}
cw_node *cw_select(const cw_node *n, const char *path) {
    const char *p = path;
    cw_node *at = (cw_node *)n;
    while (at && p) {
        const char *slash = strchr(p, '/');
        char *part = dup_n(p, slash ? (size_t)(slash - p) : strlen(p));
        if (!part)
            return NULL;
        at = cw_find(at, part);
        free(part);
        p = slash ? slash + 1 : NULL;
    }
    return at;
}

/* the entry selector for a key: "name" and ">name" are the same */
static cw_node *entry(const cw_node *n, const char *key, int deep) {
    if (!n || !key)
        return NULL;
    const char *k = key;
    while (space(*k))
        k++;
    if (*k == '>')
        return find(n, k, deep);
    char *name = trimmed(k, k + strlen(k));
    cw_node *found = name ? search(n, CW_ENTRY, name, deep) : NULL;
    free(name);
    return found;
}
const char *cw_get(const cw_node *n, const char *key) {
    cw_node *e = entry(n, key, 0);
    return e ? e->value : NULL;
}
const char *cw_get_or(const cw_node *n, const char *key, const char *fallback) {
    const char *v = cw_get(n, key);
    return v ? v : fallback;
}
const char *cw_get_deep(const cw_node *n, const char *key) {
    cw_node *e = entry(n, key, 1);
    return e ? e->value : NULL;
}
/* the value without '_' digit separators (malloc'd) */
static char *digits(const cw_node *n, const char *key) {
    const char *v = cw_get(n, key);
    if (!v)
        return NULL;
    char *d = dup(v), *w = d;
    if (!d)
        return NULL;
    for (char *r = d; *r; r++)
        if (*r != '_')
            *w++ = *r;
    *w = 0;
    return d;
}
int cw_get_int(const cw_node *n, const char *key, long long *out) {
    char *d = digits(n, key), *rest;
    if (!d)
        return -1;
    errno = 0;
    long long v = strtoll(d, &rest, 10);
    int ok = *d && !space(*d) && *rest == 0 && errno == 0;
    free(d);
    if (ok)
        *out = v;
    return ok ? 0 : -1;
}
int cw_get_double(const cw_node *n, const char *key, double *out) {
    char *d = digits(n, key), *rest;
    if (!d)
        return -1;
    double v = strtod(d, &rest);
    int ok = *d && !space(*d) && *rest == 0;
    free(d);
    if (ok)
        *out = v;
    return ok ? 0 : -1;
}
static int same(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}
int cw_get_bool(const cw_node *n, const char *key, int *out) {
    const char *v = cw_get(n, key);
    if (!v)
        return -1;
    if (same(v, "true") || same(v, "yes") || same(v, "on") || same(v, "1"))
        return *out = 1, 0;
    if (same(v, "false") || same(v, "no") || same(v, "off") || same(v, "0"))
        return *out = 0, 0;
    return -1;
}
void cw_free_list(char **list) {
    if (!list)
        return;
    for (char **p = list; *p; p++)
        free(*p);
    free(list);
}
static int list_push(char ***list, size_t *n, const char *b, const char *e) {
    char **l = realloc(*list, (*n + 2) * sizeof *l);
    if (!l)
        return -1;
    *list = l;
    char *raw = dup_n(b, (size_t)(e - b));
    l[*n] = raw ? cw_unquote(raw) : NULL;
    free(raw);
    if (!l[*n])
        return -1;
    l[++*n] = NULL;
    return 0;
}
char **cw_get_list(const cw_node *n, const char *key, size_t *count) {
    const char *v = cw_get(n, key);
    if (!v)
        return NULL;
    const char *b = v, *e = v + strlen(v);
    trim(&b, &e);
    if (e - b >= 2 && *b == '[' && e[-1] == ']')
        b++, e--;
    size_t k = 0;
    char **list = calloc(1, sizeof *list);
    if (!list)
        return NULL;
    const char *tb = b, *te = e;
    trim(&tb, &te);
    if (tb < te) {
        int quoted = 0, escaped = 0;
        const char *item = b;
        for (const char *p = b; p < e; p++) {
            if (escaped)
                escaped = 0;
            else if (quoted && *p == '\\')
                escaped = 1;
            else if (*p == '"')
                quoted = !quoted;
            else if (*p == ',' && !quoted) {
                if (list_push(&list, &k, item, p))
                    return cw_free_list(list), NULL;
                item = p + 1;
            }
        }
        if (list_push(&list, &k, item, e))
            return cw_free_list(list), NULL;
    }
    if (count)
        *count = k;
    return list;
}

static int blank(const char *b, const char *e) {
    trim(&b, &e);
    return b == e;
}
char *cw_text(const cw_node *n) {
    if (n->kind == CW_TEXT)
        return trimmed(n->raw, n->raw + strlen(n->raw));
    buf out = {0};
    for (size_t i = 0; i < n->count; i++) {
        cw_node *c = n->children[i];
        if (c->kind != CW_TEXT || blank(c->raw, c->raw + strlen(c->raw)))
            continue;
        const char *b = c->raw, *e = c->raw + strlen(c->raw);
        trim(&b, &e);
        if ((out.n && put(&out, "\n\n")) || put_n(&out, b, (size_t)(e - b))) {
            free(out.d);
            return NULL;
        }
    }
    return done(&out);
}

/* ------------------------------------------------------------------ changes */
static int ends_blank(const cw_node *n) {
    if (n->kind == CW_SECTION)
        return n->count && ends_blank(n->children[n->count - 1]);
    if (n->kind != CW_TEXT)
        return 0;
    size_t len = strlen(n->raw);
    return blank(n->raw, n->raw + len) || (len >= 2 && strcmp(n->raw + len - 2, "\n\n") == 0) ||
           (len >= 4 && strcmp(n->raw + len - 4, "\r\n\r\n") == 0);
}
static int starts_blank(const cw_node *n) {
    if (n->kind != CW_TEXT)
        return 0;
    const char *nl = strchr(n->raw, '\n');
    return blank(n->raw, nl ? nl : n->raw + strlen(n->raw));
}
static cw_node *gap(void) { return node_new(CW_TEXT, "", "", 0, "\n", 1); }
/* inserts a block at a child position, keeping a blank line on each side */
static int insert_block(cw_node *s, size_t at, cw_node *node) {
    if (!node)
        return -1;
    if ((at > 0 && !ends_blank(s->children[at - 1])) || (at == 0 && s->kind == CW_SECTION)) {
        if (insert_at(s, at, gap()))
            return cw_free(node), -1;
        at++;
    }
    if (insert_at(s, at, node))
        return cw_free(node), -1;
    at++;
    if (at < s->count && !starts_blank(s->children[at]) && insert_at(s, at, gap()))
        return -1;
    return 0;
}
static size_t content_end(const cw_node *s) {
    size_t at = 0;
    for (size_t i = 0; i < s->count && s->children[i]->kind != CW_SECTION; i++)
        at = i + 1;
    return at;
}
int cw_set(cw_node *s, const char *key, const char *value) {
    cw_node *e = entry(s, key, 0);
    if (e) {
        char *v = dup(value);
        if (!v)
            return -1;
        free(e->value);
        free(e->raw);
        e->value = v;
        e->raw = NULL;
        return 0;
    }
    const char *k = key;
    while (space(*k))
        k++;
    if (*k == '>')
        k++;
    char *name = trimmed(k, k + strlen(k));
    if (!name)
        return -1;
    cw_node *n = node_new(CW_ENTRY, name, value, 0, NULL, 0);
    free(name);
    return insert_block(s, content_end(s), n);
}
cw_node *cw_add_section(cw_node *s, const char *name) {
    char *nm = trimmed(name, name + strlen(name));
    if (!nm)
        return NULL;
    cw_node *n = node_new(CW_SECTION, nm, "", s->level + 1 > 6 ? 6 : s->level + 1, NULL, 0);
    free(nm);
    return n && insert_block(s, s->count, n) == 0 ? n : NULL;
}
int cw_add_text(cw_node *s, const char *text) {
    const char *b = text, *e = text + strlen(text);
    trim(&b, &e);
    buf raw = {0};
    if (put_n(&raw, b, (size_t)(e - b)) || put(&raw, "\n")) {
        free(raw.d);
        return -1;
    }
    cw_node *n = node_new(CW_TEXT, "", "", 0, raw.d, raw.n);
    free(raw.d);
    return insert_block(s, content_end(s), n);
}
int cw_remove(cw_node *s, const char *sel) {
    cw_node *n = cw_find(s, sel);
    if (!n)
        return 0;
    for (size_t i = 0; i < s->count; i++)
        if (s->children[i] == n) {
            memmove(s->children + i, s->children + i + 1, (s->count - i - 1) * sizeof *s->children);
            s->count--;
            break;
        }
    cw_free(n);
    return 1;
}

/* ------------------------------------------------------------------ writing */
static int write_node(buf *out, const cw_node *n) {
    if (n->raw) {
        if (put(out, n->raw))
            return -1;
    } else if (n->kind == CW_ENTRY) {
        char *q = cw_quote(n->value);
        int bad = !q || put(out, "> ") || put(out, n->name) || put(out, " = ") || put(out, q) || put(out, "\n");
        free(q);
        if (bad)
            return -1;
    } else if (n->kind == CW_SECTION) {
        for (int i = 0; i < n->level; i++)
            if (put(out, "#"))
                return -1;
        if (put(out, " ") || put(out, n->name) || put(out, "\n"))
            return -1;
    }
    if (n->kind == CW_ENTRY || n->kind == CW_TEXT)
        return 0;
    for (size_t i = 0; i < n->count; i++)
        if (write_node(out, n->children[i]))
            return -1;
    return 0;
}
char *cw_source(const cw_node *n) {
    buf out = {0};
    if (write_node(&out, n)) {
        free(out.d);
        return NULL;
    }
    return done(&out);
}
int cw_save(const cw_node *doc, const char *path) {
    const char *target = path ? path : doc->file;
    if (!target) {
        errno = EINVAL;
        return -1;
    }
    char *text = cw_source(doc);
    if (!text)
        return -1;
    FILE *f = fopen(target, "wb");
    if (!f) {
        free(text);
        return -1;
    }
    size_t n = strlen(text);
    int bad = fwrite(text, 1, n, f) != n;
    bad |= fclose(f) != 0;
    free(text);
    return bad ? -1 : 0;
}
