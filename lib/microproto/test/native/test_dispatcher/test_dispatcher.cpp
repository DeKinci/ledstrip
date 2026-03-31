#include <unity.h>
#include "HttpDispatcher.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "ResponseBuffer.h"

static ResponseBuffer resBuf;

void setUp(void) { resBuf.reset(); }
void tearDown(void) {}

// Persistent buffer for request data (buffer must outlive HttpRequest)
static char requestBuffer[1024];

// Helper to create a request - stores data in persistent buffer
void makeRequest(HttpRequest& req, const char* method, const char* path) {
    String raw = String(method) + " " + path + " HTTP/1.1\r\n\r\n";
    memcpy(requestBuffer, raw.c_str(), raw.length() + 1);
    req.parse(requestBuffer, raw.length());
}

// ============================================================================
// Basic Registration and Dispatch
// ============================================================================

void test_dispatcher_basic_get() {
    HttpDispatcher dispatcher;

    dispatcher.onGet("/ping", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("pong");
    });

    HttpRequest req;
    makeRequest(req, "GET", "/ping");
    HttpResponse res = dispatcher.dispatch(req, resBuf);

    TEST_ASSERT_EQUAL(200, res.statusCode());
    TEST_ASSERT_EQUAL_STRING("pong", res.bodyData());
}

void test_dispatcher_basic_post() {
    HttpDispatcher dispatcher;

    dispatcher.onPost("/data", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::json("{\"ok\":true}");
    });

    HttpRequest req;
    makeRequest(req, "POST", "/data");
    HttpResponse res = dispatcher.dispatch(req, resBuf);

    TEST_ASSERT_EQUAL(200, res.statusCode());
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", res.bodyData());
}

void test_dispatcher_not_found() {
    HttpDispatcher dispatcher;

    dispatcher.onGet("/exists", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("found");
    });

    HttpRequest req;
    makeRequest(req, "GET", "/notexists");
    HttpResponse res = dispatcher.dispatch(req, resBuf);

    TEST_ASSERT_EQUAL(404, res.statusCode());
}

void test_dispatcher_method_mismatch() {
    HttpDispatcher dispatcher;

    dispatcher.onGet("/resource", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("get");
    });

    // POST to a GET-only endpoint
    HttpRequest req;
    makeRequest(req, "POST", "/resource");
    HttpResponse res = dispatcher.dispatch(req, resBuf);

    TEST_ASSERT_EQUAL(404, res.statusCode());
}

// ============================================================================
// Path Parameters
// ============================================================================

void test_dispatcher_path_params() {
    HttpDispatcher dispatcher;

    dispatcher.onGet("/user/{id}", [](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
        buf.write("user:");
        auto id = req.pathParam("id");
        buf.write(id.data(), id.length());
        return HttpResponse::text(buf.data(), buf.length());
    });

    HttpRequest req;
    makeRequest(req, "GET", "/user/42");
    HttpResponse res = dispatcher.dispatch(req, resBuf);

    TEST_ASSERT_EQUAL(200, res.statusCode());
    TEST_ASSERT_EQUAL_STRING("user:42", res.bodyData());
}

void test_dispatcher_multiple_path_params() {
    HttpDispatcher dispatcher;

    dispatcher.onGet("/org/{org}/user/{user}", [](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
        auto org = req.pathParam("org");
        auto user = req.pathParam("user");
        buf.write(org.data(), org.length());
        buf.write("/");
        buf.write(user.data(), user.length());
        return HttpResponse::text(buf.data(), buf.length());
    });

    HttpRequest req;
    makeRequest(req, "GET", "/org/acme/user/john");
    HttpResponse res = dispatcher.dispatch(req, resBuf);

    TEST_ASSERT_EQUAL(200, res.statusCode());
    TEST_ASSERT_EQUAL_STRING("acme/john", res.bodyData());
}

// ============================================================================
// Priority Routing
// ============================================================================

void test_dispatcher_priority_higher_wins() {
    HttpDispatcher dispatcher;

    // Add low priority route first
    dispatcher.onGet("/api/{path}", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("catch-all");
    }, 0);

    // Add high priority route second
    dispatcher.onGet("/api/special", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("special");
    }, 10);

    HttpRequest req;
    makeRequest(req, "GET", "/api/special");
    HttpResponse res = dispatcher.dispatch(req, resBuf);

    // High priority should match first
    TEST_ASSERT_EQUAL(200, res.statusCode());
    TEST_ASSERT_EQUAL_STRING("special", res.bodyData());
}

void test_dispatcher_priority_captive_portal_pattern() {
    HttpDispatcher dispatcher;

    // Normal routes at priority 0
    dispatcher.onGet("/", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("home");
    }, 0);

    dispatcher.onGet("/settings", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("settings");
    }, 0);

    // Captive portal override at priority 100
    auto portalHandle = dispatcher.onGet("/", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("captive-portal");
    }, 100);

    // Should get captive portal
    HttpRequest req1;
    makeRequest(req1, "GET", "/");
    HttpResponse res1 = dispatcher.dispatch(req1, resBuf);
    TEST_ASSERT_EQUAL_STRING("captive-portal", res1.bodyData());

    // Remove captive portal
    dispatcher.off(portalHandle);

    // Should get normal home now
    HttpRequest req2;
    makeRequest(req2, "GET", "/");
    resBuf.reset();
    HttpResponse res2 = dispatcher.dispatch(req2, resBuf);
    TEST_ASSERT_EQUAL_STRING("home", res2.bodyData());
}

