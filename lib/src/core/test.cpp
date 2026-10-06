// Définitions de core/test.hpp
#include "core/test.hpp"

namespace test {

// ── TestState ────────────────────────────────────────────────────────────────

TestState & TestState::Get() {
	static TestState instance;
	return instance;
}

// ── Registry ─────────────────────────────────────────────────────────────────

Registry & Registry::Get() {
	static Registry instance;
	return instance;
}

void ReportFailure(const char *file, int line, const char *cond) {
	TestState::Get().currentTestFails++;
	std::cerr << file << ":" << line << ": " << COLOR_RED << "Failure\n" << COLOR_RESET;
	std::cerr << "  Condition failed: " << cond << "\n";
}

int RunAllTests() {
	auto &registry = Registry::Get();
	auto &state = TestState::Get();
	const auto &tests = registry.GetTests();

	std::cout << "\nRunning " << tests.size() << " tests.\n";

	for (const auto &test : tests) {
		state.currentTestFails = 0;

		// Exécution du test
		test.runFunc();

		if (state.currentTestFails == 0) {
			std::cout << COLOR_GREEN << "[  OK  ] " << COLOR_RESET << test.suiteName << "::" << test.testName << "\n";
			state.totalPassed++;
		} else {
			std::cout << COLOR_RED << "[ FAIL ] " << COLOR_RESET << test.suiteName << "::" << test.testName << "\n";
			state.totalFailed++;
		}
	}

	std::cout << "\nTest Results:\n";
	std::cout << COLOR_GREEN << "Passed: " << state.totalPassed << COLOR_RESET << "\n";
	if (state.totalFailed > 0) {
		std::cout << COLOR_RED << "Failed: " << state.totalFailed << COLOR_RESET << "\n";
		return 1; // Retourne un code d'erreur au système d'exploitation
	}
	return 0; // Succès
}

} // namespace test
