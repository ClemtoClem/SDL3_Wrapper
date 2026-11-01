#include "test.hpp"

int main(int argc, char** argv) {
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--filter" && i + 1 < argc) filter = argv[++i];
    }
    int failed = 0, skipped = 0, total = 0;
    for (auto& tc : test_registry()) {
        if (!filter.empty() && tc.name.find(filter) == std::string::npos) continue;
        ++total;
        int before = test_failures();
        test_skipped() = false;
        std::printf("  \033[0;33m[ RUN  ]\033[0m %s", tc.name.c_str());
        std::fflush(stdout);
        tc.fn();
        if (test_failures() > before) {
            std::printf("\r  \033[1;31m[ FAIL ]\033[0m %s\n", tc.name.c_str());
            ++failed;
        } else if (test_skipped()) {
            std::printf("\r  \033[1;33m[ SKIP ]\033[0m %s\n", tc.name.c_str());
            ++skipped;
        } else {
            std::printf("\r  \033[1;32m[ PASS ]\033[0m %s\n", tc.name.c_str());
        }
    }
    std::printf("\n%d/%d tests passed", total - failed - skipped, total);
    if (skipped) std::printf(", %d skipped", skipped);
    std::printf("\n");
    if (failed > 0) return 1;
    return (total > 0 && skipped == total) ? 77 : 0;
}
