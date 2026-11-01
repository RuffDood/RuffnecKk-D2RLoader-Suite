#include "helper_failure_diagnostics.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    using namespace RuffnecKk::MapSense::Detail;
    auto check = [](bool passed) { if (!passed) std::cerr << "helper failure diagnostics regression\n"; return passed; };
    const auto launch = FormatHelperFailure({.stage="create-process",.seed=1945484404,
        .difficulty=1,.act=0,.level=1,.windowsError=2,.command="helper atlas-binary",
        .workingDirectory="cache"});
    if (!check(launch.find("stage=create-process") != std::string::npos
        && launch.find("win32=2") != std::string::npos
        && launch.find("exit=unavailable") != std::string::npos)) return EXIT_FAILURE;
    std::string output = "MS1 H 3 seed difficulty act level\n";
    output.append(15000, 'R'); output += "\r\nerror: ActiveTableReadFailed\t\x1B";
    const auto failure = FormatHelperFailure({.stage="process-exit",.exitCodeKnown=true,
        .exitCode=0xC0000005U,.output=output});
    if (!check(failure.find("0xc0000005") != std::string::npos
        && failure.find("MS1 H 3") != std::string::npos
        && failure.find("error: ActiveTableReadFailed") != std::string::npos
        && failure.find("[truncated]") != std::string::npos
        && failure.size() < 2300U && failure.find('\n') == std::string::npos
        && failure.find('\r') == std::string::npos && failure.find('\x1B') == std::string::npos)) return EXIT_FAILURE;
    const auto timeout = FormatHelperFailure({.stage="timeout",.exitCodeKnown=true,
        .exitCode=1460,.timedOut=true});
    if (!check(timeout.find("timeout=yes") != std::string::npos)) return EXIT_FAILURE;
    std::cout << "helper failure diagnostics: PASS\n";
    return EXIT_SUCCESS;
}
