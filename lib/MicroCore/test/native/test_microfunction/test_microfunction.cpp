#ifdef NATIVE_TEST

#include <unity.h>
#include <utility>
#include <MicroFunction.h>

using namespace microcore;

// ==== Size Tests ====

void test_size_zero_is_pointer_size() {
    TEST_ASSERT_EQUAL(sizeof(void*), sizeof(MicroFunction<void(), 0>));
    TEST_ASSERT_EQUAL(sizeof(void*), sizeof(MicroFunction<int(int, int), 0>));
}

void test_size_with_storage() {
    // Storage + invoke pointer
    size_t expected = 8 + sizeof(void*);  // MaxSize=8 + function pointer
    TEST_ASSERT_EQUAL(expected, sizeof(MicroFunction<void(), 8>));
}

void test_fnptr_alias_is_zero_size() {
    TEST_ASSERT_EQUAL(sizeof(void*), sizeof(FnPtr<void()>));
}

// ==== Construction Tests ====

void test_default_construct_is_empty() {
    MicroFunction<void(), 0> fn0;
    MicroFunction<void(), 8> fn8;

    TEST_ASSERT_FALSE(static_cast<bool>(fn0));
    TEST_ASSERT_FALSE(static_cast<bool>(fn8));
}

void test_nullptr_construct() {
    MicroFunction<void(), 0> fn0 = nullptr;
    MicroFunction<void(), 8> fn8 = nullptr;

    TEST_ASSERT_FALSE(static_cast<bool>(fn0));
    TEST_ASSERT_FALSE(static_cast<bool>(fn8));
}

static int g_called = 0;
static void free_function() { g_called++; }

void test_function_pointer_construct() {
    MicroFunction<void(), 0> fn = free_function;
    TEST_ASSERT_TRUE(static_cast<bool>(fn));

    g_called = 0;
    fn();
    TEST_ASSERT_EQUAL(1, g_called);
}

void test_non_capturing_lambda_zero_storage() {
    MicroFunction<int(int), 0> fn = [](int x) { return x * 2; };
    TEST_ASSERT_TRUE(static_cast<bool>(fn));
    TEST_ASSERT_EQUAL(10, fn(5));
}

void test_non_capturing_lambda_with_storage() {
    MicroFunction<int(int), 8> fn = [](int x) { return x * 3; };
    TEST_ASSERT_TRUE(static_cast<bool>(fn));
    TEST_ASSERT_EQUAL(15, fn(5));
}

void test_capturing_lambda() {
    int multiplier = 4;
    MicroFunction<int(int), 8> fn = [multiplier](int x) { return x * multiplier; };
    TEST_ASSERT_TRUE(static_cast<bool>(fn));
    TEST_ASSERT_EQUAL(20, fn(5));
}

void test_capturing_pointer() {
    int value = 100;
    int* ptr = &value;
    MicroFunction<int(), sizeof(void*)> fn = [ptr]() { return *ptr; };

    TEST_ASSERT_EQUAL(100, fn());

    value = 200;
    TEST_ASSERT_EQUAL(200, fn());
}

// ==== Invocation Tests ====

static int g_sum = 0;
static void add_to_sum(int x) { g_sum += x; }

void test_invoke_void_function() {
    MicroFunction<void(int), 0> fn = add_to_sum;
    g_sum = 0;

    fn(10);
    fn(20);

    TEST_ASSERT_EQUAL(30, g_sum);
}

void test_invoke_returning_function() {
    MicroFunction<int(int, int), 0> fn = [](int a, int b) { return a + b; };
    TEST_ASSERT_EQUAL(7, fn(3, 4));
}

void test_invoke_with_captured_state() {
    int counter = 0;
    int* ptr = &counter;

    MicroFunction<void(), sizeof(void*)> fn = [ptr]() { (*ptr)++; };

    fn();
    fn();
    fn();

    TEST_ASSERT_EQUAL(3, counter);
}

// ==== Copy/Move Tests ====

void test_copy_zero_storage() {
    MicroFunction<int(int), 0> fn1 = [](int x) { return x * 2; };
    MicroFunction<int(int), 0> fn2 = fn1;

    TEST_ASSERT_EQUAL(10, fn1(5));
    TEST_ASSERT_EQUAL(10, fn2(5));
}

void test_copy_with_storage() {
    int val = 5;
    MicroFunction<int(), 8> fn1 = [val]() { return val; };
    MicroFunction<int(), 8> fn2 = fn1;

    TEST_ASSERT_EQUAL(5, fn1());
    TEST_ASSERT_EQUAL(5, fn2());
}

void test_move_zero_storage() {
    MicroFunction<int(int), 0> fn1 = [](int x) { return x * 3; };
    MicroFunction<int(int), 0> fn2 = std::move(fn1);

    TEST_ASSERT_EQUAL(15, fn2(5));
}

