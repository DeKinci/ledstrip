#ifdef NATIVE_TEST

#include <unity.h>
#include <MicroList.h>
#include <vector>
#include <array>

using namespace MicroProto;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Basic Operations
// ============================================================================

void test_default_constructor() {
    MicroList<int, 4> v;
    TEST_ASSERT_EQUAL(0, v.size());
    TEST_ASSERT_EQUAL(4, v.capacity());
    TEST_ASSERT_TRUE(v.empty());
    TEST_ASSERT_TRUE(v.is_inline());
}

void test_size_constructor() {
    MicroList<int, 4> v(3);
    TEST_ASSERT_EQUAL(3, v.size());
    TEST_ASSERT_TRUE(v.is_inline());
    // Elements should be zero-initialized
    TEST_ASSERT_EQUAL(0, v[0]);
    TEST_ASSERT_EQUAL(0, v[1]);
    TEST_ASSERT_EQUAL(0, v[2]);
}

void test_size_value_constructor() {
    MicroList<int, 4> v(3, 42);
    TEST_ASSERT_EQUAL(3, v.size());
    TEST_ASSERT_EQUAL(42, v[0]);
    TEST_ASSERT_EQUAL(42, v[1]);
    TEST_ASSERT_EQUAL(42, v[2]);
}

void test_initializer_list() {
    MicroList<int, 4> v = {1, 2, 3};
    TEST_ASSERT_EQUAL(3, v.size());
    TEST_ASSERT_EQUAL(1, v[0]);
    TEST_ASSERT_EQUAL(2, v[1]);
    TEST_ASSERT_EQUAL(3, v[2]);
    TEST_ASSERT_TRUE(v.is_inline());
}

void test_push_back() {
    MicroList<int, 4> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    TEST_ASSERT_EQUAL(3, v.size());
    TEST_ASSERT_EQUAL(10, v[0]);
    TEST_ASSERT_EQUAL(20, v[1]);
    TEST_ASSERT_EQUAL(30, v[2]);
}

void test_pop_back() {
    MicroList<int, 4> v = {1, 2, 3};
    v.pop_back();
    TEST_ASSERT_EQUAL(2, v.size());
    TEST_ASSERT_EQUAL(2, v.back());
}

void test_front_back() {
    MicroList<int, 4> v = {10, 20, 30};
    TEST_ASSERT_EQUAL(10, v.front());
    TEST_ASSERT_EQUAL(30, v.back());

    v.front() = 100;
    v.back() = 300;
    TEST_ASSERT_EQUAL(100, v[0]);
    TEST_ASSERT_EQUAL(300, v[2]);
}

void test_clear() {
    MicroList<int, 4> v = {1, 2, 3};
    v.clear();
    TEST_ASSERT_EQUAL(0, v.size());
    TEST_ASSERT_TRUE(v.empty());
    TEST_ASSERT_EQUAL(4, v.capacity()); // Capacity unchanged
}

void test_at_bounds_clamping() {
    MicroList<int, 4> v = {10, 20, 30};
    // at() clamps to valid range instead of throwing
    TEST_ASSERT_EQUAL(30, v.at(100)); // Clamped to last element
    TEST_ASSERT_EQUAL(10, v.at(0));
}

void test_data_pointer() {
    MicroList<int, 4> v = {1, 2, 3};
    int* ptr = v.data();
    TEST_ASSERT_EQUAL(1, ptr[0]);
    TEST_ASSERT_EQUAL(2, ptr[1]);
    ptr[0] = 100;
    TEST_ASSERT_EQUAL(100, v[0]);
}

// ============================================================================
// SBO and Heap Spillover
// ============================================================================

void test_stays_inline_under_capacity() {
    MicroList<int, 4> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);

    TEST_ASSERT_EQUAL(4, v.size());
    TEST_ASSERT_EQUAL(4, v.capacity());
    TEST_ASSERT_TRUE(v.is_inline());
}

void test_spills_to_heap() {
    MicroList<int, 4> v;
    for (int i = 0; i < 5; i++) {
        v.push_back(i);
    }

    TEST_ASSERT_EQUAL(5, v.size());
    TEST_ASSERT_FALSE(v.is_inline());
    TEST_ASSERT_TRUE(v.capacity() >= 5);

    // Verify data integrity
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(i, v[i]);
    }
}

void test_growth_factor() {
    MicroList<int, 4> v;
    for (int i = 0; i < 4; i++) v.push_back(i);
    TEST_ASSERT_EQUAL(4, v.capacity());

    v.push_back(4); // Triggers growth
    // 4 * 1.5 = 6
    TEST_ASSERT_EQUAL(6, v.capacity());

    for (int i = 5; i < 6; i++) v.push_back(i);
    v.push_back(6); // Triggers growth
    // 6 * 1.5 = 9
    TEST_ASSERT_EQUAL(9, v.capacity());
}

void test_shrink_to_fit_to_inline() {
    MicroList<int, 4> v;
    for (int i = 0; i < 10; i++) v.push_back(i);
    TEST_ASSERT_FALSE(v.is_inline());

    // Clear and shrink
    v.clear();
    v.push_back(1);
    v.push_back(2);
    v.shrink_to_fit();

    TEST_ASSERT_TRUE(v.is_inline());
    TEST_ASSERT_EQUAL(4, v.capacity());
    TEST_ASSERT_EQUAL(2, v.size());
    TEST_ASSERT_EQUAL(1, v[0]);
    TEST_ASSERT_EQUAL(2, v[1]);
}

void test_shrink_to_fit_heap() {
    MicroList<int, 4> v;
    for (int i = 0; i < 20; i++) v.push_back(i);
    size_t old_cap = v.capacity();
    TEST_ASSERT_TRUE(old_cap > 20);

    // Remove some elements but stay above inline capacity
    while (v.size() > 6) v.pop_back();
    v.shrink_to_fit();

    TEST_ASSERT_FALSE(v.is_inline());
    TEST_ASSERT_EQUAL(6, v.capacity());
    TEST_ASSERT_EQUAL(6, v.size());
}

void test_reserve() {
    MicroList<int, 4> v;
    TEST_ASSERT_TRUE(v.reserve(10));
    TEST_ASSERT_EQUAL(10, v.capacity());
    TEST_ASSERT_FALSE(v.is_inline());
    TEST_ASSERT_EQUAL(0, v.size()); // Size unchanged
}

