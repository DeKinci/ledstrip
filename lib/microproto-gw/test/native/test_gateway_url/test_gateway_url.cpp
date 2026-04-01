#include <unity.h>
#include <Arduino.h>
#include <GatewayUrl.h>

using namespace GatewayClient;

void setUp() {}
void tearDown() {}

// ============== Scheme detection ==============

void test_ws_scheme() {
    ParsedUrl p;
    TEST_ASSERT_TRUE(parseUrl("ws://example.com", p));
    TEST_ASSERT_FALSE(p.ssl);
    TEST_ASSERT_EQUAL_STRING("example.com", p.host.c_str());
}

void test_wss_scheme() {
    ParsedUrl p;
    TEST_ASSERT_TRUE(parseUrl("wss://example.com", p));
    TEST_ASSERT_TRUE(p.ssl);
    TEST_ASSERT_EQUAL_STRING("example.com", p.host.c_str());
}

void test_http_scheme() {
    ParsedUrl p;
    TEST_ASSERT_TRUE(parseUrl("http://example.com", p));
    TEST_ASSERT_FALSE(p.ssl);
}

void test_https_scheme() {
    ParsedUrl p;
    TEST_ASSERT_TRUE(parseUrl("https://example.com", p));
    TEST_ASSERT_TRUE(p.ssl);
}

void test_no_scheme() {
    ParsedUrl p;
    TEST_ASSERT_TRUE(parseUrl("example.com", p));
    TEST_ASSERT_FALSE(p.ssl);
    TEST_ASSERT_EQUAL_STRING("example.com", p.host.c_str());
}

// ============== Port parsing ==============

void test_default_port_http() {
    ParsedUrl p;
    parseUrl("ws://example.com", p);
    TEST_ASSERT_EQUAL(80, p.port);
}

void test_default_port_https() {
    ParsedUrl p;
    parseUrl("wss://example.com", p);
    TEST_ASSERT_EQUAL(443, p.port);
}

void test_explicit_port() {
    ParsedUrl p;
    parseUrl("ws://example.com:8080", p);
    TEST_ASSERT_EQUAL(8080, p.port);
    TEST_ASSERT_EQUAL_STRING("example.com", p.host.c_str());
}

void test_explicit_port_ssl() {
    ParsedUrl p;
    parseUrl("wss://example.com:9443", p);
    TEST_ASSERT_EQUAL(9443, p.port);
    TEST_ASSERT_TRUE(p.ssl);
}

// ============== Path parsing ==============

void test_default_path() {
    ParsedUrl p;
    parseUrl("ws://example.com", p);
    TEST_ASSERT_EQUAL_STRING("/ws/device", p.path.c_str());
}

void test_explicit_path() {
    ParsedUrl p;
    parseUrl("ws://example.com/api/v1/connect", p);
    TEST_ASSERT_EQUAL_STRING("/api/v1/connect", p.path.c_str());
}

void test_root_path() {
    ParsedUrl p;
    parseUrl("ws://example.com/", p);
    TEST_ASSERT_EQUAL_STRING("/", p.path.c_str());
}

void test_path_with_port() {
    ParsedUrl p;
    parseUrl("wss://gw.example.com:443/ws/device", p);
    TEST_ASSERT_EQUAL_STRING("gw.example.com", p.host.c_str());
    TEST_ASSERT_EQUAL(443, p.port);
    TEST_ASSERT_EQUAL_STRING("/ws/device", p.path.c_str());
    TEST_ASSERT_TRUE(p.ssl);
}

// ============== Full URLs ==============

void test_full_ws_url() {
    ParsedUrl p;
    TEST_ASSERT_TRUE(parseUrl("ws://192.168.1.50:81/ws/device", p));
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", p.host.c_str());
    TEST_ASSERT_EQUAL(81, p.port);
    TEST_ASSERT_EQUAL_STRING("/ws/device", p.path.c_str());
    TEST_ASSERT_FALSE(p.ssl);
}

void test_full_wss_url() {
    ParsedUrl p;
    TEST_ASSERT_TRUE(parseUrl("wss://gateway.prod.example.com/ws/device", p));
    TEST_ASSERT_EQUAL_STRING("gateway.prod.example.com", p.host.c_str());
    TEST_ASSERT_EQUAL(443, p.port);
    TEST_ASSERT_EQUAL_STRING("/ws/device", p.path.c_str());
    TEST_ASSERT_TRUE(p.ssl);
}

// ============== Edge cases ==============

void test_empty_url() {
    ParsedUrl p;
    TEST_ASSERT_FALSE(parseUrl("", p));
}

void test_scheme_only() {
    ParsedUrl p;
    TEST_ASSERT_FALSE(parseUrl("ws://", p));
}

void test_ip_no_scheme() {
    ParsedUrl p;
    TEST_ASSERT_TRUE(parseUrl("10.0.0.1:3000/gw", p));
    TEST_ASSERT_EQUAL_STRING("10.0.0.1", p.host.c_str());
    TEST_ASSERT_EQUAL(3000, p.port);
    TEST_ASSERT_EQUAL_STRING("/gw", p.path.c_str());
    TEST_ASSERT_FALSE(p.ssl);
}

void test_localhost() {
    ParsedUrl p;
    TEST_ASSERT_TRUE(parseUrl("ws://localhost:8080", p));
    TEST_ASSERT_EQUAL_STRING("localhost", p.host.c_str());
    TEST_ASSERT_EQUAL(8080, p.port);
}

int main() {
    UNITY_BEGIN();

    // Scheme
    RUN_TEST(test_ws_scheme);
    RUN_TEST(test_wss_scheme);
    RUN_TEST(test_http_scheme);
    RUN_TEST(test_https_scheme);
    RUN_TEST(test_no_scheme);

    // Port
    RUN_TEST(test_default_port_http);
    RUN_TEST(test_default_port_https);
    RUN_TEST(test_explicit_port);
    RUN_TEST(test_explicit_port_ssl);

    // Path
    RUN_TEST(test_default_path);
    RUN_TEST(test_explicit_path);
    RUN_TEST(test_root_path);
    RUN_TEST(test_path_with_port);

    // Full URLs
    RUN_TEST(test_full_ws_url);
    RUN_TEST(test_full_wss_url);

    // Edge cases
    RUN_TEST(test_empty_url);
    RUN_TEST(test_scheme_only);
    RUN_TEST(test_ip_no_scheme);
    RUN_TEST(test_localhost);

    return UNITY_END();
}
