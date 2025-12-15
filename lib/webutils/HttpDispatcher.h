#ifndef HTTP_DISPATCHER_H
#define HTTP_DISPATCHER_H

#include <Arduino.h>
#include <functional>
#include <vector>
#include <algorithm>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "HttpStatus.h"

class HttpDispatcher {
public:
    // Handler types
    using ResponseHandler = std::function<HttpResponse(HttpRequest&)>;
    using StringHandler = std::function<String(HttpRequest&)>;
    using StatusHandler = std::function<HttpStatus(HttpRequest&)>;

    struct RouteHandle {
        int id;
        bool valid() const { return id >= 0; }
        static RouteHandle invalid() { return {-1}; }
    };

    HttpDispatcher() = default;

    // Register routes - returns handle for later removal
    // Priority: higher values match first (default 0, use positive for overrides)
    RouteHandle on(const String& method, const String& pattern, ResponseHandler handler, int priority = 0);
    RouteHandle on(const String& method, const String& pattern, StringHandler handler, int priority = 0);
    RouteHandle on(const String& method, const String& pattern, StatusHandler handler, int priority = 0);

    // Convenience registration methods
    RouteHandle onGet(const String& pattern, ResponseHandler handler, int priority = 0);
    RouteHandle onGet(const String& pattern, StringHandler handler, int priority = 0);
    RouteHandle onGet(const String& pattern, StatusHandler handler, int priority = 0);

    RouteHandle onPost(const String& pattern, ResponseHandler handler, int priority = 0);
    RouteHandle onPost(const String& pattern, StringHandler handler, int priority = 0);
    RouteHandle onPost(const String& pattern, StatusHandler handler, int priority = 0);

    RouteHandle onPut(const String& pattern, ResponseHandler handler, int priority = 0);
    RouteHandle onPut(const String& pattern, StringHandler handler, int priority = 0);
    RouteHandle onPut(const String& pattern, StatusHandler handler, int priority = 0);

    RouteHandle onDelete(const String& pattern, ResponseHandler handler, int priority = 0);
    RouteHandle onDelete(const String& pattern, StringHandler handler, int priority = 0);
    RouteHandle onDelete(const String& pattern, StatusHandler handler, int priority = 0);

    // Remove a route by handle
    bool off(RouteHandle handle);

    // Remove route by method and pattern (removes first match)
    bool off(const String& method, const String& pattern);

    // Dispatch request - returns response
    HttpResponse dispatch(HttpRequest& req);

    // Set handler for unmatched routes (default: 404)
    void onNotFound(ResponseHandler handler);

    // Enable/disable collision warnings (default: enabled)
    void setWarnOnCollision(bool warn) { _warnOnCollision = warn; }

    // Get route count
    size_t routeCount() const { return _routes.size(); }

    // Clear all routes
    void clear();

private:
    struct Route {
        int id;
        String method;
        String pattern;
        ResponseHandler handler;
        int priority;
    };

    std::vector<Route> _routes;
    int _nextId = 0;
    bool _warnOnCollision = true;
    bool _needsSort = false;
    ResponseHandler _notFoundHandler = defaultNotFound;

    RouteHandle addRoute(const String& method, const String& pattern, ResponseHandler handler, int priority);
    void sortIfNeeded();
    bool checkCollision(const String& method, const String& pattern, int priority);
    static String normalizePattern(const String& pattern);
    static HttpResponse defaultNotFound(HttpRequest& req);
};

#endif