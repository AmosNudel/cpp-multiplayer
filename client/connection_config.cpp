#include "connection_config.hpp"

#include "common/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#if defined(PLATFORM_WEB)
#include <emscripten.h>

EM_JS(const char*, GetBrowserHostname, (), {
    return stringToNewUTF8(window.location.hostname);
});

EM_JS(int, IsBrowserHttps, (), {
    return window.location.protocol === "https:" ? 1 : 0;
});
#endif

namespace net {
namespace {

bool IsLocalHost(const std::string& host) {
    return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

bool EnvBool(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

std::string ResolveWebSocketHost() {
    std::string host = EnvString("WS_HOST", "");
    if (!host.empty()) {
        return host;
    }

#if defined(WS_HOST_DEFAULT)
    if (std::string(WS_HOST_DEFAULT).size() > 0) {
        return WS_HOST_DEFAULT;
    }
#endif

#if defined(PLATFORM_WEB)
    std::string browserHost;
    const char* browserHostCStr = GetBrowserHostname();
    if (browserHostCStr && browserHostCStr[0] != '\0') {
        browserHost = browserHostCStr;
        free(const_cast<char*>(browserHostCStr));
        // Only use browser localhost when no production host is baked into the build.
        if (IsLocalHost(browserHost)) {
            return browserHost;
        }
    }
#endif

    host = EnvString("SERVER_HOST", "");
    if (!host.empty()) {
        return host;
    }

#if defined(PLATFORM_WEB)
    if (!browserHost.empty()) {
        return browserHost;
    }
#endif

    return "localhost";
}

bool ShouldUseTls(const std::string& host) {
    if (HasEnv("WS_TLS")) {
        return EnvBool("WS_TLS", false);
    }

#if defined(PLATFORM_WEB)
    if (IsBrowserHttps()) {
        return true;
    }
#endif

    return !IsLocalHost(host);
}

bool IsDefaultPort(bool useTls, uint16_t port) {
    return (useTls && port == 443) || (!useTls && port == 80);
}

}  // namespace

std::string BuildWebSocketUrl() {
    const std::string host = ResolveWebSocketHost();
    const bool useTls = ShouldUseTls(host);
    const char* scheme = useTls ? "wss" : "ws";

    uint16_t port = 0;
    if (HasEnv("WS_PORT")) {
        port = EnvPort("WS_PORT", useTls ? 443 : kDefaultWsPort);
#if defined(WS_PORT_DEFAULT)
    } else if (WS_PORT_DEFAULT > 0) {
        port = static_cast<uint16_t>(WS_PORT_DEFAULT);
#endif
    } else if (!useTls) {
        port = kDefaultWsPort;
    } else {
        port = 443;
    }

    if (IsDefaultPort(useTls, port)) {
        return std::string(scheme) + "://" + host;
    }

    return std::string(scheme) + "://" + host + ":" + std::to_string(port);
}

DesktopEndpoint GetDesktopEndpoint() {
    DesktopEndpoint endpoint;

    if (HasEnv("SERVER_HOST")) {
        endpoint.host = EnvString("SERVER_HOST", "");
    }
#if defined(SERVER_HOST_DEFAULT)
    else if (std::string(SERVER_HOST_DEFAULT).size() > 0) {
        endpoint.host = SERVER_HOST_DEFAULT;
    }
#endif
    else {
        endpoint.host = "127.0.0.1";
    }

    if (HasEnv("SERVER_PORT")) {
        endpoint.port = EnvPort("SERVER_PORT", kDefaultTcpPort);
    }
#if defined(SERVER_PORT_DEFAULT)
    else if (SERVER_PORT_DEFAULT > 0) {
        endpoint.port = static_cast<uint16_t>(SERVER_PORT_DEFAULT);
    }
#endif
    else {
        endpoint.port = kDefaultTcpPort;
    }

    return endpoint;
}

}  // namespace net
