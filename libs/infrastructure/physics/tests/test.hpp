#pragma once
// Mini-framework de tests généré par cpp-maker.
//
// Deux styles acceptés :
//   TEST(Nom)          { CHECK(x); CHECK_EQ(a, b); REQUIRE(ptr); }
//   TEST(Suite, Nom)   { EXPECT_TRUE(x); EXPECT_EQ(a, b); ASSERT_TRUE(ptr); }
// CHECK/EXPECT_* continuent le test en cas d'échec ; REQUIRE/ASSERT_*
// l'arrêtent (return). SKIP("raison") marque le test comme ignoré (ex. pas
// d'affichage ou de GPU disponible) ; si tous les tests exécutés sont
// ignorés, l'exécutable sort avec le code 77 (test ignoré pour cpp-maker).
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase { std::string name; std::function<void()> fn; };

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> r;
    return r;
}
inline int& test_failures() {
    static int f = 0;
    return f;
}
inline bool& test_skipped() {
    static bool s = false;
    return s;
}

struct TestRegistrar {
    TestRegistrar(const std::string& n, std::function<void()> f) {
        test_registry().push_back({n, std::move(f)});
    }
};

inline void test_report(const char* kind, const char* expr, const char* file, int line) {
    std::fprintf(stderr, "\n  %s failed: %s (%s:%d)", kind, expr, file, line);
    ++test_failures();
}

namespace test {
/// Signale un échec depuis du code de test (compatibilité anciens tests).
inline void ReportFailure(const char* file, int line, const char* cond) {
    test_report("Failure", cond, file, line);
}
} // namespace test

#define TEST_CAT_(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT_(a, b)
#define TEST_1_(name) \
    static void test_fn_##name(); \
    static TestRegistrar test_reg_##name(#name, test_fn_##name); \
    static void test_fn_##name()
#define TEST_2_(suite, name) \
    static void test_fn_##suite##_##name(); \
    static TestRegistrar test_reg_##suite##_##name(#suite "." #name, test_fn_##suite##_##name); \
    static void test_fn_##suite##_##name()
#define TEST_PICK_(_1, _2, M, ...) M
#define TEST(...) TEST_PICK_(__VA_ARGS__, TEST_2_, TEST_1_, unused)(__VA_ARGS__)

#define CHECK(cond) do { \
    if (!(cond)) test_report("CHECK", #cond, __FILE__, __LINE__); \
} while (0)

#define CHECK_EQ(a, b) do { \
    if (!((a) == (b))) test_report("CHECK_EQ", #a " == " #b, __FILE__, __LINE__); \
} while (0)

#define CHECK_NE(a, b) do { \
    if ((a) == (b)) test_report("CHECK_NE", #a " != " #b, __FILE__, __LINE__); \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    if (!(std::fabs(double(a) - double(b)) <= double(eps))) \
        test_report("CHECK_NEAR", #a " ~ " #b, __FILE__, __LINE__); \
} while (0)

#define REQUIRE(cond) do { \
    if (!(cond)) { test_report("REQUIRE", #cond, __FILE__, __LINE__); return; } \
} while (0)

#define REQUIRE_EQ(a, b) do { \
    if (!((a) == (b))) { test_report("REQUIRE_EQ", #a " == " #b, __FILE__, __LINE__); return; } \
} while (0)

#define SKIP(reason) do { \
    std::fprintf(stderr, "\n  skipped: %s", reason); \
    test_skipped() = true; \
    return; \
} while (0)

// Style GTest (compatibilité avec les anciens tests du projet).
#define EXPECT_TRUE(c)   CHECK(c)
#define EXPECT_FALSE(c)  CHECK(!(c))
#define EXPECT_EQ(a, b)  CHECK_EQ(a, b)
#define EXPECT_NE(a, b)  CHECK_NE(a, b)
#define ASSERT_TRUE(c)   REQUIRE(c)
#define ASSERT_FALSE(c)  REQUIRE(!(c))
#define ASSERT_EQ(a, b)  REQUIRE_EQ(a, b)
