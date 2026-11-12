/* Cogwheel for C: configuration written as Markdown (.cw files). C99, no dependencies.
 *
 *     cw_node *doc = cw_confile("main.cw");
 *     const char *name = cw_get(cw_select(doc, "#Main/#User Section"), "name");
 *     const char *email = cw_get_deep(doc, "email");          // selectorAll()[">email"]
 *     cw_set(cw_get_by_id(doc, "User Section"), "name", "ann");
 *     cw_save(doc, NULL);                                     // back to main.cw
 *     cw_free(doc);
 *
 * Strings are UTF-8 and owned by the document; they stay valid until the node changes or the
 * document is freed. Functions that return a node return NULL when nothing matches. See
 * https://github.com/pmorphia/cogwheel for the format. */
#ifndef COGWHEEL_H
#define COGWHEEL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COGWHEEL_VERSION "1.0.0"

typedef enum { CW_DOCUMENT, CW_SECTION, CW_ENTRY, CW_TEXT } cw_kind;
typedef struct cw_node cw_node;

/* Reading. cw_confile returns NULL when the file cannot be read (errno says why). */
cw_node *cw_parse(const char *text, size_t length);
cw_node *cw_confile(const char *path);
void cw_free(cw_node *document);

/* What a node is. */
cw_kind cw_kind_of(const cw_node *node);
const char *cw_name(const cw_node *node);   /* a section's heading text or an entry's key */
const char *cw_value(const cw_node *node);  /* an entry's value, "" otherwise */
int cw_level(const cw_node *node);          /* a section's heading level, 0 for the document */
size_t cw_count(const cw_node *node);       /* children */
cw_node *cw_child(const cw_node *node, size_t index);
cw_node *cw_parent(const cw_node *node);

/* Selectors: "#Name" is a section, ">key" or "key" an entry. cw_find searches the direct
 * children, cw_find_deep every level (selectorAll), cw_get_by_id a section at any depth.
 * cw_select follows a path of selectors separated by '/': "#Main/#User Section/>name". */
cw_node *cw_find(const cw_node *node, const char *selector);
cw_node *cw_find_deep(const cw_node *node, const char *selector);
cw_node *cw_get_by_id(const cw_node *node, const char *id);
cw_node *cw_select(const cw_node *node, const char *path);

/* Entry values of a section (NULL / the fallback when the entry is missing). cw_get_deep looks
 * at every level. The converting getters return 0 on success and -1 when the entry is missing
 * or its value does not convert. */
const char *cw_get(const cw_node *section, const char *key);
const char *cw_get_or(const cw_node *section, const char *key, const char *fallback);
const char *cw_get_deep(const cw_node *node, const char *key);
int cw_get_int(const cw_node *section, const char *key, long long *out);
int cw_get_double(const cw_node *section, const char *key, double *out);
int cw_get_bool(const cw_node *section, const char *key, int *out);
/* A comma-separated value, optionally in [brackets]: a malloc'd array of malloc'd strings,
 * terminated by NULL (free it with cw_free_list), or NULL when the entry is missing. */
char **cw_get_list(const cw_node *section, const char *key, size_t *count);
void cw_free_list(char **list);

/* The text directly in a section (paragraphs joined by a blank line); malloc'd. */
char *cw_text(const cw_node *section);

/* Changes. cw_set changes an entry or adds it after the section's last entry or text; it
 * returns 0, or -1 when out of memory. cw_add_section returns the new subsection. cw_remove
 * removes the first direct child a selector matches and returns 1, or 0 when none does. */
int cw_set(cw_node *section, const char *key, const char *value);
cw_node *cw_add_section(cw_node *section, const char *name);
int cw_add_text(cw_node *section, const char *text);
int cw_remove(cw_node *section, const char *selector);

/* Writing: the .cw text of a node (malloc'd), as read apart from changes. cw_save writes a
 * document to path, or to the file it was read from when path is NULL; 0 or -1. */
char *cw_source(const cw_node *node);
int cw_save(const cw_node *document, const char *path);

/* How values are written and read (malloc'd). */
char *cw_quote(const char *value);
char *cw_unquote(const char *text);

#ifdef __cplusplus
}
#endif
#endif
