module;

#include <curl/curl.h>

#include <cstddef>
#include <expected>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module roboslop.platform.http;

import roboslop.core.error;

namespace roboslop {

export enum class HttpError : int {
    InitFailed = 1,
    TransportFailed = 2,
};

export [[nodiscard]] auto toError(HttpError e, std::string ctx = {}) -> Error {
    switch (e) {
    case HttpError::InitFailed:
        return {
            .category = "roboslop.platform.http",
            .code = static_cast<int>(e),
            .message = "failed to initialise libcurl",
            .context = std::move(ctx)
        };
    case HttpError::TransportFailed:
        return {
            .category = "roboslop.platform.http",
            .code = static_cast<int>(e),
            .message = "HTTP request failed before a response arrived",
            .context = std::move(ctx)
        };
    }
    return {
        .category = "roboslop.platform.http",
        .code = 0,
        .message = "unknown HttpError",
        .context = std::move(ctx)
    };
}

export struct HttpHeader {
    std::string name{};
    std::string value{};
};

export struct HttpRequest {
    std::string url{};
    std::string method = "POST";
    std::vector<HttpHeader> headers{};
    std::string body{};
    long timeoutSeconds = 60;
};

// A response that arrived, whatever its status. 4xx/5xx are reported
// here, not as an Error — only transport-level failures (DNS, refused
// connection, TLS, timeout) are Errors.
export struct HttpResponse {
    long status = 0;
    std::string body{};
};

namespace detail {

auto ensureCurlGlobalInit() -> bool {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] { ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; });
    return ok;
}

auto writeToString(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) -> std::size_t {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

} // namespace detail

// Blocking HTTP(S) request over libcurl. Intended for worker threads
// (LLM backends run it from std::async), never from the frame loop.
// TLS uses libcurl's default (OpenSSL via Conan on Linux) with
// certificate verification on.
export [[nodiscard]] auto httpRequest(const HttpRequest& req) -> Result<HttpResponse> {
    if (!detail::ensureCurlGlobalInit()) {
        return std::unexpected(toError(HttpError::InitFailed, "curl_global_init"));
    }
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return std::unexpected(toError(HttpError::InitFailed, "curl_easy_init"));
    }

    HttpResponse response;
    curl_slist* headerList = nullptr;
    for (const auto& h : req.headers) {
        headerList = curl_slist_append(headerList, (h.name + ": " + h.value).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
    if (!req.body.empty() || req.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
    }
    if (headerList != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, detail::writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, req.timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "roboslop/0.0.1");

    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    }
    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        return std::unexpected(
            toError(HttpError::TransportFailed, std::string{curl_easy_strerror(rc)})
        );
    }
    return response;
}

} // namespace roboslop