void test_reserve_no_shrink() {
    MicroList<int, 4> v;
    v.reserve(10);
    TEST_ASSERT_TRUE(v.reserve(5)); // Should succeed but not shrink
    TEST_ASSERT_EQUAL(10, v.capacity());
}

// ============================================================================
// Max Capacity
// ============================================================================

void test_max_capacity_limit() {
    MicroList<int, 4, 8> v; // Max 8 elements

    // Fill to max
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_TRUE(v.push_back(i));
    }
    TEST_ASSERT_EQUAL(8, v.size());

    // Should fail to add more
    TEST_ASSERT_FALSE(v.push_back(99));
    TEST_ASSERT_EQUAL(8, v.size()); // Size unchanged
}

void test_reserve_respects_max() {
    MicroList<int, 4, 8> v;
    TEST_ASSERT_TRUE(v.reserve(8));
    TEST_ASSERT_FALSE(v.reserve(10)); // Exceeds max
    TEST_ASSERT_EQUAL(8, v.capacity());
}

void test_resize_respects_max() {
    MicroList<int, 4, 8> v;
    TEST_ASSERT_TRUE(v.resize(8));
    TEST_ASSERT_FALSE(v.resize(10)); // Exceeds max
    TEST_ASSERT_EQUAL(8, v.size());
}

void test_unlimited_capacity() {
    MicroList<int, 4, 0> v; // Unlimited max capacity

    // Should be able to grow beyond 256 (default max)
    for (int i = 0; i < 300; i++) {
        TEST_ASSERT_TRUE(v.push_back(i));
    }
    TEST_ASSERT_EQUAL(300, v.size());
}

// ============================================================================
// Iterators
// ============================================================================

void test_iterators() {
    MicroList<int, 4> v = {10, 20, 30};

    int sum = 0;
    for (auto it = v.begin(); it != v.end(); ++it) {
        sum += *it;
    }
    TEST_ASSERT_EQUAL(60, sum);
}

void test_const_iterators() {
    const MicroList<int, 4> v = {10, 20, 30};

    int sum = 0;
    for (auto it = v.cbegin(); it != v.cend(); ++it) {
        sum += *it;
    }
    TEST_ASSERT_EQUAL(60, sum);
}

void test_range_for() {
    MicroList<int, 4> v = {1, 2, 3, 4};

    int sum = 0;
    for (const auto& x : v) {
        sum += x;
    }
    TEST_ASSERT_EQUAL(10, sum);
}

void test_modify_via_iterator() {
    MicroList<int, 4> v = {1, 2, 3};

    for (auto& x : v) {
        x *= 10;
    }

    TEST_ASSERT_EQUAL(10, v[0]);
    TEST_ASSERT_EQUAL(20, v[1]);
    TEST_ASSERT_EQUAL(30, v[2]);
}

void test_iterator_from_other_iterators() {
    MicroList<uint8_t, 8> v;
    std::vector<uint8_t> src = {1, 2, 3, 4, 5};
    MicroList<uint8_t, 8> v2(src.begin(), src.end());

    TEST_ASSERT_EQUAL(5, v2.size());
    TEST_ASSERT_EQUAL(1, v2[0]);
    TEST_ASSERT_EQUAL(5, v2[4]);
}

// ============================================================================
// Insert / Erase
// ============================================================================

void test_erase_single() {
    MicroList<int, 8> v = {1, 2, 3, 4, 5};
    auto it = v.erase(v.begin() + 2); // Erase 3

    TEST_ASSERT_EQUAL(4, v.size());
    TEST_ASSERT_EQUAL(1, v[0]);
    TEST_ASSERT_EQUAL(2, v[1]);
    TEST_ASSERT_EQUAL(4, v[2]);
    TEST_ASSERT_EQUAL(5, v[3]);
    TEST_ASSERT_EQUAL(4, *it); // Returns iterator to next element
}

void test_erase_first() {
    MicroList<int, 8> v = {1, 2, 3, 4, 5};
    v.erase(v.begin());
    TEST_ASSERT_EQUAL(4, v.size());
    TEST_ASSERT_EQUAL(2, v[0]);
}

void test_erase_last() {
    MicroList<int, 8> v = {1, 2, 3, 4, 5};
    v.erase(v.end() - 1);
    TEST_ASSERT_EQUAL(4, v.size());
    TEST_ASSERT_EQUAL(4, v.back());
}

void test_erase_range() {
    MicroList<int, 8> v = {1, 2, 3, 4, 5};
    v.erase(v.begin() + 1, v.begin() + 4); // Erase 2,3,4

    TEST_ASSERT_EQUAL(2, v.size());
    TEST_ASSERT_EQUAL(1, v[0]);
    TEST_ASSERT_EQUAL(5, v[1]);
}

void test_erase_all() {
    MicroList<int, 8> v = {1, 2, 3, 4, 5};
    v.erase(v.begin(), v.end());
    TEST_ASSERT_EQUAL(0, v.size());
    TEST_ASSERT_TRUE(v.empty());
}

void test_erase_invalid_range() {
    MicroList<int, 8> v = {1, 2, 3};
    auto it = v.erase(v.begin() + 5, v.begin() + 6); // Out of bounds
    TEST_ASSERT_EQUAL(v.end(), it);
    TEST_ASSERT_EQUAL(3, v.size()); // Unchanged
}

void test_insert() {
    MicroList<int, 8> v = {1, 2, 4, 5};
    auto it = v.insert(v.begin() + 2, 3);

    TEST_ASSERT_EQUAL(5, v.size());
    TEST_ASSERT_EQUAL(1, v[0]);
    TEST_ASSERT_EQUAL(2, v[1]);
    TEST_ASSERT_EQUAL(3, v[2]);
    TEST_ASSERT_EQUAL(4, v[3]);
    TEST_ASSERT_EQUAL(5, v[4]);
    TEST_ASSERT_EQUAL(3, *it);
}

void test_insert_at_begin() {
    MicroList<int, 8> v = {2, 3, 4};
    v.insert(v.begin(), 1);
    TEST_ASSERT_EQUAL(4, v.size());
    TEST_ASSERT_EQUAL(1, v[0]);
    TEST_ASSERT_EQUAL(2, v[1]);
}

void test_insert_at_end() {
    MicroList<int, 8> v = {1, 2, 3};
    v.insert(v.end(), 4);
    TEST_ASSERT_EQUAL(4, v.size());
    TEST_ASSERT_EQUAL(4, v[3]);
}

