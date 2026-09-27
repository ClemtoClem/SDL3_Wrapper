#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Core du Framework (Mécanique interne)
// ---------------------------------------------------------------------------
namespace test {

// Couleurs ANSI pour la console (style cargo test / gtest)
constexpr const char *COLOR_GREEN = "\033[32m";
constexpr const char *COLOR_RED = "\033[31m";
constexpr const char *COLOR_RESET = "\033[0m";

// Structure représentant un test
struct TestCase {
	std::string suiteName;
	std::string testName;
	std::function<void()> runFunc;
};

// État global du test en cours (pour remonter les erreurs des EXPECT/ASSERT)
struct TestState {
	int currentTestFails = 0;
	int totalPassed = 0;
	int totalFailed = 0;

	static TestState &Get();
};

// Registre global (Singleton) qui stocke tous les tests découverts
class Registry {
private:
	std::vector<TestCase> tests;

public:
	static Registry &Get();
	void Add(const TestCase &test) { tests.push_back(test); }
	const std::vector<TestCase> &GetTests() const { return tests; }
};

// Fonction interne pour formater et signaler une erreur
template <typename T1, typename T2>
inline void ReportFailure(const char *file, int line, const char *cond, const T1 &val1, const T2 &val2, bool isEq) {
	TestState::Get().currentTestFails++;
	std::cerr << file << ":" << line << ": " << COLOR_RED << "Failure\n" << COLOR_RESET;
	if (isEq) {
		std::cerr << "  Expected equality of these values:\n"
				  << "    Actual: " << val1 << "\n"
				  << "    Expected: " << val2 << "\n";
	} else {
		std::cerr << "  Condition failed: " << cond << "\n";
	}
}

void ReportFailure(const char *file, int line, const char *cond);

// Le moteur d'exécution (Le "Runner")
int RunAllTests();
} // namespace test

// ---------------------------------------------------------------------------
// L'API Publique : Les Macros (Style GTest)
// ---------------------------------------------------------------------------

/// Macro magique qui déclare le test et l'auto-enregistre avant le main()
#define TEST(test_suite_name, test_name)                                                                               \
	class TEST_CLASS_##test_suite_name##_##test_name {                                                                 \
	public:                                                                                                            \
		static void Run();                                                                                             \
	};                                                                                                                 \
	namespace {                                                                                                        \
	struct TEST_REGISTER_##test_suite_name##_##test_name {                                                             \
		TEST_REGISTER_##test_suite_name##_##test_name() {                                                              \
			test::Registry::Get().Add(                                                                                 \
				{#test_suite_name, #test_name, &TEST_CLASS_##test_suite_name##_##test_name::Run});                     \
		}                                                                                                              \
	} test_register_instance_##test_suite_name##_##test_name;                                                          \
	}                                                                                                                  \
	void TEST_CLASS_##test_suite_name##_##test_name::Run()

/// Démarre tous les tests enregistrés
#define RUN_ALL_TESTS() test::RunAllTests()

// --- Assertions ---

// EXPECT (Continue l'exécution du test en cas d'échec)
#define EXPECT_TRUE(condition)                                                                                         \
	do {                                                                                                               \
		if (!(condition)) {                                                                                            \
			test::ReportFailure(__FILE__, __LINE__, #condition);                                                      \
		}                                                                                                              \
	} while (0)

#define EXPECT_FALSE(condition)                                                                                        \
	do {                                                                                                               \
		if (condition) {                                                                                               \
			test::ReportFailure(__FILE__, __LINE__, "!(" #condition ")");                                             \
		}                                                                                                              \
	} while (0)

#define EXPECT_EQ(val1, val2)                                                                                          \
	do {                                                                                                               \
		if (!((val1) == (val2))) {                                                                                     \
			test::ReportFailure(__FILE__, __LINE__, #val1 " == " #val2, val1, val2, true);                            \
		}                                                                                                              \
	} while (0)

// ASSERT (Arrête *immédiatement* le test en cours en cas d'échec via un return)
// Note: En gtest, ASSERT utilise des exceptions ou des macros de retour avancées.
// Ici on utilise un 'return' simple pour ne pas alourdir le code.
#define ASSERT_TRUE(condition)                                                                                         \
	do {                                                                                                               \
		if (!(condition)) {                                                                                            \
			test::ReportFailure(__FILE__, __LINE__, #condition);                                                      \
			return;                                                                                                    \
		}                                                                                                              \
	} while (0)

#define ASSERT_FALSE(condition)                                                                                        \
	do {                                                                                                               \
		if (condition) {                                                                                               \
			test::ReportFailure(__FILE__, __LINE__, "!(" #condition ")");                                             \
			return;                                                                                                    \
		}                                                                                                              \
	} while (0)

#define ASSERT_EQ(val1, val2)                                                                                          \
	do {                                                                                                               \
		if (!((val1) == (val2))) {                                                                                     \
			test::ReportFailure(__FILE__, __LINE__, #val1 " == " #val2, val1, val2, true);                            \
			return;                                                                                                    \
		}                                                                                                              \
	} while (0)
