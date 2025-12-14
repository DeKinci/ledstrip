#include <Arduino.h>
#include <unity.h>
#include <HttpRequest.h>

void test_simple_get_request() {
    HttpRequest req;
    String raw = "GET /ping HTTP/1.1\r\n\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_TRUE(req.isValid());
    TEST_ASSERT_EQUAL_STRING("GET", req.method().c_str());
    TEST_ASSERT_EQUAL_STRING("/ping", req.path().c_str());
    TEST_ASSERT_EQUAL_STRING("HTTP/1.1", req.version().c_str());
    TEST_ASSERT_EQUAL_STRING("", req.body().c_str());
}

void test_get_request_with_headers() {
    HttpRequest req;
    String raw = "GET /api/shader HTTP/1.1\r\n"
                 "Host: 10.0.0.96\r\n"
                 "User-Agent: Test/1.0\r\n"
                 "Accept: application/json\r\n"
                 "\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_EQUAL_STRING("GET", req.method().c_str());
    TEST_ASSERT_EQUAL_STRING("/api/shader", req.path().c_str());
    TEST_ASSERT_EQUAL_STRING("10.0.0.96", req.header("Host").c_str());
    TEST_ASSERT_EQUAL_STRING("Test/1.0", req.header("User-Agent").c_str());
    TEST_ASSERT_EQUAL_STRING("application/json", req.header("Accept").c_str());
}

void test_post_request_with_body() {
    HttpRequest req;
    String raw = "POST /api/shader HTTP/1.1\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: 27\r\n"
                 "\r\n"
                 "{\"name\":\"test\",\"code\":\"x\"}";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_EQUAL_STRING("POST", req.method().c_str());
    TEST_ASSERT_EQUAL_STRING("/api/shader", req.path().c_str());
    TEST_ASSERT_EQUAL_STRING("application/json",
                             req.header("Content-Type").c_str());
    TEST_ASSERT_EQUAL_STRING("27", req.header("Content-Length").c_str());
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"test\",\"code\":\"x\"}",
                             req.body().c_str());
}

void test_delete_request() {
    HttpRequest req;
    String raw = "DELETE /api/shader/myshader HTTP/1.1\r\n"
                 "Host: 10.0.0.96\r\n"
                 "\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_EQUAL_STRING("DELETE", req.method().c_str());
    TEST_ASSERT_EQUAL_STRING("/api/shader/myshader", req.path().c_str());
}

void test_path_helpers() {
    HttpRequest req;
    String raw = "GET /api/shader/myshader HTTP/1.1\r\n\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_TRUE(req.pathStartsWith("/api/shader/"));
    TEST_ASSERT_EQUAL_STRING("myshader",
                             req.pathSuffix("/api/shader/").c_str());

    TEST_ASSERT_TRUE(req.pathStartsWith("/api/"));
    TEST_ASSERT_EQUAL_STRING("shader/myshader",
                             req.pathSuffix("/api/").c_str());

    TEST_ASSERT_FALSE(req.pathStartsWith("/ble/"));
    TEST_ASSERT_EQUAL_STRING("", req.pathSuffix("/ble/").c_str());
}

void test_header_case_insensitive() {
    HttpRequest req;
    String raw = "GET /test HTTP/1.1\r\n"
                 "Content-Type: text/plain\r\n"
                 "\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_EQUAL_STRING("text/plain", req.header("Content-Type").c_str());
    TEST_ASSERT_EQUAL_STRING("text/plain", req.header("content-type").c_str());
    TEST_ASSERT_EQUAL_STRING("text/plain", req.header("CONTENT-TYPE").c_str());
}

void test_missing_header() {
    HttpRequest req;
    String raw = "GET /test HTTP/1.1\r\n\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_EQUAL_STRING("", req.header("Content-Type").c_str());
    TEST_ASSERT_FALSE(req.hasHeader("Content-Type"));
}

void test_invalid_request_no_separator() {
    HttpRequest req;
    String raw = "GET /ping HTTP/1.1\r\n";  // Missing \r\n\r\n

    TEST_ASSERT_FALSE(req.parse(raw));
    TEST_ASSERT_FALSE(req.isValid());
}

