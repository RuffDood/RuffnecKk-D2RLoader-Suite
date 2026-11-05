#include "object_catalog.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
using namespace AutoInteract;
int main(int argc, char** argv) {
    ObjectCatalog catalog; std::string error;
    CHECK(catalog.Parse("Class\tInitFn\tOperateFn\tSubClass\tSelectable0\tIsDoor\nNull\t0\t0\t0\t0\t0\nLog\t0\t14\t0\t1\t0\nExpansion\t\t\t\t\t\nBasket1\t2\t3\t8\t1\t0\n", error));
    std::array<std::uint8_t, 0x168> record{};
    record[0] = 1; record[0x15E] = 14; record[0x85] = 1; // Real runtime linker ID, no ASCII Class string.
    CHECK(catalog.Key(1, 3, record) == "Log"); CHECK(catalog.Key(1, 4, record).empty());
    record[0x15E] = 68; CHECK(catalog.Key(1, 3, record).empty()); record[0x15E] = 14;
    CHECK(catalog.Key(3, 3, record).empty());
    CHECK(!catalog.Parse("Class\tInitFn\nLog\t0\n", error)); CHECK(catalog.Size() == 0);
    if (argc > 1 && std::string_view(argv[1]).ends_with(".txt")) {
        std::ifstream file(argv[1], std::ios::binary); std::string text{std::istreambuf_iterator<char>(file), {}};
        CHECK(!text.empty() && catalog.Parse(text, error));
        std::cout << "Actual TXT catalog parsed: " << catalog.Size() << " rows\n";
    }
    std::cout << "linker-ID rows, active tuple checks, malformed and source catalogs passed\n";
}