void test_insert_triggers_growth() {
    MicroList<int, 4> v = {1, 2, 3, 4}; // Full
    TEST_ASSERT_TRUE(v.is_inline());

    v.insert(v.begin() + 2, 99);
    TEST_ASSERT_EQUAL(5, v.size());
    TEST_ASSERT_FALSE(v.is_inline());
    TEST_ASSERT_EQUAL(1, v[0]);
    TEST_ASSERT_EQUAL(2, v[1]);
    TEST_ASSERT_EQUAL(99, v[2]);
    TEST_ASSERT_EQUAL(3, v[3]);
    TEST_ASSERT_EQUAL(4, v[4]);
}

// ============================================================================
// Resize
// ============================================================================

void test_resize_grow_zero_fill() {
    MicroList<int, 8> v = {1, 2, 3};
    v.resize(5);
    TEST_ASSERT_EQUAL(5, v.size());
    TEST_ASSERT_EQUAL(1, v[0]);
    TEST_ASSERT_EQUAL(2, v[1]);
    TEST_ASSERT_EQUAL(3, v[2]);
    TEST_ASSERT_EQUAL(0, v[3]);
    TEST_ASSERT_EQUAL(0, v[4]);
}

void test_resize_grow_value_fill() {
    MicroList<int, 8> v = {1, 2, 3};
    v.resize(5, 42);
    TEST_ASSERT_EQUAL(5, v.size());
    TEST_ASSERT_EQUAL(1, v[0]);
    TEST_ASSERT_EQUAL(2, v[1]);
    TEST_ASSERT_EQUAL(3, v[2]);
    TEST_ASSERT_EQUAL(42, v[3]);
    TEST_ASSERT_EQUAL(42, v[4]);
}

void test_resize_shrink() {
    MicroList<int, 8> v = {1, 2, 3, 4, 5};
    v.resize(3);
    TEST_ASSERT_EQUAL(3, v.size());
    TEST_ASSERT_EQUAL(1, v[0]);
    TEST_ASSERT_EQUAL(2, v[1]);
    TEST_ASSERT_EQUAL(3, v[2]);
}

void test_resize_to_zero() {
    MicroList<int, 8> v = {1, 2, 3};
    v.resize(0);
    TEST_ASSERT_EQUAL(0, v.size());
    TEST_ASSERT_TRUE(v.empty());
}

// ============================================================================
// Copy / Move
// ============================================================================

void test_copy_constructor_inline() {
    MicroList<int, 4> v1 = {1, 2, 3};
    MicroList<int, 4> v2(v1);

    TEST_ASSERT_EQUAL(3, v2.size());
    TEST_ASSERT_TRUE(v2.is_inline());
    TEST_ASSERT_EQUAL(1, v2[0]);
    TEST_ASSERT_EQUAL(2, v2[1]);
    TEST_ASSERT_EQUAL(3, v2[2]);

    // Modify v1, v2 should be independent
    v1[0] = 100;
    TEST_ASSERT_EQUAL(1, v2[0]);
}

void test_copy_constructor_heap() {
    MicroList<int, 2> v1;
    for (int i = 0; i < 5; i++) v1.push_back(i);
    TEST_ASSERT_FALSE(v1.is_inline());

    MicroList<int, 2> v2(v1);
    TEST_ASSERT_EQUAL(5, v2.size());

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(i, v2[i]);
    }

    // Verify deep copy
    v1[0] = 999;
    TEST_ASSERT_EQUAL(0, v2[0]);
}

void test_move_constructor_inline() {
    MicroList<int, 4> v1 = {1, 2, 3};
    MicroList<int, 4> v2(std::move(v1));

    TEST_ASSERT_EQUAL(3, v2.size());
    TEST_ASSERT_TRUE(v2.is_inline());
    TEST_ASSERT_EQUAL(1, v2[0]);
}

void test_move_constructor_heap() {
    MicroList<int, 2> v1;
    for (int i = 0; i < 5; i++) v1.push_back(i);

    const int* old_data = v1.data();
    MicroList<int, 2> v2(std::move(v1));

    TEST_ASSERT_EQUAL(5, v2.size());
    TEST_ASSERT_FALSE(v2.is_inline());
    TEST_ASSERT_EQUAL(old_data, v2.data()); // Pointer stolen

    TEST_ASSERT_EQUAL(0, v1.size()); // v1 is empty now
    TEST_ASSERT_TRUE(v1.is_inline());
}

void test_copy_assignment() {
    MicroList<int, 4> v1 = {1, 2, 3};
    MicroList<int, 4> v2 = {10, 20};

    v2 = v1;
    TEST_ASSERT_EQUAL(3, v2.size());
    TEST_ASSERT_EQUAL(1, v2[0]);
    TEST_ASSERT_EQUAL(2, v2[1]);
    TEST_ASSERT_EQUAL(3, v2[2]);
}

void test_copy_assignment_self() {
    MicroList<int, 4> v = {1, 2, 3};
    v = v; // Self-assignment
    TEST_ASSERT_EQUAL(3, v.size());
    TEST_ASSERT_EQUAL(1, v[0]);
}

void test_move_assignment() {
    MicroList<int, 2> v1;
    for (int i = 0; i < 5; i++) v1.push_back(i);

    MicroList<int, 2> v2 = {99};
    v2 = std::move(v1);

    TEST_ASSERT_EQUAL(5, v2.size());
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(i, v2[i]);
    }
}

void test_move_assignment_self() {
    MicroList<int, 4> v = {1, 2, 3};
    v = std::move(v); // Self move-assignment
    // Behavior is implementation-defined but should not crash
    // Our implementation handles this gracefully
}

void test_initializer_list_assignment() {
    MicroList<int, 8> v = {1, 2, 3};
    v = {10, 20, 30, 40};
    TEST_ASSERT_EQUAL(4, v.size());
    TEST_ASSERT_EQUAL(10, v[0]);
    TEST_ASSERT_EQUAL(40, v[3]);
}

// ============================================================================
// std::vector Interop
// ============================================================================

void test_construct_from_std_vector() {
    std::vector<int> sv = {1, 2, 3, 4, 5};
    MicroList<int, 4> mv(sv);

    TEST_ASSERT_EQUAL(5, mv.size());
    TEST_ASSERT_FALSE(mv.is_inline()); // Exceeds inline capacity

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(sv[i], mv[i]);
    }
}

