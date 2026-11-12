/* Tests of the C API: cc -std=c99 cogwheel.c test.c && ./a.out */
#include "cogwheel.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *MAIN = "Edit fields after >\n\n# Main\n\n## User Section\n\nAbout users.\n\n> name = 123\n\n> email = user@example.com\n";

static void check_source(const cw_node *doc, const char *want) {
    char *s = cw_source(doc);
    if (strcmp(s, want) != 0) {
        fprintf(stderr, "source differs:\n%s\n--- want ---\n%s\n", s, want);
        exit(1);
    }
    free(s);
}

int main(void) {
    cw_node *doc = cw_parse(MAIN, strlen(MAIN));
    assert(strcmp(cw_get(cw_select(doc, "#Main/#User Section"), "name"), "123") == 0);
    assert(strcmp(cw_value(cw_select(doc, "#Main/#User Section/>email")), "user@example.com") == 0);
    assert(strcmp(cw_get(cw_find(cw_get_by_id(doc, "Main"), "#User Section"), "name"), "123") == 0);
    assert(strcmp(cw_get_deep(doc, ">name"), "123") == 0);
    assert(cw_get(doc, "name") == NULL && cw_find(doc, "#Nope") == NULL);
    assert(strcmp(cw_get_or(doc, "name", "x"), "x") == 0);
    long long i;
    assert(cw_get_int(cw_get_by_id(doc, "User Section"), "name", &i) == 0 && i == 123);
    assert(cw_get_int(cw_get_by_id(doc, "User Section"), "email", &i) == -1);
    char *text = cw_text(cw_get_by_id(doc, "User Section"));
    assert(strcmp(text, "About users.") == 0);
    free(text);
    check_source(doc, MAIN);

    cw_node *user = cw_get_by_id(doc, "User Section");
    assert(cw_set(user, "name", "danny") == 0);
    assert(cw_set(user, "age", "3") == 0);
    assert(cw_set(user, "motto", "  spaced ") == 0);
    check_source(doc, "Edit fields after >\n\n# Main\n\n## User Section\n\nAbout users.\n\n> name = danny\n\n> email = user@example.com\n\n> age = 3\n\n> motto = \"  spaced \"\n");
    cw_free(doc);

    const char *a = "# A\n\n> x = 1\n";
    doc = cw_parse(a, strlen(a));
    cw_node *b = cw_add_section(doc, "B");
    assert(cw_set(b, "y", "2") == 0 && cw_add_text(b, "About B.") == 0);
    assert(cw_set(cw_add_section(cw_find(doc, "#A"), "Inner"), "z", "true") == 0);
    check_source(doc, "# A\n\n> x = 1\n\n## Inner\n\n> z = true\n\n# B\n\n> y = 2\n\nAbout B.\n");
    int flag;
    assert(cw_get_bool(cw_get_by_id(doc, "Inner"), "z", &flag) == 0 && flag == 1);
    assert(cw_remove(doc, "#B") == 1 && cw_remove(doc, "#B") == 0);
    cw_free(doc);

    const char *v = "> n = -1_500\n\n> d = 2.5e1\n\n> l = [x, \"y, z\"]\n";
    doc = cw_parse(v, strlen(v));
    double d;
    size_t count;
    assert(cw_get_int(doc, "n", &i) == 0 && i == -1500);
    assert(cw_get_double(doc, "d", &d) == 0 && d == 25.0);
    char **list = cw_get_list(doc, "l", &count);
    assert(count == 2 && strcmp(list[0], "x") == 0 && strcmp(list[1], "y, z") == 0 && list[2] == NULL);
    cw_free_list(list);
    cw_free(doc);
    puts("cogwheel C: all tests passed");
    return 0;
}
