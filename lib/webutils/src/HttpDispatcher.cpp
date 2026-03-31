#include <MicroLog.h>
#include "HttpDispatcher.h"
#include <cstring>
#include <BuildInfo.h>

bool HttpDispatcher::checkCollision(const char* method, const char* pattern, int priority) {
    for (const auto& route : _routes) {
        if (strcmp(route.method, method) == 0 &&
            strcmp(route.pattern, pattern) == 0 &&
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
            return a.priority > b.priority;
        });
    _needsSort = false;
}

HttpDispatcher::RouteHandle HttpDispatcher::addRoute(const char* method, const char* pattern,
                                                      Handler handler, int priority, const char* etag) {
    if (_warnOnCollision && checkCollision(method, pattern, priority)) {
        LOG_WARN("HTTP", "Route collision for %s %s (priority %d)",
                      method, pattern, priority);
    }

    int newId = _nextId++;
    _routes.push_back({newId, method, pattern, handler, priority, etag});
    _needsSort = true;

    return {newId};
}

HttpDispatcher::RouteHandle HttpDispatcher::on(const char* method, const char* pattern,
                                                Handler handler, int priority) {
    return addRoute(method, pattern, handler, priority);
}

HttpDispatcher::RouteHandle HttpDispatcher::onGet(const char* pattern, Handler handler, int priority) {
    return on("GET", pattern, handler, priority);
}

HttpDispatcher::RouteHandle HttpDispatcher::onPost(const char* pattern, Handler handler, int priority) {
    return on("POST", pattern, handler, priority);
}

HttpDispatcher::RouteHandle HttpDispatcher::onPut(const char* pattern, Handler handler, int priority) {
    return on("PUT", pattern, handler, priority);
}

HttpDispatcher::RouteHandle HttpDispatcher::onDelete(const char* pattern, Handler handler, int priority) {
    return on("DELETE", pattern, handler, priority);
}

HttpDispatcher::RouteHandle HttpDispatcher::onGet(const char* pattern, Handler handler, const char* etag, int priority) {
    return addRoute("GET", pattern, handler, priority, etag);
}

bool HttpDispatcher::off(RouteHandle handle) {
    if (!handle.valid()) return false;
    auto it = std::find_if(_routes.begin(), _routes.end(),
        [&handle](const Route& route) { return route.id == handle.id; });
    if (it != _routes.end()) { _routes.erase(it); return true; }
    return false;
}

bool HttpDispatcher::off(const char* method, const char* pattern) {
    auto it = std::find_if(_routes.begin(), _routes.end(),
        [method, pattern](const Route& route) {
            return strcmp(route.method, method) == 0 && strcmp(route.pattern, pattern) == 0;
        });
    if (it != _routes.end()) { _routes.erase(it); return true; }
    return false;
}

bool HttpDispatcher::matchEtag(StringView clientEtag, const char* routeEtag) {
    if (clientEtag.empty() || !routeEtag) return false;

    // Strip quotes: "abc123" -> abc123
    const char* d = clientEtag.data();
    size_t len = clientEtag.length();
    if (len >= 2 && d[0] == '"' && d[len - 1] == '"') {
        d++;
        len -= 2;
    }

    size_t etagLen = strlen(routeEtag);
    return len == etagLen && memcmp(d, routeEtag, len) == 0;
}

HttpResponse HttpDispatcher::dispatch(HttpRequest& req, ResponseBuffer& buf) {
    sortIfNeeded();

    // OPTIONS → 204 (CORS preflight, always allowed)
    if (req.method() == "OPTIONS") {
        return HttpResponse::withStatus(204);
    }

    for (auto& route : _routes) {
        if (req.match(route.method, route.pattern)) {
            // ETag short-circuit: check BEFORE calling handler
            if (route.etag) {
                StringView clientEtag = req.header("If-None-Match");
                if (matchEtag(clientEtag, route.etag)) {
                    return HttpResponse::notModified(route.etag);
                }
            }

            HttpResponse res = route.handler(req, buf);

            // Attach route-level etag to response if handler didn't set one
            if (route.etag && !res.etagValue()) {
                res.etag(route.etag);
            }

            return res;
        }
    }

    if (_notFoundHandler) return _notFoundHandler(req, buf);
    return HttpResponse::notFound();
}

const char* HttpDispatcher::globalEtag() {
    if (!_globalEtag) _globalEtag = BuildInfo::firmwareHash();
    return _globalEtag;
}

HttpDispatcher::RouteHandle HttpDispatcher::resource(const char* pattern, const Resource& res) {
    const Resource* r = &res;
    return addRoute("GET", pattern, [r](HttpRequest&, ResponseBuffer&) -> HttpResponse {
        return HttpResponse().status(200).contentType(r->contentType).body(r->data, r->length).encoding(r->encoding);
    }, 0, globalEtag());
}

void HttpDispatcher::onNotFound(Handler handler) {
    _notFoundHandler = handler;
}

void HttpDispatcher::clear() {
    _routes.clear();
}
