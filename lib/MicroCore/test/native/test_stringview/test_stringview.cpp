#include <unity.h>
#include <Arduino.h>
#include <StringView.h>

void setUp() {}
void tearDown() {}

// ============== Construction ==============

void test_default_construct() {
    StringView sv;
    TEST_ASSERT_NULL(sv.data());
    TEST_ASSERT_EQUAL(0, sv.length());
    TEST_ASSERT_TRUE(sv.empty());
}

void test_construct_from_cstr() {
    StringView sv("hello");
    TEST_ASSERT_EQUAL_STRING("hello", sv.data());
    TEST_ASSERT_EQUAL(5, sv.length());
    TEST_ASSERT_FALSE(sv.empty());
}

void test_construct_from_nullptr() {
    StringView sv(static_cast<const char*>(nullptr));
    TEST_ASSERT_NULL(sv.data());
    TEST_ASSERT_EQUAL(0, sv.length());
    TEST_ASSERT_TRUE(sv.empty());
}

void test_construct_from_cstr_and_len() {
    StringView sv("hello world", 5);
    TEST_ASSERT_EQUAL(5, sv.length());
    TEST_ASSERT_EQUAL('h', sv[0]);
    TEST_ASSERT_EQUAL('o', sv[4]);
}

void test_construct_from_arduino_string() {
    String s("hello");
    StringView sv(s);
    TEST_ASSERT_EQUAL(5, sv.length());
    TEST_ASSERT_EQUAL_STRING("hello", sv.data());
}

// ============== Element access ==============

void test_operator_bracket() {
    StringView sv("abcde");
    TEST_ASSERT_EQUAL('a', sv[0]);
    TEST_ASSERT_EQUAL('c', sv[2]);
    TEST_ASSERT_EQUAL('e', sv[4]);
}

void test_front() {
    StringView sv("hello");
    TEST_ASSERT_EQUAL('h', sv.front());
}

void test_back() {
    StringView sv("hello");
    TEST_ASSERT_EQUAL('o', sv.back());
}

void test_size_equals_length() {
    StringView sv("test");
    TEST_ASSERT_EQUAL(sv.length(), sv.size());
}

// ============== Iterators ==============

void test_begin_end() {
    StringView sv("abc");
    TEST_ASSERT_EQUAL('a', *sv.begin());
    TEST_ASSERT_EQUAL(3, sv.end() - sv.begin());
}

void test_range_for() {
    StringView sv("abc");
    char out[4] = {};
    int i = 0;
    for (char c : sv) out[i++] = c;
    TEST_ASSERT_EQUAL_STRING("abc", out);
}

// ============== Modifiers ==============

void test_remove_prefix() {
    StringView sv("hello world");
    sv.removePrefix(6);
    TEST_ASSERT_EQUAL(5, sv.length());
    TEST_ASSERT_EQUAL('w', sv[0]);
}

void test_remove_prefix_more_than_length() {
    StringView sv("hi");
    sv.removePrefix(10);
    TEST_ASSERT_EQUAL(0, sv.length());
    TEST_ASSERT_TRUE(sv.empty());
}

void test_remove_suffix() {
    StringView sv("hello world");
    sv.removeSuffix(6);
    TEST_ASSERT_EQUAL(5, sv.length());
    TEST_ASSERT_EQUAL('o', sv[4]);
}

void test_remove_suffix_more_than_length() {
    StringView sv("hi");
    sv.removeSuffix(10);
    TEST_ASSERT_EQUAL(0, sv.length());
    TEST_ASSERT_TRUE(sv.empty());
}

// ============== substr ==============

void test_substr_from_pos() {
    StringView sv("hello world");
    StringView sub = sv.substr(6);
    TEST_ASSERT_EQUAL(5, sub.length());
    TEST_ASSERT_EQUAL('w', sub[0]);
}

void test_substr_with_count() {
    StringView sv("hello world");
    StringView sub = sv.substr(0, 5);
    TEST_ASSERT_EQUAL(5, sub.length());
    TEST_ASSERT_EQUAL('h', sub[0]);
    TEST_ASSERT_EQUAL('o', sub[4]);
}

void test_substr_past_end() {
    StringView sv("hello");
    StringView sub = sv.substr(10);
    TEST_ASSERT_TRUE(sub.empty());
}

void test_substr_count_exceeds_remaining() {
    StringView sv("hello");
    StringView sub = sv.substr(3, 100);
    TEST_ASSERT_EQUAL(2, sub.length());
    TEST_ASSERT_EQUAL('l', sub[0]);
}

// ============== find ==============

void test_find_char() {
    StringView sv("hello");
    TEST_ASSERT_EQUAL(2, sv.find('l'));
}

void test_find_char_not_found() {
    StringView sv("hello");
    TEST_ASSERT_EQUAL(-1, sv.find('z'));
}

