/* Prints a .cw file's tree for the conformance tests (../tests). */
#include "cogwheel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void walk(const cw_node *n, const char *path) {
    for (size_t i = 0; i < cw_count(n); i++) {
        const cw_node *c = cw_child(n, i);
        if (cw_kind_of(c) != CW_SECTION && cw_kind_of(c) != CW_ENTRY)
            continue;
        size_t len = strlen(path) + strlen(cw_name(c)) + 2;
        char *p = malloc(len);
        snprintf(p, len, "%s%s%s", path, *path ? "/" : "", cw_name(c));
        if (cw_kind_of(c) == CW_SECTION) {
            printf("section %d %s\n", cw_level(c), p);
            walk(c, p);
        } else {
            char *q = cw_quote(cw_value(c));
            printf("entry %s = %s\n", p, q);
            free(q);
            if (strncmp(cw_name(c), "list", 4) == 0) {
                char **list = cw_get_list(n, cw_name(c), NULL);
                printf("  list: ");
                for (char **it = list; *it; it++)
                    printf("%s%s", it == list ? "" : " | ", *it);
                printf("\n");
                cw_free_list(list);
            }
        }
        free(p);
    }
}

int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    cw_node *doc = cw_confile(argv[1]);
    if (!doc) {
        perror(argv[1]);
        return 1;
    }
    walk(doc, "");
    FILE *f = fopen(argv[1], "rb");
    static char text[1 << 20];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    char *src = cw_source(doc);
    printf("roundtrip %s\n", strcmp(src, text) == 0 ? "true" : "false");
    free(src);
    cw_free(doc);
    return 0;
}