void test_assign_from_std_vector() {
    MicroList<int, 4> mv = {10, 20};
    std::vector<int> sv = {1, 2, 3};

    mv = sv;
    TEST_ASSERT_EQUAL(3, mv.size());
    TEST_ASSERT_TRUE(mv.is_inline());
    TEST_ASSERT_EQUAL(1, mv[0]);
    TEST_ASSERT_EQUAL(2, mv[1]);
    TEST_ASSERT_EQUAL(3, mv[2]);
}

void test_to_vector() {
    MicroList<int, 4> mv = {1, 2, 3};
    std::vector<int> sv = mv.to_vector();

    TEST_ASSERT_EQUAL(3, sv.size());
    TEST_ASSERT_EQUAL(1, sv[0]);
    TEST_ASSERT_EQUAL(2, sv[1]);
    TEST_ASSERT_EQUAL(3, sv[2]);
}

void test_compare_with_std_vector() {
    MicroList<int, 4> mv = {1, 2, 3};
    std::vector<int> sv1 = {1, 2, 3};
    std::vector<int> sv2 = {1, 2, 4};
    std::vector<int> sv3 = {1, 2};

    TEST_ASSERT_TRUE(mv == sv1);
    TEST_ASSERT_FALSE(mv == sv2);
    TEST_ASSERT_FALSE(mv == sv3);
    TEST_ASSERT_TRUE(mv != sv2);
}

void test_empty_to_vector() {
    MicroList<int, 4> mv;
    std::vector<int> sv = mv.to_vector();
    TEST_ASSERT_EQUAL(0, sv.size());
}

// ============================================================================
// Type Traits
// ============================================================================

void test_type_traits() {
    static_assert(is_micro_list_v<MicroList<int, 4>>, "Should detect MicroList");
    static_assert(!is_micro_list_v<std::vector<int>>, "Should not detect std::vector");
    static_assert(!is_micro_list_v<int>, "Should not detect int");

    static_assert(std::is_same_v<micro_list_element_t<MicroList<int, 4>>, int>,
        "Element type should be int");

    static_assert(micro_list_inline_capacity_v<MicroList<int, 8>> == 8,
        "Inline capacity should be 8");

    static_assert(micro_list_max_capacity_v<MicroList<int, 4, 100>> == 100,
        "Max capacity should be 100");

    static_assert(micro_list_max_capacity_v<MicroList<int, 4, 0>> == 0,
        "Max capacity should be 0 (unlimited)");

    TEST_PASS();
}

// ============================================================================
// Comparison
// ============================================================================

void test_equality() {
    MicroList<int, 4> v1 = {1, 2, 3};
    MicroList<int, 4> v2 = {1, 2, 3};
    MicroList<int, 4> v3 = {1, 2, 4};
    MicroList<int, 4> v4 = {1, 2};

    TEST_ASSERT_TRUE(v1 == v2);
    TEST_ASSERT_FALSE(v1 == v3);
    TEST_ASSERT_FALSE(v1 == v4);
}

void test_inequality() {
    MicroList<int, 4> v1 = {1, 2, 3};
    MicroList<int, 4> v2 = {1, 2, 4};

    TEST_ASSERT_TRUE(v1 != v2);
    TEST_ASSERT_FALSE(v1 != v1);
}

void test_equality_empty() {
    MicroList<int, 4> v1;
    MicroList<int, 4> v2;
    MicroList<int, 4> v3 = {1};

    TEST_ASSERT_TRUE(v1 == v2);
    TEST_ASSERT_FALSE(v1 == v3);
}

// ============================================================================
// Edge Cases
// ============================================================================

void test_empty_operations() {
    MicroList<int, 4> v;

    // These should not crash
    v.pop_back();
    v.clear();
    TEST_ASSERT_EQUAL(0, v.size());
}

void test_at_empty_list() {
    MicroList<int, 4> v;
    // at() on empty list - behavior is defined (clamps to 0)
    // This won't crash but accessing the result is UB
    // Just test that it doesn't crash
    (void)v.at(0);
    TEST_PASS();
}

void test_assign_raw_data() {
    MicroList<int, 4> mv;
    int data[] = {10, 20, 30, 40, 50};

    TEST_ASSERT_TRUE(mv.assign(data, 5));
    TEST_ASSERT_EQUAL(5, mv.size());
    TEST_ASSERT_FALSE(mv.is_inline());

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(data[i], mv[i]);
    }
}

void test_assign_respects_max() {
    MicroList<int, 4, 8> mv;
    int data[10] = {0};

    TEST_ASSERT_FALSE(mv.assign(data, 10)); // Exceeds max
    TEST_ASSERT_EQUAL(0, mv.size()); // Unchanged
}

void test_assign_replaces_content() {
    MicroList<int, 4> mv = {100, 200, 300};
    int data[] = {1, 2};

    TEST_ASSERT_TRUE(mv.assign(data, 2));
    TEST_ASSERT_EQUAL(2, mv.size());
    TEST_ASSERT_EQUAL(1, mv[0]);
    TEST_ASSERT_EQUAL(2, mv[1]);
}

void test_emplace_back() {
    MicroList<int, 4> v;
    int* ptr = v.emplace_back(42);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL(42, *ptr);
    TEST_ASSERT_EQUAL(1, v.size());
    TEST_ASSERT_EQUAL(42, v[0]);
}

void test_emplace_back_at_max() {
    MicroList<int, 2, 2> v = {1, 2}; // Full
    int* ptr = v.emplace_back(3);
    TEST_ASSERT_NULL(ptr);
    TEST_ASSERT_EQUAL(2, v.size());
}

void test_single_element_inline() {
    MicroList<int, 1> v;
    TEST_ASSERT_TRUE(v.push_back(42));
    TEST_ASSERT_EQUAL(1, v.size());
    TEST_ASSERT_TRUE(v.is_inline());

    // Second element spills to heap
    TEST_ASSERT_TRUE(v.push_back(43));
    TEST_ASSERT_FALSE(v.is_inline());
}

void test_different_element_types() {
    MicroList<uint8_t, 8> bytes = {0xFF, 0x00, 0x80};
    TEST_ASSERT_EQUAL(255, bytes[0]);
    TEST_ASSERT_EQUAL(0, bytes[1]);
    TEST_ASSERT_EQUAL(128, bytes[2]);

    MicroList<float, 4> floats = {1.5f, 2.5f};
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.5f, floats[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2.5f, floats[1]);
}

