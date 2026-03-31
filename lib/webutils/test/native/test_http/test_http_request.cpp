#ifdef NATIVE_TEST

#include <unity.h>
#include <HttpRequest.h>

// Helper to parse a string literal
#define PARSE(req, str) req.parse(str, sizeof(str) - 1)

// Basic parsing tests
void test_simple_get_request() {
    HttpRequest req;
    const char* raw = "GET /ping HTTP/1.1\r\n\r\n";

    TEST_ASSERT_TRUE(req.parse(raw, strlen(raw)));
    TEST_ASSERT_TRUE(req);  // operator bool
    TEST_ASSERT_EQUAL_STRING("GET", req.method().toString().c_str());
    TEST_ASSERT_EQUAL_STRING("/ping", req.path().toString().c_str());
}

void test_post_request_with_body() {
    HttpRequest req;
    const char* raw = "POST /api/data HTTP/1.1\r\n"
                 "Content-Type: application/json\r\n"
                 "\r\n"
                 "{\"key\":\"value\"}";

    TEST_ASSERT_TRUE(req.parse(raw, strlen(raw)));
    TEST_ASSERT_EQUAL_STRING("POST", req.method().toString().c_str());
    TEST_ASSERT_EQUAL_STRING("{\"key\":\"value\"}", req.body().toString().c_str());
}

void test_invalid_request() {
    HttpRequest req;
    const char* raw = "INVALID\r\n\r\n";

    TEST_ASSERT_FALSE(req.parse(raw, strlen(raw)));
    TEST_ASSERT_FALSE(req);  // operator bool
}

void test_empty_request() {
    HttpRequest req;

    TEST_ASSERT_FALSE(req.parse("", 0));
    TEST_ASSERT_FALSE(req);
}

void test_invalid_factory() {
    HttpRequest req = HttpRequest::invalid();
    TEST_ASSERT_FALSE(req);
}

// Header tests
void test_header_access() {
    HttpRequest req;
    const char* raw = "GET /test HTTP/1.1\r\n"
                 "Content-Type: text/plain\r\n"
                 "X-Custom: hello\r\n"
                 "\r\n";

    TEST_ASSERT_TRUE(req.parse(raw, strlen(raw)));
    TEST_ASSERT_EQUAL_STRING("text/plain", req.header("Content-Type").toString().c_str());
    TEST_ASSERT_EQUAL_STRING("hello", req.header("X-Custom").toString().c_str());
}

void test_header_case_insensitive() {
    HttpRequest req;
    const char* raw = "GET /test HTTP/1.1\r\n"
                 "Content-Type: text/plain\r\n"
                 "\r\n";

    TEST_ASSERT_TRUE(req.parse(raw, strlen(raw)));
    TEST_ASSERT_EQUAL_STRING("text/plain", req.header("content-type").toString().c_str());
    TEST_ASSERT_EQUAL_STRING("text/plain", req.header("CONTENT-TYPE").toString().c_str());
}

// Path matching tests
void test_match_simple_path() {
    HttpRequest req;
    const char* raw = "GET /ping HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.match("GET", "/ping"));
    TEST_ASSERT_FALSE(req.match("POST", "/ping"));
    TEST_ASSERT_FALSE(req.match("GET", "/pong"));
}

void test_match_with_path_param() {
    HttpRequest req;
    const char* raw = "GET /api/user/123 HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.match("GET", "/api/user/{id}"));
    TEST_ASSERT_EQUAL_STRING("123", req.pathParam("id").toString().c_str());
}

void test_match_with_multiple_path_params() {
    HttpRequest req;
    const char* raw = "GET /api/user/123/posts/456 HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.match("GET", "/api/user/{userId}/posts/{postId}"));
    TEST_ASSERT_EQUAL_STRING("123", req.pathParam("userId").toString().c_str());
    TEST_ASSERT_EQUAL_STRING("456", req.pathParam("postId").toString().c_str());
}

void test_match_trailing_slash() {
    HttpRequest req;
    const char* raw = "GET /api/test/ HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.match("GET", "/api/test"));
    TEST_ASSERT_TRUE(req.match("GET", "/api/test/"));
}

void test_match_pattern_trailing_slash() {
    HttpRequest req;
    const char* raw = "GET /api/test HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.match("GET", "/api/test/"));
}

void test_match_clears_previous_params() {
    HttpRequest req;
    const char* raw = "GET /api/user/123 HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.match("GET", "/api/user/{id}"));
    TEST_ASSERT_EQUAL_STRING("123", req.pathParam("id").toString().c_str());

    // Match a different pattern - should clear old params
    TEST_ASSERT_TRUE(req.match("GET", "/api/user/{newId}"));
    TEST_ASSERT_EQUAL_STRING("123", req.pathParam("newId").toString().c_str());
    TEST_ASSERT_TRUE(req.pathParam("id").empty());  // Old param cleared
}

void test_match_fails_clears_params() {
    HttpRequest req;
    const char* raw = "GET /api/user/123 HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.match("GET", "/api/user/{id}"));
    TEST_ASSERT_EQUAL_STRING("123", req.pathParam("id").toString().c_str());

    // Failed match should still clear params
    TEST_ASSERT_FALSE(req.match("GET", "/different/path"));
    TEST_ASSERT_TRUE(req.pathParam("id").empty());
}

void test_match_empty_path_param_fails() {
    HttpRequest req;
    const char* raw = "GET /api/user//posts HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    // Empty segment where {id} should be - should fail
    TEST_ASSERT_FALSE(req.match("GET", "/api/user/{id}/posts"));
}

