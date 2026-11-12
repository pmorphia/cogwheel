// Prints a .cw file's tree for the conformance tests (../tests).
#include "cogwheel.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

static void walk(const cogwheel::node &n, const std::string &path) {
    for (auto &c : n.children) {
        if (c->kind != cogwheel::kind::section && c->kind != cogwheel::kind::entry)
            continue;
        auto p = path.empty() ? c->name : path + "/" + c->name;
        if (c->kind == cogwheel::kind::section) {
            std::cout << "section " << c->level << " " << p << "\n";
            walk(*c, p);
        } else {
            std::cout << "entry " << p << " = " << cogwheel::quote(c->value) << "\n";
            if (c->name.rfind("list", 0) == 0) {
                std::cout << "  list: ";
                auto items = n.get_list(c->name);
                for (size_t i = 0; i < items.size(); ++i)
                    std::cout << (i ? " | " : "") << items[i];
                std::cout << "\n";
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    auto doc = cogwheel::confile(argv[1]);
    walk(doc, "");
    std::ifstream f(argv[1], std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    std::cout << "roundtrip " << (doc.source() == s.str() ? "true" : "false") << "\n";
}
