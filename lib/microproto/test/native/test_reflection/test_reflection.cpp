#ifdef NATIVE_TEST

#include <unity.h>
#include <Reflect.h>
#include <Field.h>
#include <cstring>

using namespace MicroProto::reflect;
using namespace MicroProto;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Test Structs
// ============================================================================

struct Empty {};

struct OneField {
    int x;
};

struct TwoFields {
    int x;
    float y;
};

struct ThreeFields {
    int32_t a;
    uint8_t b;
    float c;
};

struct Position {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct MixedTypes {
    bool flag;
    uint8_t byte_val;
    int32_t int_val;
    float float_val;
};

struct LargeStruct {
    int a, b, c, d, e, f, g, h, i, j;
};

// ============================================================================
// Field Count Tests
// ============================================================================

void test_field_count_one() {
    TEST_ASSERT_EQUAL(1, field_count_v<OneField>);
}

void test_field_count_two() {
    TEST_ASSERT_EQUAL(2, field_count_v<TwoFields>);
}

void test_field_count_three() {
    TEST_ASSERT_EQUAL(3, field_count_v<ThreeFields>);
}

void test_field_count_position() {
    TEST_ASSERT_EQUAL(3, field_count_v<Position>);
}

void test_field_count_mixed() {
    TEST_ASSERT_EQUAL(4, field_count_v<MixedTypes>);
}

void test_field_count_large() {
    TEST_ASSERT_EQUAL(10, field_count_v<LargeStruct>);
}

// ============================================================================
// is_reflectable Tests
// ============================================================================

void test_is_reflectable_simple_struct() {
    TEST_ASSERT_TRUE(is_reflectable_v<Position>);
    TEST_ASSERT_TRUE(is_reflectable_v<ThreeFields>);
}

void test_is_reflectable_not_array() {
    TEST_ASSERT_FALSE(is_reflectable_v<int[5]>);
}

void test_is_reflectable_const() {
    TEST_ASSERT_TRUE(is_reflectable_v<const Position>);
}

// ============================================================================
// to_tuple Tests
// ============================================================================

void test_to_tuple_access() {
    Position pos{10, 20, 30};
    auto t = to_tuple(pos);

    TEST_ASSERT_EQUAL(10, std::get<0>(t));
    TEST_ASSERT_EQUAL(20, std::get<1>(t));
    TEST_ASSERT_EQUAL(30, std::get<2>(t));
}

void test_to_tuple_modify() {
    Position pos{10, 20, 30};
    auto t = to_tuple(pos);

    std::get<0>(t) = 100;
    std::get<1>(t) = 200;

    TEST_ASSERT_EQUAL(100, pos.x);
    TEST_ASSERT_EQUAL(200, pos.y);
    TEST_ASSERT_EQUAL(30, pos.z);
}

void test_to_tuple_mixed_types() {
    MixedTypes m{true, 42, -100, 3.14f};
    auto t = to_tuple(m);

    TEST_ASSERT_EQUAL(true, std::get<0>(t));
    TEST_ASSERT_EQUAL(42, std::get<1>(t));
    TEST_ASSERT_EQUAL(-100, std::get<2>(t));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, std::get<3>(t));
}

// ============================================================================
// field_type_t Tests
// ============================================================================

void test_field_type() {
    static_assert(std::is_same_v<field_type_t<Position, 0>, int32_t>, "Field 0 should be int32_t");
    static_assert(std::is_same_v<field_type_t<Position, 1>, int32_t>, "Field 1 should be int32_t");
    static_assert(std::is_same_v<field_type_t<Position, 2>, int32_t>, "Field 2 should be int32_t");

    static_assert(std::is_same_v<field_type_t<MixedTypes, 0>, bool>, "Field 0 should be bool");
    static_assert(std::is_same_v<field_type_t<MixedTypes, 1>, uint8_t>, "Field 1 should be uint8_t");
    static_assert(std::is_same_v<field_type_t<MixedTypes, 2>, int32_t>, "Field 2 should be int32_t");
    static_assert(std::is_same_v<field_type_t<MixedTypes, 3>, float>, "Field 3 should be float");

    TEST_PASS();
}

