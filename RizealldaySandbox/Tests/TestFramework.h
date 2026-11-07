#pragma once
// Tiny self-test framework for `RizealldaySandbox.exe --run-tests [filter]` (editor builds only).
// Tests are free functions registered at static-initialisation time; see EditorSuite_Tests.cpp.
#ifdef USE_IMGUI

#include <cmath>
#include <string>
#include <vector>

class EditorSuite;

struct TestContext {
	std::string name;
	int failures = 0;
	std::vector<std::string> messages;

	void Fail(const char* file, int line, const std::string& text)
	{
		// Keep only the file name: the full build path is noise in the report
		const char* base = file;
		for (const char* p = file; *p; ++p) {
			if (*p == '\\' || *p == '/') base = p + 1;
		}
		++failures;
		messages.push_back(std::string(base) + ":" + std::to_string(line) + ": " + text);
	}
};

struct TestCase {
	const char* name;
	void (*fn)(EditorSuite&, TestContext&);
};

inline std::vector<TestCase>& GetTestRegistry()
{
	static std::vector<TestCase> registry;
	return registry;
}

struct TestRegistrar {
	TestRegistrar(const char* name, void (*fn)(EditorSuite&, TestContext&))
	{
		GetTestRegistry().push_back({ name, fn });
	}
};

/// TEST_CASE(Suite, Name) { ... } defines the test "Suite.Name". Inside the body `s` is the EditorSuite
/// and `ctx` the TestContext (the CHECK macros use `ctx`).
#define TEST_CASE(Suite, Name)                                                                       \
	static void Test_##Suite##_##Name([[maybe_unused]] EditorSuite& s, [[maybe_unused]] TestContext& ctx); \
	static const TestRegistrar sRegistrar_##Suite##_##Name(#Suite "." #Name, &Test_##Suite##_##Name);      \
	static void Test_##Suite##_##Name([[maybe_unused]] EditorSuite& s, [[maybe_unused]] TestContext& ctx)

/// Records a failure and continues.
#define CHECK(expr)                                                         \
	do {                                                                    \
		if (!(expr)) ctx.Fail(__FILE__, __LINE__, "CHECK(" #expr ") failed"); \
	} while (0)

/// Same as CHECK with an extra explanation (e.g. the actual values).
#define CHECK_MSG(expr, text)                                                                \
	do {                                                                                     \
		if (!(expr)) ctx.Fail(__FILE__, __LINE__, std::string("CHECK(" #expr ") failed: ") + (text)); \
	} while (0)

/// Records a failure and returns from the test.
#define REQUIRE(expr)                                                         \
	do {                                                                      \
		if (!(expr)) {                                                        \
			ctx.Fail(__FILE__, __LINE__, "REQUIRE(" #expr ") failed");        \
			return;                                                           \
		}                                                                     \
	} while (0)

/// Same as REQUIRE with an extra explanation.
#define REQUIRE_MSG(expr, text)                                                                  	do {                                                                                         		if (!(expr)) {                                                                           			ctx.Fail(__FILE__, __LINE__, std::string("REQUIRE(" #expr ") failed: ") + (text));    			return;                                                                              		}                                                                                        	} while (0)

/// Float comparison with an absolute tolerance.
#define CHECK_NEAR(a, b, eps)                                                                                  \
	do {                                                                                                       \
		const double checkA_ = static_cast<double>(a);                                                         \
		const double checkB_ = static_cast<double>(b);                                                         \
		if (!(std::fabs(checkA_ - checkB_) <= static_cast<double>(eps))) {                                     \
			ctx.Fail(__FILE__, __LINE__, "CHECK_NEAR(" #a ", " #b ") failed: " + std::to_string(checkA_) +     \
				" vs " + std::to_string(checkB_));                                                             \
		}                                                                                                      \
	} while (0)

#endif // USE_IMGUI