void test_find_char_from_pos() {
    StringView sv("hello");
    TEST_ASSERT_EQUAL(3, sv.find('l', 3));
}

void test_find_stringview() {
    StringView sv("hello world");
    TEST_ASSERT_EQUAL(6, sv.find(StringView("world")));
}

void test_find_stringview_not_found() {
    StringView sv("hello");
    TEST_ASSERT_EQUAL(-1, sv.find(StringView("xyz")));
}

void test_find_empty_stringview() {
    StringView sv("hello");
    TEST_ASSERT_EQUAL(0, sv.find(StringView("")));
}

void test_find_stringview_longer_than_source() {
    StringView sv("hi");
    TEST_ASSERT_EQUAL(-1, sv.find(StringView("hello")));
}

// ============== contains ==============

void test_contains_char_true() {
    StringView sv("hello");
    TEST_ASSERT_TRUE(sv.contains('e'));
}

void test_contains_char_false() {
    StringView sv("hello");
    TEST_ASSERT_FALSE(sv.contains('z'));
}

void test_contains_stringview_true() {
    StringView sv("hello world");
    TEST_ASSERT_TRUE(sv.contains(StringView("world")));
}

void test_contains_stringview_false() {
    StringView sv("hello");
    TEST_ASSERT_FALSE(sv.contains(StringView("xyz")));
}

// ============== startsWith / endsWith ==============

void test_starts_with_true() {
    StringView sv("hello world");
    TEST_ASSERT_TRUE(sv.startsWith(StringView("hello")));
}

void test_starts_with_false() {
    StringView sv("hello world");
    TEST_ASSERT_FALSE(sv.startsWith(StringView("world")));
}

void test_starts_with_empty() {
    StringView sv("hello");
    TEST_ASSERT_TRUE(sv.startsWith(StringView("")));
}

void test_starts_with_longer() {
    StringView sv("hi");
    TEST_ASSERT_FALSE(sv.startsWith(StringView("hello")));
}

void test_ends_with_true() {
    StringView sv("hello world");
    TEST_ASSERT_TRUE(sv.endsWith(StringView("world")));
}

void test_ends_with_false() {
    StringView sv("hello world");
    TEST_ASSERT_FALSE(sv.endsWith(StringView("hello")));
}

void test_ends_with_empty() {
    StringView sv("hello");
    TEST_ASSERT_TRUE(sv.endsWith(StringView("")));
}

void test_ends_with_longer() {
    StringView sv("hi");
    TEST_ASSERT_FALSE(sv.endsWith(StringView("hello")));
}

// ============== Comparison ==============

void test_equal() {
    StringView a("hello");
    StringView b("hello");
    TEST_ASSERT_TRUE(a == b);
}

void test_not_equal_content() {
    StringView a("hello");
    StringView b("world");
    TEST_ASSERT_TRUE(a != b);
}

void test_not_equal_length() {
    StringView a("hello");
    StringView b("hell");
    TEST_ASSERT_TRUE(a != b);
}

void test_compare_equal() {
    StringView a("abc");
    StringView b("abc");
    TEST_ASSERT_EQUAL(0, a.compare(b));
}

void test_compare_less() {
    StringView a("abc");
    StringView b("abd");
    TEST_ASSERT_TRUE(a.compare(b) < 0);
}

void test_compare_greater() {
    StringView a("abd");
    StringView b("abc");
    TEST_ASSERT_TRUE(a.compare(b) > 0);
}

void test_compare_prefix() {
    StringView a("abc");
    StringView b("abcd");
    TEST_ASSERT_TRUE(a.compare(b) < 0);  // shorter is less
}

void test_operator_less_than() {
    StringView a("abc");
    StringView b("abd");
    TEST_ASSERT_TRUE(a < b);
    TEST_ASSERT_FALSE(b < a);
}

// ============== equalsIgnoreCase ==============

void test_equals_ignore_case_same() {
    StringView a("Hello");
    StringView b("hello");
    TEST_ASSERT_TRUE(a.equalsIgnoreCase(b));
}

void test_equals_ignore_case_all_caps() {
    StringView a("HELLO");
    StringView b("hello");
    TEST_ASSERT_TRUE(a.equalsIgnoreCase(b));
}

void test_equals_ignore_case_different() {
    StringView a("hello");
    StringView b("world");
    TEST_ASSERT_FALSE(a.equalsIgnoreCase(b));
}

void test_equals_ignore_case_different_length() {
    StringView a("hello");
    StringView b("hell");
    TEST_ASSERT_FALSE(a.equalsIgnoreCase(b));
}

void test_equals_ignore_case_mixed() {
    StringView a("Content-Type");
    StringView b("content-type");
    TEST_ASSERT_TRUE(a.equalsIgnoreCase(b));
}

// ============== toString / conversion ==============

void test_to_string() {
    StringView sv("hello");
    String s = sv.toString();
    TEST_ASSERT_EQUAL_STRING("hello", s.c_str());
    TEST_ASSERT_EQUAL(5, s.length());
}

