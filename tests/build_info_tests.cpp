#include "build_info.hpp"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    const std::string timestamp = tundraux::build_info::timestamp();
    return expect(!timestamp.empty(), "build timestamp should not be empty") &&
            expect(timestamp.find("__TIMESTAMP__") == std::string::npos, "build timestamp should be generated") &&
            expect(timestamp.find("Unknown") == std::string::npos, "build timestamp should be known")
        ? 0
        : 1;
}
