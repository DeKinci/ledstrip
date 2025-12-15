#ifndef HTTP_DISPATCHER_H
#define HTTP_DISPATCHER_H

#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <MicroFunction.h>
#include "HttpRequest.h"
#include "HttpResponse.h"

class HttpDispatcher {
public:
    // Handler type: MicroFunction with 8-byte buffer for capturing 'this' pointer
    using Handler = microcore::MicroFunction<HttpResponse(HttpRequest&), 8>;

    struct RouteHandle {
        int id;
        bool valid() const { return id >= 0; }
        static RouteHandle invalid() { return {-1}; }
    };

    HttpDispatcher() = default;

    // Register routes - returns handle for later removal
    // Priority: higher values match first (default 0, use positive for overrides)
    RouteHandle on(const String& method, const String& pattern, Handler handler, int priority = 0);

    // Void handler overload - returns 200 OK
    template<typename F>
    auto on(const String& method, const String& pattern, F handler, int priority = 0)
        -> std::enable_if_t<std::is_void_v<decltype(handler(std::declval<HttpRequest&>()))>, RouteHandle>
    {
        return addRoute(method, pattern, [handler](HttpRequest& req) -> HttpResponse {
            handler(req);
            return HttpResponse::ok();
        }, priority);
    }

    // Convenience registration methods
    RouteHandle onGet(const String& pattern, Handler handler, int priority = 0);
    RouteHandle onPost(const String& pattern, Handler handler, int priority = 0);
    RouteHandle onPut(const String& pattern, Handler handler, int priority = 0);
    RouteHandle onDelete(const String& pattern, Handler handler, int priority = 0);

    // Void handler convenience methods
    template<typename F>
    auto onGet(const String& pattern, F handler, int priority = 0)
        -> std::enable_if_t<std::is_void_v<decltype(handler(std::declval<HttpRequest&>()))>, RouteHandle>
    { return on("GET", pattern, handler, priority); }

    template<typename F>
    auto onPost(const String& pattern, F handler, int priority = 0)
        -> std::enable_if_t<std::is_void_v<decltype(handler(std::declval<HttpRequest&>()))>, RouteHandle>
    { return on("POST", pattern, handler, priority); }

    template<typename F>
    auto onPut(const String& pattern, F handler, int priority = 0)
        -> std::enable_if_t<std::is_void_v<decltype(handler(std::declval<HttpRequest&>()))>, RouteHandle>
    { return on("PUT", pattern, handler, priority); }

    template<typename F>
    auto onDelete(const String& pattern, F handler, int priority = 0)
        -> std::enable_if_t<std::is_void_v<decltype(handler(std::declval<HttpRequest&>()))>, RouteHandle>
    { return on("DELETE", pattern, handler, priority); }

    // Remove a route by handle
    bool off(RouteHandle handle);

    // Remove route by method and pattern (removes first match)
    bool off(const String& method, const String& pattern);

    // Dispatch request - returns response
    HttpResponse dispatch(HttpRequest& req);

    // Set handler for unmatched routes (default: 404)
    void onNotFound(Handler handler);

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
        Handler handler;
        int priority;
    };

    std::vector<Route> _routes;
    int _nextId = 0;
    bool _warnOnCollision = true;
    bool _needsSort = false;
    Handler _notFoundHandler;

    RouteHandle addRoute(const String& method, const String& pattern, Handler handler, int priority);
    void sortIfNeeded();
    bool checkCollision(const String& method, const String& pattern, int priority);
    static String normalizePattern(const String& pattern);
};

#endif