//
//  VROReactVisionAuth.h
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#ifndef VROReactVisionAuth_h
#define VROReactVisionAuth_h

#include <map>
#include <mutex>
#include <string>

/**
 * Consumers read this per request or connect, so a refreshed token applies
 * without rebuilding them, and a session wins over a manifest API key.
 */
class VROReactVisionAuth {
public:

    static VROReactVisionAuth &get() {
        // Never destroyed: detached request threads can still read it during exit.
        static VROReactVisionAuth *instance = new VROReactVisionAuth();
        return *instance;
    }

    /**
     * An empty baseUrl or accessToken clears the session. `functionRegion` is
     * the session project's database region, sent as x-region on platform
     * requests so its edge functions run beside the database. Empty adds none
     * here; ReactVisionCCA still pins the default platform URL itself.
     */
    void setSession(const std::string &baseUrl, const std::string &accessToken,
                    const std::string &clientTag, const std::string &functionRegion = "") {
        std::string url = baseUrl;
        while (!url.empty() && url.back() == '/') {
            url.pop_back();
        }

        std::lock_guard<std::mutex> lock(_mutex);
        if (url.empty() || accessToken.empty()) {
            _baseUrl.clear();
            _accessToken.clear();
            _clientTag.clear();
            _functionRegion.clear();
            return;
        }
        _baseUrl = url;
        _accessToken = accessToken;
        _clientTag = clientTag;
        _functionRegion = functionRegion;
    }

    void clearSession() {
        setSession("", "", "");
    }

    bool hasSession() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return !_accessToken.empty();
    }

    /**
     * What a request fails with, before it is sent, when there is neither a
     * session nor a key — after clearSession() on a session-only app, say.
     */
    static constexpr const char *kNoCredentialsError =
        "Not signed in: no ReactVision session or API key";

    /** True when a request could authenticate: a session, or `apiKey`. */
    bool hasCredentials(const std::string &apiKey) const {
        return !apiKey.empty() || hasSession();
    }

    /** No trailing slash. Empty without a session. */
    std::string sessionBaseUrl() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _baseUrl;
    }

    /** Authorization, plus x-rv-client when a tag was set. Empty without a session. */
    std::map<std::string, std::string> sessionHeaders() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return headersLocked();
    }

    /**
     * Base URL and headers read under one lock, so a request never pairs one
     * session's URL with another's token. Leaves both untouched and returns
     * false without a session. For platform requests, so the headers carry
     * x-region too; sessionHeaders() leaves it out, since the co-location
     * relay is not an edge function.
     */
    bool getSession(std::string &baseUrl, std::map<std::string, std::string> &headers) const {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_accessToken.empty()) {
            return false;
        }
        baseUrl = _baseUrl;
        headers = headersLocked();
        if (!_functionRegion.empty()) {
            headers["x-region"] = _functionRegion;
        }
        return true;
    }

    /** Overrides the manifest project id for cloud anchors. Empty clears the override. */
    void setProjectId(const std::string &projectId) {
        std::lock_guard<std::mutex> lock(_mutex);
        _projectId = projectId;
    }

    std::string projectId() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _projectId;
    }

private:

    VROReactVisionAuth() {}
    VROReactVisionAuth(const VROReactVisionAuth &) = delete;
    VROReactVisionAuth &operator=(const VROReactVisionAuth &) = delete;

    std::map<std::string, std::string> headersLocked() const {
        std::map<std::string, std::string> headers;
        if (_accessToken.empty()) {
            return headers;
        }
        headers["Authorization"] = "Bearer " + _accessToken;
        if (!_clientTag.empty()) {
            headers["x-rv-client"] = _clientTag;
        }
        return headers;
    }

    mutable std::mutex _mutex;
    std::string _baseUrl;
    std::string _accessToken;
    std::string _clientTag;
    std::string _functionRegion;
    std::string _projectId;
};

#endif /* VROReactVisionAuth_h */