void test_max_size() {
    MicroList<int, 4, 100> v;
    TEST_ASSERT_EQUAL(100, v.max_size());

    MicroList<int, 4, 0> unlimited;
    TEST_ASSERT_TRUE(unlimited.max_size() > 1000000);
}

// ============================================================================
// Composite Types (Structs)
// ============================================================================

// POD struct - trivially copyable
struct Point {
    int32_t x;
    int32_t y;
};

// Struct with std::array - still trivially copyable
struct Color {
    std::array<uint8_t, 3> rgb;
};

// Struct with nested struct - trivially copyable if all members are
struct Rectangle {
    Point topLeft;
    Point bottomRight;
};

// Struct with constructor but still trivially copyable
// Note: Having a constructor doesn't make a type non-trivially copyable
// as long as copy/move operations are trivial
struct Vec2 {
    float x, y;
    // Default member initializers don't affect trivial copyability
};

// Verify our test types are trivially copyable at compile time
static_assert(std::is_trivially_copyable_v<Point>, "Point should be trivially copyable");
static_assert(std::is_trivially_copyable_v<Color>, "Color should be trivially copyable");
static_assert(std::is_trivially_copyable_v<Rectangle>, "Rectangle should be trivially copyable");
static_assert(std::is_trivially_copyable_v<Vec2>, "Vec2 should be trivially copyable");

// Types that are NOT trivially copyable (for documentation):
// - std::string (has heap allocation)
// - std::vector<T> (has heap allocation)
// - Any class with virtual functions
// - Any class with non-trivial copy constructor/assignment
// - Any class with non-trivial destructor
//
// These will cause a compile error:
// MicroList<std::string, 4> strings;  // ERROR: element type must be trivially copyable

void test_pod_struct() {
    MicroList<Point, 4> points;

    points.push_back({10, 20});
    points.push_back({30, 40});

    TEST_ASSERT_EQUAL(2, points.size());
    TEST_ASSERT_EQUAL(10, points[0].x);
    TEST_ASSERT_EQUAL(20, points[0].y);
    TEST_ASSERT_EQUAL(30, points[1].x);
    TEST_ASSERT_EQUAL(40, points[1].y);
}

void test_pod_struct_initializer_list() {
    MicroList<Point, 4> points = {{1, 2}, {3, 4}, {5, 6}};

    TEST_ASSERT_EQUAL(3, points.size());
    TEST_ASSERT_EQUAL(1, points[0].x);
    TEST_ASSERT_EQUAL(6, points[2].y);
}

void test_struct_with_array() {
    MicroList<Color, 4> colors;

    Color red = {{255, 0, 0}};
    Color green = {{0, 255, 0}};
    colors.push_back(red);
    colors.push_back(green);

    TEST_ASSERT_EQUAL(2, colors.size());
    TEST_ASSERT_EQUAL(255, colors[0].rgb[0]);
    TEST_ASSERT_EQUAL(0, colors[0].rgb[1]);
    TEST_ASSERT_EQUAL(0, colors[1].rgb[0]);
    TEST_ASSERT_EQUAL(255, colors[1].rgb[1]);
}

void test_nested_struct() {
    MicroList<Rectangle, 4> rects;

    Rectangle r1 = {{0, 0}, {100, 100}};
    Rectangle r2 = {{10, 10}, {50, 50}};
    rects.push_back(r1);
    rects.push_back(r2);

    TEST_ASSERT_EQUAL(2, rects.size());
    TEST_ASSERT_EQUAL(0, rects[0].topLeft.x);
    TEST_ASSERT_EQUAL(100, rects[0].bottomRight.x);
    TEST_ASSERT_EQUAL(10, rects[1].topLeft.x);
}

void test_struct_copy_semantics() {
    MicroList<Point, 4> v1 = {{1, 2}, {3, 4}};
    MicroList<Point, 4> v2 = v1; // Copy

    // Modify v1
    v1[0].x = 100;

    // v2 should be independent
    TEST_ASSERT_EQUAL(1, v2[0].x);
    TEST_ASSERT_EQUAL(100, v1[0].x);
}

void test_struct_move_semantics() {
    MicroList<Point, 2> v1;
    for (int i = 0; i < 5; i++) {
        v1.push_back({i, i * 10});
    }
    TEST_ASSERT_FALSE(v1.is_inline());

    const Point* old_data = v1.data();
    MicroList<Point, 2> v2 = std::move(v1);

    TEST_ASSERT_EQUAL(5, v2.size());
    TEST_ASSERT_EQUAL(old_data, v2.data()); // Pointer stolen
    TEST_ASSERT_EQUAL(0, v2[0].x);
    TEST_ASSERT_EQUAL(40, v2[4].y);
}

void test_struct_erase() {
    MicroList<Point, 8> points = {{1, 1}, {2, 2}, {3, 3}, {4, 4}};

    points.erase(points.begin() + 1); // Remove {2, 2}

    TEST_ASSERT_EQUAL(3, points.size());
    TEST_ASSERT_EQUAL(1, points[0].x);
    TEST_ASSERT_EQUAL(3, points[1].x); // Was {3, 3}
    TEST_ASSERT_EQUAL(4, points[2].x);
}

void test_struct_insert() {
    MicroList<Point, 8> points = {{1, 1}, {3, 3}};

    points.insert(points.begin() + 1, {2, 2});

    TEST_ASSERT_EQUAL(3, points.size());
    TEST_ASSERT_EQUAL(1, points[0].x);
    TEST_ASSERT_EQUAL(2, points[1].x);
    TEST_ASSERT_EQUAL(3, points[2].x);
}

void test_struct_resize_zero_init() {
    MicroList<Point, 8> points = {{1, 1}};
    points.resize(3);

    TEST_ASSERT_EQUAL(3, points.size());
    TEST_ASSERT_EQUAL(1, points[0].x);
    // New elements are zero-initialized via memset
    TEST_ASSERT_EQUAL(0, points[1].x);
    TEST_ASSERT_EQUAL(0, points[1].y);
    TEST_ASSERT_EQUAL(0, points[2].x);
}

void test_struct_resize_value_fill() {
    MicroList<Point, 8> points;
    Point fill = {42, 42};
    points.resize(3, fill);

    TEST_ASSERT_EQUAL(3, points.size());
    TEST_ASSERT_EQUAL(42, points[0].x);
    TEST_ASSERT_EQUAL(42, points[1].y);
    TEST_ASSERT_EQUAL(42, points[2].x);
}