void test_move_with_storage() {
    int val = 7;
    MicroFunction<int(), 8> fn1 = [val]() { return val; };
    MicroFunction<int(), 8> fn2 = std::move(fn1);

    TEST_ASSERT_EQUAL(7, fn2());
}

// ==== Assignment Tests ====

void test_assign_nullptr() {
    MicroFunction<void(), 0> fn = free_function;
    TEST_ASSERT_TRUE(static_cast<bool>(fn));

    fn = nullptr;
    TEST_ASSERT_FALSE(static_cast<bool>(fn));
}

void test_assign_new_function() {
    MicroFunction<int(int), 0> fn = [](int x) { return x; };
    TEST_ASSERT_EQUAL(5, fn(5));

    fn = [](int x) { return x * x; };
    TEST_ASSERT_EQUAL(25, fn(5));
}

void test_clear() {
    MicroFunction<void(), 0> fn0 = free_function;
    MicroFunction<void(), 8> fn8 = []() {};

    fn0.clear();
    fn8.clear();

    TEST_ASSERT_FALSE(static_cast<bool>(fn0));
    TEST_ASSERT_FALSE(static_cast<bool>(fn8));
}

// ==== Target Tests (zero storage only) ====

void test_target_returns_function_pointer() {
    MicroFunction<void(), 0> fn = free_function;
    TEST_ASSERT_EQUAL_PTR(free_function, fn.target());
}

void test_target_nullptr_when_empty() {
    MicroFunction<void(), 0> fn;
    TEST_ASSERT_NULL(fn.target());
}

// ==== Edge Cases ====

void test_multiple_args() {
    MicroFunction<int(int, int, int, int), 0> fn = [](int a, int b, int c, int d) {
        return a + b + c + d;
    };
    TEST_ASSERT_EQUAL(10, fn(1, 2, 3, 4));
}

void test_capture_multiple_values() {
    int a = 1, b = 2;
    MicroFunction<int(), 16> fn = [a, b]() { return a + b; };
    TEST_ASSERT_EQUAL(3, fn());
}

void test_reassign_different_captures() {
    int x = 10;
    int y = 20;

    MicroFunction<int(), 8> fn = [x]() { return x; };
    TEST_ASSERT_EQUAL(10, fn());

    fn = [y]() { return y; };
    TEST_ASSERT_EQUAL(20, fn());
}

// ==== Type Alias Tests ====

void test_fnptr_alias() {
    FnPtr<int(int)> fn = [](int x) { return x * 2; };
    TEST_ASSERT_EQUAL(10, fn(5));
}

void test_fn_alias() {
    int val = 3;
    Fn<int()> fn = [val]() { return val; };
    TEST_ASSERT_EQUAL(3, fn());
}

void test_fn16_alias() {
    int a = 1, b = 2, c = 3;
    Fn16<int()> fn = [a, b, c]() { return a + b + c; };
    TEST_ASSERT_EQUAL(6, fn());
}

// ==== Setup/Teardown ====

void setUp(void) {
    g_called = 0;
    g_sum = 0;
}

void tearDown(void) {}

// ==== Main ====

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Size tests
    RUN_TEST(test_size_zero_is_pointer_size);
    RUN_TEST(test_size_with_storage);
    RUN_TEST(test_fnptr_alias_is_zero_size);

    // Construction tests
    RUN_TEST(test_default_construct_is_empty);
    RUN_TEST(test_nullptr_construct);
    RUN_TEST(test_function_pointer_construct);
    RUN_TEST(test_non_capturing_lambda_zero_storage);
    RUN_TEST(test_non_capturing_lambda_with_storage);
    RUN_TEST(test_capturing_lambda);
    RUN_TEST(test_capturing_pointer);

    // Invocation tests
    RUN_TEST(test_invoke_void_function);
    RUN_TEST(test_invoke_returning_function);
    RUN_TEST(test_invoke_with_captured_state);

    // Copy/Move tests
    RUN_TEST(test_copy_zero_storage);
    RUN_TEST(test_copy_with_storage);
    RUN_TEST(test_move_zero_storage);
    RUN_TEST(test_move_with_storage);

    // Assignment tests
    RUN_TEST(test_assign_nullptr);
    RUN_TEST(test_assign_new_function);
    RUN_TEST(test_clear);

    // Target tests
    RUN_TEST(test_target_returns_function_pointer);
    RUN_TEST(test_target_nullptr_when_empty);

    // Edge cases
    RUN_TEST(test_multiple_args);
    RUN_TEST(test_capture_multiple_values);
    RUN_TEST(test_reassign_different_captures);

    // Type alias tests
    RUN_TEST(test_fnptr_alias);
    RUN_TEST(test_fn_alias);
    RUN_TEST(test_fn16_alias);

    return UNITY_END();
}

#endif // NATIVE_TEST