void test_to_string_empty() {
    StringView sv;
    String s = sv.toString();
    TEST_ASSERT_EQUAL(0, s.length());
}

void test_implicit_string_conversion() {
    StringView sv("test");
    String s = sv;  // implicit conversion
    TEST_ASSERT_EQUAL_STRING("test", s.c_str());
}

// ============== String concatenation operators ==============

void test_string_plus_stringview() {
    String lhs("hello ");
    StringView rhs("world");
    String result = lhs + rhs;
    TEST_ASSERT_EQUAL_STRING("hello world", result.c_str());
}

void test_cstr_plus_stringview() {
    StringView rhs("world");
    String result = "hello " + rhs;
    TEST_ASSERT_EQUAL_STRING("hello world", result.c_str());
}

// ============== Edge cases ==============

void test_single_char() {
    StringView sv("x");
    TEST_ASSERT_EQUAL(1, sv.length());
    TEST_ASSERT_EQUAL('x', sv.front());
    TEST_ASSERT_EQUAL('x', sv.back());
    TEST_ASSERT_EQUAL('x', sv[0]);
}

void test_substr_of_substr() {
    StringView sv("hello world foo");
    StringView sub1 = sv.substr(6, 5);  // "world"
    StringView sub2 = sub1.substr(1, 3); // "orl"
    TEST_ASSERT_EQUAL(3, sub2.length());
    TEST_ASSERT_EQUAL('o', sub2[0]);
    TEST_ASSERT_EQUAL('l', sub2[2]);
}

int main() {
    UNITY_BEGIN();

    // Construction
    RUN_TEST(test_default_construct);
    RUN_TEST(test_construct_from_cstr);
    RUN_TEST(test_construct_from_nullptr);
    RUN_TEST(test_construct_from_cstr_and_len);
    RUN_TEST(test_construct_from_arduino_string);

    // Element access
    RUN_TEST(test_operator_bracket);
    RUN_TEST(test_front);
    RUN_TEST(test_back);
    RUN_TEST(test_size_equals_length);

    // Iterators
    RUN_TEST(test_begin_end);
    RUN_TEST(test_range_for);

    // Modifiers
    RUN_TEST(test_remove_prefix);
    RUN_TEST(test_remove_prefix_more_than_length);
    RUN_TEST(test_remove_suffix);
    RUN_TEST(test_remove_suffix_more_than_length);

    // substr
    RUN_TEST(test_substr_from_pos);
    RUN_TEST(test_substr_with_count);
    RUN_TEST(test_substr_past_end);
    RUN_TEST(test_substr_count_exceeds_remaining);

    // find
    RUN_TEST(test_find_char);
    RUN_TEST(test_find_char_not_found);
    RUN_TEST(test_find_char_from_pos);
    RUN_TEST(test_find_stringview);
    RUN_TEST(test_find_stringview_not_found);
    RUN_TEST(test_find_empty_stringview);
    RUN_TEST(test_find_stringview_longer_than_source);

    // contains
    RUN_TEST(test_contains_char_true);
    RUN_TEST(test_contains_char_false);
    RUN_TEST(test_contains_stringview_true);
    RUN_TEST(test_contains_stringview_false);

    // startsWith / endsWith
    RUN_TEST(test_starts_with_true);
    RUN_TEST(test_starts_with_false);
    RUN_TEST(test_starts_with_empty);
    RUN_TEST(test_starts_with_longer);
    RUN_TEST(test_ends_with_true);
    RUN_TEST(test_ends_with_false);
    RUN_TEST(test_ends_with_empty);
    RUN_TEST(test_ends_with_longer);

    // Comparison
    RUN_TEST(test_equal);
    RUN_TEST(test_not_equal_content);
    RUN_TEST(test_not_equal_length);
    RUN_TEST(test_compare_equal);
    RUN_TEST(test_compare_less);
    RUN_TEST(test_compare_greater);
    RUN_TEST(test_compare_prefix);
    RUN_TEST(test_operator_less_than);

    // equalsIgnoreCase
    RUN_TEST(test_equals_ignore_case_same);
    RUN_TEST(test_equals_ignore_case_all_caps);
    RUN_TEST(test_equals_ignore_case_different);
    RUN_TEST(test_equals_ignore_case_different_length);
    RUN_TEST(test_equals_ignore_case_mixed);

    // toString / conversion
    RUN_TEST(test_to_string);
    RUN_TEST(test_to_string_empty);
    RUN_TEST(test_implicit_string_conversion);

    // Concatenation operators
    RUN_TEST(test_string_plus_stringview);
    RUN_TEST(test_cstr_plus_stringview);

    // Edge cases
    RUN_TEST(test_single_char);
    RUN_TEST(test_substr_of_substr);

    return UNITY_END();
}