void test_struct_spill_to_heap() {
    MicroList<Point, 2> points;

    // Fill inline capacity
    points.push_back({1, 1});
    points.push_back({2, 2});
    TEST_ASSERT_TRUE(points.is_inline());

    // Trigger heap allocation
    points.push_back({3, 3});
    TEST_ASSERT_FALSE(points.is_inline());

    // Verify data integrity
    TEST_ASSERT_EQUAL(1, points[0].x);
    TEST_ASSERT_EQUAL(2, points[1].x);
    TEST_ASSERT_EQUAL(3, points[2].x);
}

void test_struct_shrink_to_inline() {
    MicroList<Point, 4> points;

    // Force heap allocation
    for (int i = 0; i < 10; i++) {
        points.push_back({i, i});
    }
    TEST_ASSERT_FALSE(points.is_inline());

    // Shrink back
    points.clear();
    points.push_back({1, 1});
    points.shrink_to_fit();

    TEST_ASSERT_TRUE(points.is_inline());
    TEST_ASSERT_EQUAL(1, points[0].x);
}

void test_struct_to_vector() {
    MicroList<Point, 4> ml = {{1, 2}, {3, 4}};
    std::vector<Point> sv = ml.to_vector();

    TEST_ASSERT_EQUAL(2, sv.size());
    TEST_ASSERT_EQUAL(1, sv[0].x);
    TEST_ASSERT_EQUAL(4, sv[1].y);
}

void test_struct_from_vector() {
    std::vector<Point> sv = {{10, 20}, {30, 40}, {50, 60}};
    MicroList<Point, 4> ml(sv);

    TEST_ASSERT_EQUAL(3, ml.size());
    TEST_ASSERT_EQUAL(10, ml[0].x);
    TEST_ASSERT_EQUAL(60, ml[2].y);
}

void test_struct_comparison() {
    MicroList<Point, 4> v1 = {{1, 2}, {3, 4}};
    MicroList<Point, 4> v2 = {{1, 2}, {3, 4}};
    MicroList<Point, 4> v3 = {{1, 2}, {3, 5}};

    TEST_ASSERT_TRUE(v1 == v2);
    TEST_ASSERT_FALSE(v1 == v3);
}

void test_large_struct() {
    // Struct larger than a pointer
    struct LargeStruct {
        int32_t data[16]; // 64 bytes
    };
    static_assert(std::is_trivially_copyable_v<LargeStruct>, "LargeStruct should be trivially copyable");
    static_assert(sizeof(LargeStruct) == 64, "LargeStruct should be 64 bytes");

    MicroList<LargeStruct, 2> list;
    LargeStruct s1 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    list.push_back(s1);

    TEST_ASSERT_EQUAL(1, list.size());
    TEST_ASSERT_EQUAL(1, list[0].data[0]);
    TEST_ASSERT_EQUAL(16, list[0].data[15]);
}

// ============================================================================
// Non-trivial types (std::string)
// ============================================================================

// Verify std::string is NOT trivially copyable (confirms we're testing the right code path)
static_assert(!std::is_trivially_copyable_v<std::string>, "std::string should NOT be trivially copyable");
static_assert(!is_micro_list_trivial_v<MicroList<std::string, 4>>, "MicroList<std::string> should NOT be trivial");

void test_string_basic() {
    MicroList<std::string, 4> list;
    TEST_ASSERT_TRUE(list.empty());
    TEST_ASSERT_EQUAL(0, list.size());
    TEST_ASSERT_TRUE(list.is_inline());
}

void test_string_push_back() {
    MicroList<std::string, 4> list;
    list.push_back("hello");
    list.push_back("world");

    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_TRUE(list[0] == "hello");
    TEST_ASSERT_TRUE(list[1] == "world");
}

void test_string_initializer_list() {
    MicroList<std::string, 4> list = {"one", "two", "three"};

    TEST_ASSERT_EQUAL(3, list.size());
    TEST_ASSERT_TRUE(list[0] == "one");
    TEST_ASSERT_TRUE(list[1] == "two");
    TEST_ASSERT_TRUE(list[2] == "three");
}

void test_string_pop_back() {
    MicroList<std::string, 4> list = {"first", "second", "third"};
    list.pop_back();

    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_TRUE(list.back() == "second");
}

void test_string_clear() {
    MicroList<std::string, 4> list = {"a", "b", "c"};
    list.clear();

    TEST_ASSERT_TRUE(list.empty());
    TEST_ASSERT_EQUAL(0, list.size());
}

void test_string_copy_constructor() {
    MicroList<std::string, 4> original = {"alpha", "beta", "gamma"};
    MicroList<std::string, 4> copy(original);

    TEST_ASSERT_EQUAL(3, copy.size());
    TEST_ASSERT_TRUE(copy[0] == "alpha");
    TEST_ASSERT_TRUE(copy[1] == "beta");
    TEST_ASSERT_TRUE(copy[2] == "gamma");

    // Verify it's a deep copy
    original[0] = "modified";
    TEST_ASSERT_TRUE(copy[0] == "alpha");
}

void test_string_move_constructor() {
    MicroList<std::string, 4> original = {"one", "two"};
    MicroList<std::string, 4> moved(std::move(original));

    TEST_ASSERT_EQUAL(2, moved.size());
    TEST_ASSERT_TRUE(moved[0] == "one");
    TEST_ASSERT_TRUE(moved[1] == "two");
    TEST_ASSERT_EQUAL(0, original.size()); // Moved-from should be empty
}

void test_string_copy_assignment() {
    MicroList<std::string, 4> original = {"x", "y", "z"};
    MicroList<std::string, 4> target = {"old"};
    target = original;

    TEST_ASSERT_EQUAL(3, target.size());
    TEST_ASSERT_TRUE(target[0] == "x");
    TEST_ASSERT_TRUE(target[2] == "z");

    // Verify deep copy
    original[0] = "changed";
    TEST_ASSERT_TRUE(target[0] == "x");
}

void test_string_move_assignment() {
    MicroList<std::string, 4> original = {"data", "more"};
    MicroList<std::string, 4> target = {"old", "values", "here"};
    target = std::move(original);

    TEST_ASSERT_EQUAL(2, target.size());
    TEST_ASSERT_TRUE(target[0] == "data");
    TEST_ASSERT_TRUE(target[1] == "more");
    TEST_ASSERT_EQUAL(0, original.size());
}