// ============================================================================
// get<N> Tests
// ============================================================================

void test_get_by_index() {
    Position pos{100, 200, 300};

    TEST_ASSERT_EQUAL(100, get<0>(pos));
    TEST_ASSERT_EQUAL(200, get<1>(pos));
    TEST_ASSERT_EQUAL(300, get<2>(pos));
}

void test_get_modify() {
    Position pos{0, 0, 0};

    get<0>(pos) = 10;
    get<1>(pos) = 20;
    get<2>(pos) = 30;

    TEST_ASSERT_EQUAL(10, pos.x);
    TEST_ASSERT_EQUAL(20, pos.y);
    TEST_ASSERT_EQUAL(30, pos.z);
}

void test_get_const() {
    const Position pos{1, 2, 3};

    TEST_ASSERT_EQUAL(1, get<0>(pos));
    TEST_ASSERT_EQUAL(2, get<1>(pos));
    TEST_ASSERT_EQUAL(3, get<2>(pos));
}

// ============================================================================
// for_each_field Tests
// ============================================================================

void test_for_each_field_count() {
    Position pos{1, 2, 3};
    int count = 0;

    for_each_field(pos, [&count](auto, auto&) {
        count++;
    });

    TEST_ASSERT_EQUAL(3, count);
}

void test_for_each_field_sum() {
    Position pos{10, 20, 30};
    int sum = 0;

    for_each_field(pos, [&sum](auto, auto& field) {
        sum += field;
    });

    TEST_ASSERT_EQUAL(60, sum);
}

void test_for_each_field_modify() {
    Position pos{1, 2, 3};

    for_each_field(pos, [](auto, auto& field) {
        field *= 10;
    });

    TEST_ASSERT_EQUAL(10, pos.x);
    TEST_ASSERT_EQUAL(20, pos.y);
    TEST_ASSERT_EQUAL(30, pos.z);
}

void test_for_each_field_indexed() {
    Position pos{100, 200, 300};
    int indices[3] = {-1, -1, -1};
    int values[3] = {0, 0, 0};
    int idx = 0;

    for_each_field_indexed(pos, [&](size_t i, auto& field) {
        indices[idx] = static_cast<int>(i);
        values[idx] = field;
        idx++;
    });

    TEST_ASSERT_EQUAL(0, indices[0]);
    TEST_ASSERT_EQUAL(1, indices[1]);
    TEST_ASSERT_EQUAL(2, indices[2]);

    TEST_ASSERT_EQUAL(100, values[0]);
    TEST_ASSERT_EQUAL(200, values[1]);
    TEST_ASSERT_EQUAL(300, values[2]);
}

// ============================================================================
// Field Name Extraction Tests
// ============================================================================

// Helper to compare string_view with C-string
bool sv_equals(std::string_view sv, const char* str) {
    return sv == std::string_view(str);
}

void test_member_name_extraction() {
    // Test that we can extract member names via __PRETTY_FUNCTION__
    constexpr auto name = MicroProto::reflect::detail::member_name_v<&Position::x>;
    TEST_ASSERT_TRUE(sv_equals(name, "x"));
    TEST_ASSERT_EQUAL(1, name.size());
}

void test_member_name_y() {
    constexpr auto name = MicroProto::reflect::detail::member_name_v<&Position::y>;
    TEST_ASSERT_TRUE(sv_equals(name, "y"));
}