void test_invalid_request_bad_format() {
    HttpRequest req;
    String raw = "INVALID\r\n\r\n";

    TEST_ASSERT_FALSE(req.parse(raw));
    TEST_ASSERT_FALSE(req.isValid());
}

void test_empty_request() {
    HttpRequest req;
    String raw = "";

    TEST_ASSERT_FALSE(req.parse(raw));
    TEST_ASSERT_FALSE(req.isValid());
}

void test_root_path() {
    HttpRequest req;
    String raw = "GET / HTTP/1.1\r\n\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_EQUAL_STRING("GET", req.method().c_str());
    TEST_ASSERT_EQUAL_STRING("/", req.path().c_str());
}

void test_long_path() {
    HttpRequest req;
    String raw = "GET /api/ble/connect/AA:BB:CC:DD:EE:FF HTTP/1.1\r\n\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_EQUAL_STRING("/api/ble/connect/AA:BB:CC:DD:EE:FF",
                             req.path().c_str());
    TEST_ASSERT_TRUE(req.pathStartsWith("/api/ble/connect/"));
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF",
                             req.pathSuffix("/api/ble/connect/").c_str());
}

void test_header_with_spaces() {
    HttpRequest req;
    String raw = "GET /test HTTP/1.1\r\n"
                 "Content-Type:  application/json  \r\n"
                 "\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    // Should trim leading spaces after colon
    String contentType = req.header("Content-Type");
    TEST_ASSERT_TRUE(contentType.startsWith("application/json"));
}

void test_multiline_body() {
    HttpRequest req;
    String raw = "POST /api/shader HTTP/1.1\r\n"
                 "Content-Length: 20\r\n"
                 "\r\n"
                 "line1\n"
                 "line2\n"
                 "line3";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_EQUAL_STRING("line1\nline2\nline3", req.body().c_str());
    TEST_ASSERT_EQUAL(20, req.body().length());
}

void test_http_10() {
    HttpRequest req;
    String raw = "GET /test HTTP/1.0\r\n\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_EQUAL_STRING("HTTP/1.0", req.version().c_str());
}

void test_path_with_query_string() {
    HttpRequest req;
    String raw = "GET /api/test?param=value HTTP/1.1\r\n\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    // Query string should be stripped from path
    TEST_ASSERT_EQUAL_STRING("/api/test", req.path().c_str());
}

void test_real_browser_request() {
    HttpRequest req;
    String raw = "GET /ping HTTP/1.1\r\n"
                 "Host: 10.0.0.96\r\n"
                 "Connection: keep-alive\r\n"
                 "Cache-Control: max-age=0\r\n"
                 "Upgrade-Insecure-Requests: 1\r\n"
                 "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                 "AppleWebKit/537.36\r\n"
                 "Accept: text/html,application/xhtml+xml\r\n"
                 "Accept-Encoding: gzip, deflate\r\n"
                 "Accept-Language: en,ru;q=0.9\r\n"
                 "\r\n";

    TEST_ASSERT_TRUE(req.parse(raw));
    TEST_ASSERT_EQUAL_STRING("GET", req.method().c_str());
    TEST_ASSERT_EQUAL_STRING("/ping", req.path().c_str());
    TEST_ASSERT_EQUAL_STRING("10.0.0.96", req.header("Host").c_str());
    TEST_ASSERT_EQUAL_STRING("keep-alive", req.header("Connection").c_str());
    TEST_ASSERT_TRUE(req.header("User-Agent").startsWith("Mozilla"));
}

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_simple_get_request);
    RUN_TEST(test_get_request_with_headers);
    RUN_TEST(test_post_request_with_body);
    RUN_TEST(test_delete_request);
    RUN_TEST(test_path_helpers);
    RUN_TEST(test_header_case_insensitive);
    RUN_TEST(test_missing_header);
    RUN_TEST(test_invalid_request_no_separator);
    RUN_TEST(test_invalid_request_bad_format);
    RUN_TEST(test_empty_request);
    RUN_TEST(test_root_path);
    RUN_TEST(test_long_path);
    RUN_TEST(test_header_with_spaces);
    RUN_TEST(test_multiline_body);
    RUN_TEST(test_http_10);
    RUN_TEST(test_path_with_query_string);
    RUN_TEST(test_real_browser_request);

    UNITY_END();
}

void loop() {
    // Nothing here
}