void test_string_spill_to_heap() {
    MicroList<std::string, 2> list;  // Only 2 inline
    list.push_back("first");
    list.push_back("second");
    TEST_ASSERT_TRUE(list.is_inline());

    list.push_back("third");  // Should spill
    TEST_ASSERT_FALSE(list.is_inline());

    TEST_ASSERT_EQUAL(3, list.size());
    TEST_ASSERT_TRUE(list[0] == "first");
    TEST_ASSERT_TRUE(list[1] == "second");
    TEST_ASSERT_TRUE(list[2] == "third");
}

void test_string_shrink_to_inline() {
    MicroList<std::string, 4> list = {"a", "b", "c", "d", "e"};  // Spills to heap
    TEST_ASSERT_FALSE(list.is_inline());

    list.resize(2);  // Back to inline size
    list.shrink_to_fit();

    TEST_ASSERT_TRUE(list.is_inline());
    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_TRUE(list[0] == "a");
    TEST_ASSERT_TRUE(list[1] == "b");
}

void test_string_insert() {
    MicroList<std::string, 8> list = {"a", "c", "d"};
    list.insert(list.begin() + 1, "b");

    TEST_ASSERT_EQUAL(4, list.size());
    TEST_ASSERT_TRUE(list[0] == "a");
    TEST_ASSERT_TRUE(list[1] == "b");
    TEST_ASSERT_TRUE(list[2] == "c");
    TEST_ASSERT_TRUE(list[3] == "d");
}

void test_string_insert_at_begin() {
    MicroList<std::string, 8> list = {"second", "third"};
    list.insert(list.begin(), "first");

    TEST_ASSERT_EQUAL(3, list.size());
    TEST_ASSERT_TRUE(list[0] == "first");
    TEST_ASSERT_TRUE(list[1] == "second");
}

void test_string_erase() {
    MicroList<std::string, 8> list = {"a", "b", "c", "d"};
    list.erase(list.begin() + 1, list.begin() + 3);  // Erase "b" and "c"

    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_TRUE(list[0] == "a");
    TEST_ASSERT_TRUE(list[1] == "d");
}

void test_string_erase_single() {
    MicroList<std::string, 8> list = {"one", "two", "three"};
    list.erase(list.begin() + 1);  // Erase "two"

    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_TRUE(list[0] == "one");
    TEST_ASSERT_TRUE(list[1] == "three");
}

void test_string_resize_grow() {
    MicroList<std::string, 8> list = {"a"};
    list.resize(3);  // Grow with default-constructed strings

    TEST_ASSERT_EQUAL(3, list.size());
    TEST_ASSERT_TRUE(list[0] == "a");
    TEST_ASSERT_TRUE(list[1] == "");  // Default constructed
    TEST_ASSERT_TRUE(list[2] == "");
}

void test_string_resize_grow_with_value() {
    MicroList<std::string, 8> list = {"x"};
    list.resize(4, "fill");

    TEST_ASSERT_EQUAL(4, list.size());
    TEST_ASSERT_TRUE(list[0] == "x");
    TEST_ASSERT_TRUE(list[1] == "fill");
    TEST_ASSERT_TRUE(list[2] == "fill");
    TEST_ASSERT_TRUE(list[3] == "fill");
}

void test_string_resize_shrink() {
    MicroList<std::string, 8> list = {"a", "b", "c", "d", "e"};
    list.resize(2);

    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_TRUE(list[0] == "a");
    TEST_ASSERT_TRUE(list[1] == "b");
}

void test_string_iterators() {
    MicroList<std::string, 4> list = {"one", "two", "three"};

    std::string result;
    for (const auto& s : list) {
        result += s;
    }
    TEST_ASSERT_TRUE(result == "onetwothree");
}

void test_string_modify_via_iterator() {
    MicroList<std::string, 4> list = {"a", "b", "c"};

    for (auto& s : list) {
        s = s + s;  // Double each string
    }

    TEST_ASSERT_TRUE(list[0] == "aa");
    TEST_ASSERT_TRUE(list[1] == "bb");
    TEST_ASSERT_TRUE(list[2] == "cc");
}

void test_string_to_vector() {
    MicroList<std::string, 4> ml = {"hello", "world"};
    std::vector<std::string> sv = ml.to_vector();

    TEST_ASSERT_EQUAL(2, sv.size());
    TEST_ASSERT_TRUE(sv[0] == "hello");
    TEST_ASSERT_TRUE(sv[1] == "world");
}

void test_string_from_vector() {
    std::vector<std::string> sv = {"alpha", "beta", "gamma"};
    MicroList<std::string, 4> ml(sv);

    TEST_ASSERT_EQUAL(3, ml.size());
    TEST_ASSERT_TRUE(ml[0] == "alpha");
    TEST_ASSERT_TRUE(ml[1] == "beta");
    TEST_ASSERT_TRUE(ml[2] == "gamma");
}

void test_string_equality() {
    MicroList<std::string, 4> a = {"x", "y"};
    MicroList<std::string, 4> b = {"x", "y"};
    MicroList<std::string, 4> c = {"x", "z"};

    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
    TEST_ASSERT_TRUE(a != c);
}

void test_string_emplace_back() {
    MicroList<std::string, 4> list;
    list.emplace_back("test");
    list.emplace_back(5, 'x');  // std::string(5, 'x') = "xxxxx"

    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_TRUE(list[0] == "test");
    TEST_ASSERT_TRUE(list[1] == "xxxxx");
}

void test_string_long_strings() {
    // Test with strings longer than SSO buffer (usually 15-22 chars)
    MicroList<std::string, 4> list;
    std::string long_str = "this is a very long string that exceeds the small string optimization buffer size";

    list.push_back(long_str);
    list.push_back(long_str + " second");

    TEST_ASSERT_EQUAL(2, list.size());
    TEST_ASSERT_TRUE(list[0] == long_str);
    TEST_ASSERT_TRUE(list[1] == long_str + " second");
}

void test_string_copy_heap_to_heap() {
    MicroList<std::string, 2> original;
    for (int i = 0; i < 5; ++i) {
        original.push_back("item" + std::to_string(i));
    }
    TEST_ASSERT_FALSE(original.is_inline());

    MicroList<std::string, 2> copy(original);
    TEST_ASSERT_FALSE(copy.is_inline());
    TEST_ASSERT_EQUAL(5, copy.size());
    TEST_ASSERT_TRUE(copy[0] == "item0");
    TEST_ASSERT_TRUE(copy[4] == "item4");
}