// ============================================================================
// Route Removal
// ============================================================================

void test_dispatcher_off_by_handle() {
    HttpDispatcher dispatcher;

    auto handle = dispatcher.onGet("/temp", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("temporary");
    });

    TEST_ASSERT_EQUAL(1, dispatcher.routeCount());

    bool removed = dispatcher.off(handle);
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_EQUAL(0, dispatcher.routeCount());

    // Should now 404
    HttpRequest req;
    makeRequest(req, "GET", "/temp");
    HttpResponse res = dispatcher.dispatch(req, resBuf);
    TEST_ASSERT_EQUAL(404, res.statusCode());
}

void test_dispatcher_off_by_pattern() {
    HttpDispatcher dispatcher;

    dispatcher.onGet("/remove-me", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("here");
    });

    TEST_ASSERT_EQUAL(1, dispatcher.routeCount());

    bool removed = dispatcher.off("GET", "/remove-me");
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_EQUAL(0, dispatcher.routeCount());
}

void test_dispatcher_off_invalid_handle() {
    HttpDispatcher dispatcher;

    HttpDispatcher::RouteHandle invalid = HttpDispatcher::RouteHandle::invalid();
    bool removed = dispatcher.off(invalid);
    TEST_ASSERT_FALSE(removed);
}

// ============================================================================
// Void Handler (returns 200 OK)
// ============================================================================

static bool voidHandlerCalled = false;

void test_dispatcher_void_handler() {
    HttpDispatcher dispatcher;
    voidHandlerCalled = false;

    dispatcher.onPost("/action", [](HttpRequest& req, ResponseBuffer&) {
        voidHandlerCalled = true;
        // No return - void handler returns 200 OK automatically
    });

    HttpRequest req;
    makeRequest(req, "POST", "/action");
    HttpResponse res = dispatcher.dispatch(req, resBuf);

    TEST_ASSERT_TRUE(voidHandlerCalled);
    TEST_ASSERT_EQUAL(200, res.statusCode());
}

// ============================================================================
// Custom Not Found Handler
// ============================================================================

void test_dispatcher_custom_not_found() {
    HttpDispatcher dispatcher;

    dispatcher.onNotFound([](HttpRequest& req, ResponseBuffer& buf) -> HttpResponse {
        buf.printf("{\"error\":\"not found\",\"path\":\"%.*s\"}", (int)req.path().length(), req.path().data());
        return HttpResponse::json(buf.data(), buf.length(), 404);
    });

    HttpRequest req;
    makeRequest(req, "GET", "/missing");
    HttpResponse res = dispatcher.dispatch(req, resBuf);

    TEST_ASSERT_EQUAL(404, res.statusCode());
    TEST_ASSERT_TRUE(res.bodyData() && strstr(res.bodyData(), "missing") != nullptr);
}

// ============================================================================
// Clear Routes
// ============================================================================

void test_dispatcher_clear() {
    HttpDispatcher dispatcher;

    dispatcher.onGet("/a", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("a");
    });
    dispatcher.onGet("/b", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("b");
    });
    dispatcher.onGet("/c", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("c");
    });

    TEST_ASSERT_EQUAL(3, dispatcher.routeCount());

    dispatcher.clear();

    TEST_ASSERT_EQUAL(0, dispatcher.routeCount());

    // All should 404 now
    HttpRequest req;
    makeRequest(req, "GET", "/a");
    HttpResponse res = dispatcher.dispatch(req, resBuf);
    TEST_ASSERT_EQUAL(404, res.statusCode());
}

// ============================================================================
// Collision Detection
// ============================================================================

void test_dispatcher_collision_different_param_names() {
    HttpDispatcher dispatcher;
    dispatcher.setWarnOnCollision(false);  // Disable warnings for this test

    // These should be detected as the same pattern
    dispatcher.onGet("/user/{userid}", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("first");
    });

    dispatcher.onGet("/user/{id}", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("second");
    });

    // Both routes exist (collision warning only, doesn't prevent registration)
    TEST_ASSERT_EQUAL(2, dispatcher.routeCount());

    // First registered wins (both at priority 0)
    HttpRequest req;
    makeRequest(req, "GET", "/user/123");
    HttpResponse res = dispatcher.dispatch(req, resBuf);
    TEST_ASSERT_EQUAL_STRING("first", res.bodyData());
}

void test_dispatcher_no_collision_different_segments() {
    HttpDispatcher dispatcher;

    // These are different patterns - no collision
    dispatcher.onGet("/user/{id}", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("user");
    });

    dispatcher.onGet("/org/{id}", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("org");
    });

    TEST_ASSERT_EQUAL(2, dispatcher.routeCount());

    HttpRequest req1;
    makeRequest(req1, "GET", "/user/123");
    TEST_ASSERT_EQUAL_STRING("user", dispatcher.dispatch(req1, resBuf).bodyData());

    HttpRequest req2;
    makeRequest(req2, "GET", "/org/456");
    TEST_ASSERT_EQUAL_STRING("org", dispatcher.dispatch(req2, resBuf).bodyData());
}