void test_match_segment_count_mismatch() {
    HttpRequest req;
    const char* raw = "GET /api/user HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    // Pattern has more segments
    TEST_ASSERT_FALSE(req.match("GET", "/api/user/{id}"));

    // Path has more segments
    const char* raw2 = "GET /api/user/123/extra HTTP/1.1\r\n\r\n";
    req.parse(raw2, strlen(raw2));
    TEST_ASSERT_FALSE(req.match("GET", "/api/user/{id}"));
}

void test_match_root_path() {
    HttpRequest req;
    const char* raw = "GET / HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.match("GET", "/"));
    TEST_ASSERT_FALSE(req.match("GET", "/api"));
}

void test_match_complex_pattern() {
    HttpRequest req;
    const char* raw = "GET /api/v1/user/abc123/avatar HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.match("GET", "api/v1/user/{userid}/avatar"));
    TEST_ASSERT_EQUAL_STRING("abc123", req.pathParam("userid").toString().c_str());
}

void test_toString() {
    HttpRequest req;
    const char* raw = "GET /ping HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));
    TEST_ASSERT_EQUAL_STRING("GET /ping", req.toString().c_str());

    const char* raw2 = "POST /data HTTP/1.1\r\n\r\n{\"x\":1}";
    req.parse(raw2, strlen(raw2));
    TEST_ASSERT_TRUE(req.toString().indexOf("[7 bytes]") >= 0);
}

void test_toString_invalid() {
    HttpRequest req = HttpRequest::invalid();
    TEST_ASSERT_EQUAL_STRING("[invalid request]", req.toString().c_str());
}

// Query parameter tests
void test_query_param_single() {
    HttpRequest req;
    const char* raw = "GET /api/test?name=value HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_EQUAL_STRING("/api/test", req.path().toString().c_str());
    TEST_ASSERT_EQUAL_STRING("value", req.queryParam("name").toString().c_str());
    TEST_ASSERT_TRUE(req.hasQueryParam("name"));
}

void test_query_param_multiple() {
    HttpRequest req;
    const char* raw = "GET /search?q=hello&page=2&limit=10 HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_EQUAL_STRING("/search", req.path().toString().c_str());
    TEST_ASSERT_EQUAL_STRING("hello", req.queryParam("q").toString().c_str());
    TEST_ASSERT_EQUAL_STRING("2", req.queryParam("page").toString().c_str());
    TEST_ASSERT_EQUAL_STRING("10", req.queryParam("limit").toString().c_str());
}

void test_query_param_empty_value() {
    HttpRequest req;
    const char* raw = "GET /api?flag= HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.hasQueryParam("flag"));
    TEST_ASSERT_TRUE(req.queryParam("flag").empty());
}

void test_query_param_no_value() {
    HttpRequest req;
    const char* raw = "GET /api?flag HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.hasQueryParam("flag"));
    TEST_ASSERT_TRUE(req.queryParam("flag").empty());
}

void test_query_param_missing() {
    HttpRequest req;
    const char* raw = "GET /api?foo=bar HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_FALSE(req.hasQueryParam("missing"));
    TEST_ASSERT_TRUE(req.queryParam("missing").empty());
}

void test_query_param_no_query_string() {
    HttpRequest req;
    const char* raw = "GET /api HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_FALSE(req.hasQueryParam("anything"));
    TEST_ASSERT_TRUE(req.queryParam("anything").empty());
}

void test_query_param_with_path_param() {
    HttpRequest req;
    const char* raw = "GET /api/user/123?details=full HTTP/1.1\r\n\r\n";
    req.parse(raw, strlen(raw));

    TEST_ASSERT_TRUE(req.match("GET", "/api/user/{id}"));
    TEST_ASSERT_EQUAL_STRING("123", req.pathParam("id").toString().c_str());
    TEST_ASSERT_EQUAL_STRING("full", req.queryParam("details").toString().c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Basic parsing
    RUN_TEST(test_simple_get_request);
    RUN_TEST(test_post_request_with_body);
    RUN_TEST(test_invalid_request);
    RUN_TEST(test_empty_request);
    RUN_TEST(test_invalid_factory);

    // Headers
    RUN_TEST(test_header_access);
    RUN_TEST(test_header_case_insensitive);

    // Path matching
    RUN_TEST(test_match_simple_path);
    RUN_TEST(test_match_with_path_param);
    RUN_TEST(test_match_with_multiple_path_params);
    RUN_TEST(test_match_trailing_slash);
    RUN_TEST(test_match_pattern_trailing_slash);
    RUN_TEST(test_match_clears_previous_params);
    RUN_TEST(test_match_fails_clears_params);
    RUN_TEST(test_match_empty_path_param_fails);
    RUN_TEST(test_match_segment_count_mismatch);
    RUN_TEST(test_match_root_path);
    RUN_TEST(test_match_complex_pattern);

    // Query parameters
    RUN_TEST(test_query_param_single);
    RUN_TEST(test_query_param_multiple);
    RUN_TEST(test_query_param_empty_value);
    RUN_TEST(test_query_param_no_value);
    RUN_TEST(test_query_param_missing);
    RUN_TEST(test_query_param_no_query_string);
    RUN_TEST(test_query_param_with_path_param);

    // toString
    RUN_TEST(test_toString);
    RUN_TEST(test_toString_invalid);

    return UNITY_END();
}

#endif // NATIVE_TEST