void test_string_move_heap() {
    MicroList<std::string, 2> original;
    for (int i = 0; i < 5; ++i) {
        original.push_back("data" + std::to_string(i));
    }
    TEST_ASSERT_FALSE(original.is_inline());

    MicroList<std::string, 2> moved(std::move(original));
    TEST_ASSERT_FALSE(moved.is_inline());
    TEST_ASSERT_EQUAL(5, moved.size());
    TEST_ASSERT_TRUE(moved[2] == "data2");
    TEST_ASSERT_TRUE(original.empty());  // Moved-from should be empty
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Basic operations
    RUN_TEST(test_default_constructor);
    RUN_TEST(test_size_constructor);
    RUN_TEST(test_size_value_constructor);
    RUN_TEST(test_initializer_list);
    RUN_TEST(test_push_back);
    RUN_TEST(test_pop_back);
    RUN_TEST(test_front_back);
    RUN_TEST(test_clear);
    RUN_TEST(test_at_bounds_clamping);
    RUN_TEST(test_data_pointer);

    // SBO and heap
    RUN_TEST(test_stays_inline_under_capacity);
    RUN_TEST(test_spills_to_heap);
    RUN_TEST(test_growth_factor);
    RUN_TEST(test_shrink_to_fit_to_inline);
    RUN_TEST(test_shrink_to_fit_heap);
    RUN_TEST(test_reserve);
    RUN_TEST(test_reserve_no_shrink);

    // Max capacity
    RUN_TEST(test_max_capacity_limit);
    RUN_TEST(test_reserve_respects_max);
    RUN_TEST(test_resize_respects_max);
    RUN_TEST(test_unlimited_capacity);

    // Iterators
    RUN_TEST(test_iterators);
    RUN_TEST(test_const_iterators);
    RUN_TEST(test_range_for);
    RUN_TEST(test_modify_via_iterator);
    RUN_TEST(test_iterator_from_other_iterators);

    // Insert/Erase
    RUN_TEST(test_erase_single);
    RUN_TEST(test_erase_first);
    RUN_TEST(test_erase_last);
    RUN_TEST(test_erase_range);
    RUN_TEST(test_erase_all);
    RUN_TEST(test_erase_invalid_range);
    RUN_TEST(test_insert);
    RUN_TEST(test_insert_at_begin);
    RUN_TEST(test_insert_at_end);
    RUN_TEST(test_insert_triggers_growth);

    // Resize
    RUN_TEST(test_resize_grow_zero_fill);
    RUN_TEST(test_resize_grow_value_fill);
    RUN_TEST(test_resize_shrink);
    RUN_TEST(test_resize_to_zero);

    // Copy/Move
    RUN_TEST(test_copy_constructor_inline);
    RUN_TEST(test_copy_constructor_heap);
    RUN_TEST(test_move_constructor_inline);
    RUN_TEST(test_move_constructor_heap);
    RUN_TEST(test_copy_assignment);
    RUN_TEST(test_copy_assignment_self);
    RUN_TEST(test_move_assignment);
    RUN_TEST(test_move_assignment_self);
    RUN_TEST(test_initializer_list_assignment);

    // std::vector interop
    RUN_TEST(test_construct_from_std_vector);
    RUN_TEST(test_assign_from_std_vector);
    RUN_TEST(test_to_vector);
    RUN_TEST(test_compare_with_std_vector);
    RUN_TEST(test_empty_to_vector);

    // Type traits
    RUN_TEST(test_type_traits);

    // Comparison
    RUN_TEST(test_equality);
    RUN_TEST(test_inequality);
    RUN_TEST(test_equality_empty);

    // Edge cases
    RUN_TEST(test_empty_operations);
    RUN_TEST(test_at_empty_list);
    RUN_TEST(test_assign_raw_data);
    RUN_TEST(test_assign_respects_max);
    RUN_TEST(test_assign_replaces_content);
    RUN_TEST(test_emplace_back);
    RUN_TEST(test_emplace_back_at_max);
    RUN_TEST(test_single_element_inline);
    RUN_TEST(test_different_element_types);
    RUN_TEST(test_max_size);

    // Composite types (structs)
    RUN_TEST(test_pod_struct);
    RUN_TEST(test_pod_struct_initializer_list);
    RUN_TEST(test_struct_with_array);
    RUN_TEST(test_nested_struct);
    RUN_TEST(test_struct_copy_semantics);
    RUN_TEST(test_struct_move_semantics);
    RUN_TEST(test_struct_erase);
    RUN_TEST(test_struct_insert);
    RUN_TEST(test_struct_resize_zero_init);
    RUN_TEST(test_struct_resize_value_fill);
    RUN_TEST(test_struct_spill_to_heap);
    RUN_TEST(test_struct_shrink_to_inline);
    RUN_TEST(test_struct_to_vector);
    RUN_TEST(test_struct_from_vector);
    RUN_TEST(test_struct_comparison);
    RUN_TEST(test_large_struct);

    // Non-trivial types (std::string)
    RUN_TEST(test_string_basic);
    RUN_TEST(test_string_push_back);
    RUN_TEST(test_string_initializer_list);
    RUN_TEST(test_string_pop_back);
    RUN_TEST(test_string_clear);
    RUN_TEST(test_string_copy_constructor);
    RUN_TEST(test_string_move_constructor);
    RUN_TEST(test_string_copy_assignment);
    RUN_TEST(test_string_move_assignment);
    RUN_TEST(test_string_spill_to_heap);
    RUN_TEST(test_string_shrink_to_inline);
    RUN_TEST(test_string_insert);
    RUN_TEST(test_string_insert_at_begin);
    RUN_TEST(test_string_erase);
    RUN_TEST(test_string_erase_single);
    RUN_TEST(test_string_resize_grow);
    RUN_TEST(test_string_resize_grow_with_value);
    RUN_TEST(test_string_resize_shrink);
    RUN_TEST(test_string_iterators);
    RUN_TEST(test_string_modify_via_iterator);
    RUN_TEST(test_string_to_vector);
    RUN_TEST(test_string_from_vector);
    RUN_TEST(test_string_equality);
    RUN_TEST(test_string_emplace_back);
    RUN_TEST(test_string_long_strings);
    RUN_TEST(test_string_copy_heap_to_heap);
    RUN_TEST(test_string_move_heap);

    return UNITY_END();
}

#endif // NATIVE_TEST
