#ifndef FALLOUT_COOP_HTTP_H_
#define FALLOUT_COOP_HTTP_H_

#include <string>

namespace fallout {

// Minimal blocking HTTP/1.0 client for the co-op menu's update check and
// bug reports. Plain http:// only. `timeoutMs` bounds connecting and each
// wait for data.
//
// Returns true when the server answered with a 2xx status; `body` gets the
// response body, `error` a reason otherwise.
bool coop_http_get(const std::string& url, std::string* body, std::string* error, int timeoutMs = 8000);
// Downloads `url` (following redirects) into the file at `path`, without
// holding it in memory. `progress`, when given, is called with the bytes so
// far and the total (0: not known); returning false cancels. The file is
// removed when it fails.
typedef bool(CoopHttpProgress)(void* data, unsigned long long done, unsigned long long total);
bool coop_http_download(const std::string& url, const std::string& path, CoopHttpProgress* progress, void* data, std::string* error, int timeoutMs = 20000);

bool coop_http_post(const std::string& url, const std::string& contentType, const std::string& data, std::string* body, std::string* error, int timeoutMs = 8000);

} // namespace fallout

#endif /* FALLOUT_COOP_HTTP_H_ */