void test_member_name_mixed() {
    constexpr auto name_flag = MicroProto::reflect::detail::member_name_v<&MixedTypes::flag>;
    constexpr auto name_byte = MicroProto::reflect::detail::member_name_v<&MixedTypes::byte_val>;
    constexpr auto name_int = MicroProto::reflect::detail::member_name_v<&MixedTypes::int_val>;
    constexpr auto name_float = MicroProto::reflect::detail::member_name_v<&MixedTypes::float_val>;

    TEST_ASSERT_TRUE(sv_equals(name_flag, "flag"));
    TEST_ASSERT_TRUE(sv_equals(name_byte, "byte_val"));
    TEST_ASSERT_TRUE(sv_equals(name_int, "int_val"));
    TEST_ASSERT_TRUE(sv_equals(name_float, "float_val"));
}

// ============================================================================
// Value Wrapper Tests
// ============================================================================

void test_value_basic_access() {
    Value<int> f{42};

    // Transparent read
    int val = f;
    TEST_ASSERT_EQUAL(42, val);

    // Transparent write
    f = 100;
    TEST_ASSERT_EQUAL(100, f.value);
}

void test_value_operators() {
    Value<int> f{10};

    // Comparison
    TEST_ASSERT_TRUE(f == 10);
    TEST_ASSERT_FALSE(f == 11);
    TEST_ASSERT_TRUE(f < 20);
    TEST_ASSERT_TRUE(f > 5);

    // Arithmetic
    TEST_ASSERT_EQUAL(15, f + 5);
    TEST_ASSERT_EQUAL(5, f - 5);
    TEST_ASSERT_EQUAL(20, f * 2);

    // Compound assignment
    f += 5;
    TEST_ASSERT_EQUAL(15, f.value);
}

void test_value_constraints_min_max() {
    Value<int> f{50};
    f.setRange(0, 100);

    TEST_ASSERT_TRUE(f.constraints.flags.hasMin);
    TEST_ASSERT_TRUE(f.constraints.flags.hasMax);
    TEST_ASSERT_EQUAL(0, f.constraints.getMin<int>());
    TEST_ASSERT_EQUAL(100, f.constraints.getMax<int>());
}

void test_value_validation() {
    Value<int> f{50};
    f.setRange(0, 100);

    TEST_ASSERT_TRUE(f.validate(50));
    TEST_ASSERT_TRUE(f.validate(0));
    TEST_ASSERT_TRUE(f.validate(100));
    TEST_ASSERT_FALSE(f.validate(-1));
    TEST_ASSERT_FALSE(f.validate(101));
}

void test_value_try_set() {
    Value<int> f{50};
    f.setRange(0, 100);

    TEST_ASSERT_TRUE(f.trySet(75));
    TEST_ASSERT_EQUAL(75, f.value);

    TEST_ASSERT_FALSE(f.trySet(150));
    TEST_ASSERT_EQUAL(75, f.value);  // Unchanged
}

void test_value_set_clamp() {
    Value<int> f{50};
    f.setRange(0, 100);

    f.setClamp(150);
    TEST_ASSERT_EQUAL(100, f.value);

    f.setClamp(-50);
    TEST_ASSERT_EQUAL(0, f.value);

    f.setClamp(50);
    TEST_ASSERT_EQUAL(50, f.value);
}

void test_value_readonly() {
    Value<int> f{42};
    f.setReadOnly();

    f = 100;  // Should be ignored
    TEST_ASSERT_EQUAL(42, f.value);

    f += 10;  // Should be ignored
    TEST_ASSERT_EQUAL(42, f.value);

    TEST_ASSERT_FALSE(f.trySet(100));
}

void test_value_is_value_trait() {
    static_assert(is_value_v<Value<int>>, "Value<int> should be is_value");
    static_assert(is_value_v<Value<float>>, "Value<float> should be is_value");
    static_assert(!is_value_v<int>, "int should not be is_value");
    static_assert(!is_value_v<Position>, "Position should not be is_value");
    TEST_PASS();
}

void test_value_unwrap() {
    static_assert(std::is_same_v<unwrap_value_t<Value<int>>, int>, "Unwrap Value<int> should be int");
    static_assert(std::is_same_v<unwrap_value_t<Value<float>>, float>, "Unwrap Value<float> should be float");
    static_assert(std::is_same_v<unwrap_value_t<int>, int>, "Unwrap int should be int");
    TEST_PASS();
}

