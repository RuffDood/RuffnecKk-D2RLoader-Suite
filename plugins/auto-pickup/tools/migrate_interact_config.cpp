#include "auto_interact/settings.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "Usage: migrate_interact_config PICKIT_TOML LEGACY_TOML NEW_OUTPUT\n"; return 2; }
    try {
        namespace fs = std::filesystem;
        if (fs::exists(argv[3]) || fs::file_size(argv[1]) >= 65536 || fs::file_size(argv[2]) >= 65536) return 2;
        std::ifstream pickit(argv[1], std::ios::binary), legacy(argv[2], std::ios::binary);
        if (!pickit || !legacy) return 2;
        std::string original{std::istreambuf_iterator<char>(pickit), {}}, previous{std::istreambuf_iterator<char>(legacy), {}}, output, error;
        if (!AutoInteract::MigrateLegacy(original, previous, output, error) || output.size() >= 65536) { std::cerr << error << '\n'; return 1; }
        std::ofstream file(argv[3], std::ios::binary); file.write(output.data(), static_cast<std::streamsize>(output.size()));
        if (!file) return 1;
        std::cout << "Pickit migration prepared; inputs unchanged.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