// ============================================================================
// Route Handle Stability After Modifications
// ============================================================================

void test_dispatcher_handle_stability_after_additions() {
    HttpDispatcher dispatcher;

    // Add low priority route, save handle
    auto handleA = dispatcher.onGet("/test", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("A");
    }, 0);

    // Add high priority route (will cause sort, moving A)
    dispatcher.onGet("/test", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("B");
    }, 100);

    // Add medium priority route (another sort)
    dispatcher.onGet("/test", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("C");
    }, 50);

    // handleA should still work despite routes being shuffled
    bool removed = dispatcher.off(handleA);
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_EQUAL(2, dispatcher.routeCount());

    // B (priority 100) should still match first
    HttpRequest req;
    makeRequest(req, "GET", "/test");
    HttpResponse res = dispatcher.dispatch(req, resBuf);
    TEST_ASSERT_EQUAL_STRING("B", res.bodyData());
}

void test_dispatcher_priority_after_removal_and_add() {
    HttpDispatcher dispatcher;

    // Add routes in order
    auto h1 = dispatcher.onGet("/x", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("priority-100");
    }, 100);

    dispatcher.onGet("/x", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("priority-50");
    }, 50);

    dispatcher.onGet("/x", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("priority-0");
    }, 0);

    // Remove highest priority
    dispatcher.off(h1);

    // priority-50 should now match
    HttpRequest req1;
    makeRequest(req1, "GET", "/x");
    TEST_ASSERT_EQUAL_STRING("priority-50", dispatcher.dispatch(req1, resBuf).bodyData());

    // Add new route with priority 75 (should slot between removed 100 and existing 50)
    dispatcher.onGet("/x", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("priority-75");
    }, 75);

    // priority-75 should now match first
    HttpRequest req2;
    makeRequest(req2, "GET", "/x");
    TEST_ASSERT_EQUAL_STRING("priority-75", dispatcher.dispatch(req2, resBuf).bodyData());
}

// ============================================================================
// Multiple Methods Same Path
// ============================================================================

void test_dispatcher_multiple_methods() {
    HttpDispatcher dispatcher;

    dispatcher.onGet("/resource", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("GET");
    });

    dispatcher.onPost("/resource", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("POST");
    });

    dispatcher.onPut("/resource", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("PUT");
    });

    dispatcher.onDelete("/resource", [](HttpRequest& req, ResponseBuffer&) -> HttpResponse {
        return HttpResponse::text("DELETE");
    });

    HttpRequest reqGet;
    makeRequest(reqGet, "GET", "/resource");
    TEST_ASSERT_EQUAL_STRING("GET", dispatcher.dispatch(reqGet, resBuf).bodyData());

    HttpRequest reqPost;
    makeRequest(reqPost, "POST", "/resource");
    TEST_ASSERT_EQUAL_STRING("POST", dispatcher.dispatch(reqPost, resBuf).bodyData());

    HttpRequest reqPut;
    makeRequest(reqPut, "PUT", "/resource");
    TEST_ASSERT_EQUAL_STRING("PUT", dispatcher.dispatch(reqPut, resBuf).bodyData());

    HttpRequest reqDelete;
    makeRequest(reqDelete, "DELETE", "/resource");
    TEST_ASSERT_EQUAL_STRING("DELETE", dispatcher.dispatch(reqDelete, resBuf).bodyData());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Basic
    RUN_TEST(test_dispatcher_basic_get);
    RUN_TEST(test_dispatcher_basic_post);
    RUN_TEST(test_dispatcher_not_found);
    RUN_TEST(test_dispatcher_method_mismatch);

    // Path params
    RUN_TEST(test_dispatcher_path_params);
    RUN_TEST(test_dispatcher_multiple_path_params);

    // Priority
    RUN_TEST(test_dispatcher_priority_higher_wins);
    RUN_TEST(test_dispatcher_priority_captive_portal_pattern);

    // Route removal
    RUN_TEST(test_dispatcher_off_by_handle);
    RUN_TEST(test_dispatcher_off_by_pattern);
    RUN_TEST(test_dispatcher_off_invalid_handle);

    // Void handler
    RUN_TEST(test_dispatcher_void_handler);

    // Custom handlers
    RUN_TEST(test_dispatcher_custom_not_found);

    // Clear
    RUN_TEST(test_dispatcher_clear);

    // Multiple methods
    RUN_TEST(test_dispatcher_multiple_methods);

    // Handle stability
    RUN_TEST(test_dispatcher_handle_stability_after_additions);
    RUN_TEST(test_dispatcher_priority_after_removal_and_add);

    // Collision detection
    RUN_TEST(test_dispatcher_collision_different_param_names);
    RUN_TEST(test_dispatcher_no_collision_different_segments);

    return UNITY_END();
}