void test_value_get_value() {
    Value<int> f{42};
    int plain = 100;

    TEST_ASSERT_EQUAL(42, get_value(f));
    TEST_ASSERT_EQUAL(100, get_value(plain));

    // Modify via get_value
    get_value(f) = 50;
    TEST_ASSERT_EQUAL(50, f.value);
}

void test_value_get_constraints() {
    Value<int> f{42};
    f.setRange(0, 100);
    int plain = 100;

    const ValueConstraints* fc = get_constraints(f);
    const ValueConstraints* pc = get_constraints(plain);

    TEST_ASSERT_NOT_NULL(fc);
    TEST_ASSERT_NULL(pc);
    TEST_ASSERT_TRUE(fc->flags.hasMin);
}

// Test struct with Value members
struct ConfigWithValues {
    Value<uint8_t> brightness{128};
    Value<uint8_t> speed{50};
    Value<bool> enabled{true};
    int32_t plainValue;  // Mix of Value and plain
};

void test_value_in_struct() {
    ConfigWithValues config;
    config.brightness.setRange(0, 255);
    config.speed.setRange(0, 100);

    config.brightness = 200;
    config.speed = 75;
    config.enabled = false;
    config.plainValue = 42;

    TEST_ASSERT_EQUAL(200, config.brightness.value);
    TEST_ASSERT_EQUAL(75, config.speed.value);
    TEST_ASSERT_FALSE(config.enabled.value);
    TEST_ASSERT_EQUAL(42, config.plainValue);
}

void test_value_reflection_with_values() {
    ConfigWithValues config;
    config.brightness = 100;
    config.speed = 50;
    config.enabled = true;
    config.plainValue = 42;

    // Should still be able to iterate
    int count = 0;
    for_each_field(config, [&count](auto, auto&) {
        count++;
    });
    TEST_ASSERT_EQUAL(4, count);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Field count tests
    RUN_TEST(test_field_count_one);
    RUN_TEST(test_field_count_two);
    RUN_TEST(test_field_count_three);
    RUN_TEST(test_field_count_position);
    RUN_TEST(test_field_count_mixed);
    RUN_TEST(test_field_count_large);

    // is_reflectable tests
    RUN_TEST(test_is_reflectable_simple_struct);
    RUN_TEST(test_is_reflectable_not_array);
    RUN_TEST(test_is_reflectable_const);

    // to_tuple tests
    RUN_TEST(test_to_tuple_access);
    RUN_TEST(test_to_tuple_modify);
    RUN_TEST(test_to_tuple_mixed_types);

    // field_type_t tests
    RUN_TEST(test_field_type);

    // get<N> tests
    RUN_TEST(test_get_by_index);
    RUN_TEST(test_get_modify);
    RUN_TEST(test_get_const);

    // for_each_field tests
    RUN_TEST(test_for_each_field_count);
    RUN_TEST(test_for_each_field_sum);
    RUN_TEST(test_for_each_field_modify);
    RUN_TEST(test_for_each_field_indexed);

    // Field name extraction tests
    RUN_TEST(test_member_name_extraction);
    RUN_TEST(test_member_name_y);
    RUN_TEST(test_member_name_mixed);

    // Value wrapper tests
    RUN_TEST(test_value_basic_access);
    RUN_TEST(test_value_operators);
    RUN_TEST(test_value_constraints_min_max);
    RUN_TEST(test_value_validation);
    RUN_TEST(test_value_try_set);
    RUN_TEST(test_value_set_clamp);
    RUN_TEST(test_value_readonly);
    RUN_TEST(test_value_is_value_trait);
    RUN_TEST(test_value_unwrap);
    RUN_TEST(test_value_get_value);
    RUN_TEST(test_value_get_constraints);
    RUN_TEST(test_value_in_struct);
    RUN_TEST(test_value_reflection_with_values);

    return UNITY_END();
}

#endif // NATIVE_TEST