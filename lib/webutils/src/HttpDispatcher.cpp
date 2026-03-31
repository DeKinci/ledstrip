#include "HttpDispatcher.h"

// Normalize pattern by stripping param names: /user/{userid} -> /user/{}
String HttpDispatcher::normalizePattern(const String& pattern) {
    String result;
    bool inBrace = false;
    for (size_t i = 0; i < pattern.length(); i++) {
        char c = pattern[i];
        if (c == '{') {
            inBrace = true;
            result += '{';
        } else if (c == '}') {
            inBrace = false;
            result += '}';
        } else if (!inBrace) {
            result += c;
        }
    }
    return result;
}

bool HttpDispatcher::checkCollision(const String& method, const String& pattern, int priority) {
    String normalized = normalizePattern(pattern);
    for (const auto& route : _routes) {
        if (route.method == method &&
            normalizePattern(route.pattern) == normalized &&
            route.priority == priority) {
            return true;
        }
    }
    return false;
}

void HttpDispatcher::sortIfNeeded() {
    if (!_needsSort) return;
    std::stable_sort(_routes.begin(), _routes.end(),
        [](const Route& a, const Route& b) {
            return a.priority > b.priority;  // Higher priority first
        });
    _needsSort = false;
}

HttpDispatcher::RouteHandle HttpDispatcher::addRoute(const String& method, const String& pattern,
                                                      Handler handler, int priority) {
    if (_warnOnCollision && checkCollision(method, pattern, priority)) {
        Serial.printf("[HttpDispatcher] Warning: collision detected for %s %s (priority %d)\n",
                      method.c_str(), pattern.c_str(), priority);
    }

    int newId = _nextId++;
    _routes.push_back({newId, method, pattern, handler, priority});
    _needsSort = true;  // Mark for sorting on next dispatch

    return {newId};
}

// Main registration method
HttpDispatcher::RouteHandle HttpDispatcher::on(const String& method, const String& pattern,
                                                Handler handler, int priority) {
    return addRoute(method, pattern, handler, priority);
}

// GET convenience method
HttpDispatcher::RouteHandle HttpDispatcher::onGet(const String& pattern, Handler handler, int priority) {
    return on("GET", pattern, handler, priority);
}

// POST convenience method
HttpDispatcher::RouteHandle HttpDispatcher::onPost(const String& pattern, Handler handler, int priority) {
    return on("POST", pattern, handler, priority);
}

// PUT convenience method
HttpDispatcher::RouteHandle HttpDispatcher::onPut(const String& pattern, Handler handler, int priority) {
    return on("PUT", pattern, handler, priority);
}

// DELETE convenience method
HttpDispatcher::RouteHandle HttpDispatcher::onDelete(const String& pattern, Handler handler, int priority) {
    return on("DELETE", pattern, handler, priority);
}

bool HttpDispatcher::off(RouteHandle handle) {
    if (!handle.valid()) return false;

    auto it = std::find_if(_routes.begin(), _routes.end(),
        [&handle](const Route& route) {
            return route.id == handle.id;
        });

    if (it != _routes.end()) {
        _routes.erase(it);
        return true;
    }
    return false;
}

bool HttpDispatcher::off(const String& method, const String& pattern) {
    auto it = std::find_if(_routes.begin(), _routes.end(),
        [&method, &pattern](const Route& route) {
            return route.method == method && route.pattern == pattern;
        });

    if (it != _routes.end()) {
        _routes.erase(it);
        return true;
    }
    return false;
}

HttpResponse HttpDispatcher::dispatch(HttpRequest& req) {
    sortIfNeeded();  // Sort only if routes changed since last dispatch

    // Routes are sorted by priority (highest first)
    for (auto& route : _routes) {
        if (req.match(route.method, route.pattern)) {
            return route.handler(req);
        }
    }

    // Default 404 if no handler set
    if (_notFoundHandler) {
        return _notFoundHandler(req);
    }
    return HttpResponse::notFound();
}

void HttpDispatcher::onNotFound(Handler handler) {
    _notFoundHandler = handler;
}

void HttpDispatcher::clear() {
    _routes.clear();
}