#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

auto ReadFile(const char* path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const auto plugin = ReadFile(argv[1]);
    const auto policy = ReadFile(argv[2]);
    const auto consumerApi = ReadFile(argv[3]);
    if (plugin.empty() || policy.empty() || consumerApi.empty()) return 3;
    auto combined = plugin + policy;
    for (const auto* allowed : {
            "RuffnecKkStackManagerGetAutoPickupRouteApi",
            "d2rl-ruffneckk-stack-manager.dll"}) {
        for (auto position = combined.find(allowed);
            position != std::string::npos;
            position = combined.find(allowed)) {
            combined.erase(position, std::char_traits<char>::length(allowed));
        }
    }
    for (const auto* forbidden : {"Stack", "stack", "Quantity", "quantity", "Serialize", "AdvancedStash", "Stash"}) {
        if (combined.find(forbidden) != std::string::npos) {
            std::cerr << "forbidden source symbol: " << forbidden << '\n';
            return 1;
        }
    }
    return 0;
}
