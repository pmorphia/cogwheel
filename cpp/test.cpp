// Tests of the C++ API: c++ -std=c++17 test.cpp && ./a.out
#include "cogwheel.hpp"

#include <cassert>
#include <iostream>

int main() {
    const char *MAIN = "Edit fields after >\n\n# Main\n\n## User Section\n\nAbout users.\n\n> name = 123\n\n> email = user@example.com\n";
    auto doc = cogwheel::parse(MAIN);
    std::string name = doc["#Main"]["#User Section"][">name"];
    assert(name == "123");
    assert(doc.getById("Main")["#User Section"].get("name") == "123");
    assert(doc.selectorAll()[">email"].value == "user@example.com");
    assert(doc.get_by_id("User Section").get_int("name") == 123);
    assert(doc.getById("User Section").text() == "About users.");
    assert(!doc.has(">name") && doc.selectorAll().has(">name"));
    assert(doc.to_map().at("Main.User Section.name") == "123");
    try {
        doc["#Nope"];
        assert(false);
    } catch (const cogwheel::not_found &) {
    }
    assert(doc.source() == MAIN);
    auto &user = doc.getById("User Section");
    user.set("name", "danny").set("age", 3).set("motto", "  spaced ");
    assert(doc.source() == std::string(MAIN).replace(std::string(MAIN).find("123"), 3, "danny") +
                               "\n> age = 3\n\n> motto = \"  spaced \"\n");

    auto two = cogwheel::parse("# A\n\n> x = 1\n");
    two.add_section("B").set("y", "2").add_text("About B.");
    two["#A"].add_section("Inner").set("z", true);
    assert(two.source() == "# A\n\n> x = 1\n\n## Inner\n\n> z = true\n\n# B\n\n> y = 2\n\nAbout B.\n");
    assert(two.getById("Inner").get_bool("z"));
    assert(two.selectorAll().remove(">z") && !two.remove(">z"));
    assert(two.remove("#B") && two.sections().size() == 1);

    auto v = cogwheel::parse("> n = -1_500\n\n> d = 2.5e1\n\n> l = [x, \"y, z\"]\n\n> bad = maybe\n");
    assert(v.get_int("n") == -1500 && v.get_double("d") == 25.0);
    assert((v.get_list("l") == std::vector<std::string>{"x", "y, z"}));
    try {
        v.get_bool("bad");
        assert(false);
    } catch (const cogwheel::bad_value &) {
    }
    std::cout << "cogwheel C++: all tests passed\n";
}
