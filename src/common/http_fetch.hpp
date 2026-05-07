#ifndef TULPAR_COMMON_HTTP_FETCH_HPP
#define TULPAR_COMMON_HTTP_FETCH_HPP

#include <string>

namespace tulpar {

// Plain C++ HTTP/1.0 fetcher — no TLS, no redirects, no chunked. Used
// by both the runtime's `aot_http_request` builtin and the package
// manager's registry-side `tulpar pkg install` so they don't grow two
// parallel HTTP implementations.
//
// On success, writes the response body into `out_body` and returns
// true. On failure, sets `out_err` to a short reason and returns false.
// `out_status` receives the HTTP status code on success.
bool http_fetch_url(const std::string &url, std::string &out_body,
                    int &out_status, std::string &out_err);

// More general: arbitrary method + body. Used by the runtime's
// `aot_http_request` builtin so client code can POST/PUT/DELETE in
// addition to GET. `out_full` receives the entire response (status
// line + headers + `\r\n\r\n` + body) so the caller can do its own
// parsing — easier than threading a structured result back through
// VMValue here.
//
// `extra_headers` is appended verbatim to the request after the
// auto-generated `Host` / `User-Agent` / `Content-Length` / `Content-Type`
// lines. Each header MUST end in `\r\n`; pass empty string when none are
// needed. Used by `tulpar pkg publish` to add `Authorization: Bearer <…>`
// without forking another HTTP path.
bool http_request_url(const std::string &method, const std::string &url,
                      const std::string &body, std::string &out_full,
                      std::string &out_err,
                      const std::string &extra_headers = "");

}  // namespace tulpar

#endif  // TULPAR_COMMON_HTTP_FETCH_HPP
