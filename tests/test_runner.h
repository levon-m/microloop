/**
 * test_runner.h - Simple on-device test framework
 *
 * DESIGN:
 * - Runs tests directly on Teensy (no external test harness needed)
 * - Uses serial output for test results
 * - Integrates with Trace utility for detailed debugging
 * - Minimal overhead (~1KB code)
 *
 * USAGE:
 *   TEST("My test name") {
 *       ASSERT_EQ(actual, expected);
 *       ASSERT_TRUE(condition);
 *   }
 *
 *   void setup() {
 *       Serial.begin(115200);
 *       RUN_ALL_TESTS();
 *   }
 */

#pragma once

#include <Arduino.h>

// Test statistics
static int g_testsPassed = 0;
static int g_testsFailed = 0;
static const char* g_currentTest = nullptr;

// ANSI color codes for serial output (if terminal supports it)
#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET  "\033[0m"

/**
 * Begin a test
 */
#define TEST(name) \
    static void test_##name(); \
    static struct TestRegistrar_##name { \
        TestRegistrar_##name() { \
            TestRunner::registerTest(#name, test_##name); \
        } \
    } testRegistrar_##name; \
    static void test_##name()

/**
 * Helper functions to print values (handles enums by casting to int)
 */
template<typename T>
typename std::enable_if<std::is_enum<T>::value, void>::type
printValue(T value) {
    Serial.print(static_cast<int>(value));
}

template<typename T>
typename std::enable_if<!std::is_enum<T>::value, void>::type
printValue(T value) {
    Serial.print(value);
}

/**
 * Assertions
 */
#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            Serial.print(COLOR_RED "  FAIL: " COLOR_RESET); \
            Serial.print(__FILE__); \
            Serial.print(":"); \
            Serial.print(__LINE__); \
            Serial.print(" - Condition failed: " #condition); \
            Serial.println(); \
            g_testsFailed++; \
            return; \
        } \
    } while(0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#define ASSERT_EQ(actual, expected) \
    do { \
        auto _actual = (actual); \
        auto _expected = (expected); \
        if (_actual != _expected) { \
            Serial.print(COLOR_RED "  FAIL: " COLOR_RESET); \
            Serial.print(__FILE__); \
            Serial.print(":"); \
            Serial.print(__LINE__); \
            Serial.print(" - Expected "); \
            printValue(_expected); \
            Serial.print(", got "); \
            printValue(_actual); \
            Serial.println(); \
            g_testsFailed++; \
            return; \
        } \
    } while(0)

#define ASSERT_NE(actual, expected) \
    do { \
        auto _actual = (actual); \
        auto _expected = (expected); \
        if (_actual == _expected) { \
            Serial.print(COLOR_RED "  FAIL: " COLOR_RESET); \
            Serial.print(__FILE__); \
            Serial.print(":"); \
            Serial.print(__LINE__); \
            Serial.print(" - Expected values to differ, both were "); \
            printValue(_actual); \
            Serial.println(); \
            g_testsFailed++; \
            return; \
        } \
    } while(0)

#define ASSERT_LT(actual, expected) \
    do { \
        auto _actual = (actual); \
        auto _expected = (expected); \
        if (!(_actual < _expected)) { \
            Serial.print(COLOR_RED "  FAIL: " COLOR_RESET); \
            Serial.print(__FILE__); \
            Serial.print(":"); \
            Serial.print(__LINE__); \
            Serial.print(" - Expected "); \
            printValue(_actual); \
            Serial.print(" < "); \
            printValue(_expected); \
            Serial.println(); \
            g_testsFailed++; \
            return; \
        } \
    } while(0)

#define ASSERT_GT(actual, expected) \
    do { \
        auto _actual = (actual); \
        auto _expected = (expected); \
        if (!(_actual > _expected)) { \
            Serial.print(COLOR_RED "  FAIL: " COLOR_RESET); \
            Serial.print(__FILE__); \
            Serial.print(":"); \
            Serial.print(__LINE__); \
            Serial.print(" - Expected "); \
            printValue(_actual); \
            Serial.print(" > "); \
            printValue(_expected); \
            Serial.println(); \
            g_testsFailed++; \
            return; \
        } \
    } while(0)

#define ASSERT_NEAR(actual, expected, tolerance) \
    do { \
        auto _actual = (actual); \
        auto _expected = (expected); \
        auto _diff = (_actual > _expected) ? (_actual - _expected) : (_expected - _actual); \
        if (_diff > (tolerance)) { \
            Serial.print(COLOR_RED "  FAIL: " COLOR_RESET); \
            Serial.print(__FILE__); \
            Serial.print(":"); \
            Serial.print(__LINE__); \
            Serial.print(" - Expected "); \
            printValue(_actual); \
            Serial.print(" ≈ "); \
            printValue(_expected); \
            Serial.print(" (tolerance "); \
            printValue(tolerance); \
            Serial.println(")"); \
            g_testsFailed++; \
            return; \
        } \
    } while(0)

/**
 * Test runner
 */
class TestRunner {
public:
    using TestFunc = void (*)();

    static constexpr int MAX_TESTS = 50;

    static void registerTest(const char* name, TestFunc func) {
        if (s_numTests < MAX_TESTS) {
            s_tests[s_numTests].name = name;
            s_tests[s_numTests].func = func;
            s_numTests++;
        }
    }

    static void runAll() {
        Serial.println();
        Serial.println("========================================");
        Serial.println("        MicroLoop Test Suite");
        Serial.println("========================================");
        Serial.println();

        uint32_t startTime = millis();

        for (int i = 0; i < s_numTests; i++) {
            g_currentTest = s_tests[i].name;
            Serial.print("[ RUN      ] ");
            Serial.println(s_tests[i].name);

            int failedBefore = g_testsFailed;
            s_tests[i].func();

            if (g_testsFailed == failedBefore) {
                Serial.print(COLOR_GREEN "[       OK ] " COLOR_RESET);
                Serial.println(s_tests[i].name);
                g_testsPassed++;
            } else {
                Serial.print(COLOR_RED "[  FAILED  ] " COLOR_RESET);
                Serial.println(s_tests[i].name);
            }
        }

        uint32_t duration = millis() - startTime;

        Serial.println();
        Serial.println("========================================");
        Serial.print("Tests run: ");
        Serial.println(s_numTests);
        Serial.print(COLOR_GREEN "Passed: ");
        Serial.print(g_testsPassed);
        Serial.println(COLOR_RESET);
        if (g_testsFailed > 0) {
            Serial.print(COLOR_RED "Failed: ");
            Serial.print(g_testsFailed);
            Serial.println(COLOR_RESET);
        }
        Serial.print("Duration: ");
        Serial.print(duration);
        Serial.println(" ms");
        Serial.println("========================================");
        Serial.println();

        if (g_testsFailed == 0) {
            Serial.println(COLOR_GREEN "✓ All tests passed!" COLOR_RESET);
        } else {
            Serial.println(COLOR_RED "✗ Some tests failed" COLOR_RESET);
        }
    }

private:
    struct Test {
        const char* name;
        TestFunc func;
    };

    static Test s_tests[MAX_TESTS];
    static int s_numTests;
};

// Static member definitions
TestRunner::Test TestRunner::s_tests[TestRunner::MAX_TESTS];
int TestRunner::s_numTests = 0;

// Convenience macro
#define RUN_ALL_TESTS() TestRunner::runAll()
