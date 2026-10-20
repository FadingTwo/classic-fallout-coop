#include "coop_http.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace fallout {

// Largest response accepted (update packages included).
#define HTTP_MAX_RESPONSE (256 * 1024 * 1024)

typedef struct HttpUrl {
    bool secure;
    std::string host;
    std::string port;
    std::string path;
} HttpUrl;

static bool http_parse_url(const std::string& url, HttpUrl* out, std::string* error)
{
    std::string rest;
    if (url.compare(0, 8, "https://") == 0) {
        out->secure = true;
        rest = url.substr(8);
    } else if (url.compare(0, 7, "http://") == 0) {
        out->secure = false;
        rest = url.substr(7);
    } else {
        *error = "Not a web address: " + url;
        return false;
    }

    size_t slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    out->path = slash == std::string::npos ? "/" : rest.substr(slash);

    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        out->host = authority.substr(0, colon);
        out->port = authority.substr(colon + 1);
    } else {
        out->host = authority;
        out->port = out->secure ? "443" : "80";
    }

    if (out->host.empty()) {
        *error = "No host in " + url;
        return false;
    }

    return true;
}

static std::string http_status_error(int status)
{
    char text[64];
    snprintf(text, sizeof(text), "The server answered with error %d.", status);
    return text;
}

#ifdef _WIN32

// -----------------------------------------------------------------------------
// Windows: WinHTTP (system proxy settings and certificate store included).

static std::wstring http_wide(const std::string& text)
{
    int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    std::wstring result(length > 0 ? length - 1 : 0, L'\0');
    if (length > 1) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &result[0], length);
    }
    return result;
}

static bool http_request(const std::string& method, const std::string& urlText, const std::string& contentType, const std::string& data, std::string* body, std::string* error, int timeoutMs)
{
    HttpUrl url;
    if (!http_parse_url(urlText, &url, error)) {
        return false;
    }

    HINTERNET session = WinHttpOpen(L"fallout-ce-coop", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) {
        *error = "Networking unavailable.";
        return false;
    }

    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    bool ok = false;
    HINTERNET connection = WinHttpConnect(session, http_wide(url.host).c_str(), (INTERNET_PORT)atoi(url.port.c_str()), 0);
    HINTERNET request = connection != NULL
        ? WinHttpOpenRequest(connection, http_wide(method).c_str(), http_wide(url.path).c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure ? WINHTTP_FLAG_SECURE : 0)
        : NULL;

    if (request == NULL) {
        *error = "Cannot connect to the server " + url.host + ".";
    } else {
        std::wstring headers;
        if (method == "POST") {
            headers = L"Content-Type: " + http_wide(contentType) + L"\r\n";
        }

        if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0 : (DWORD)-1L, (LPVOID)data.data(), (DWORD)data.size(), (DWORD)data.size(), 0)
            || !WinHttpReceiveResponse(request, NULL)) {
            *error = "Cannot connect to the server " + url.host + ".";
        } else {
            DWORD status = 0;
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);

            body->clear();
            bool readOk = true;
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) {
                    readOk = false;
                    break;
                }
                if (available == 0) {
                    break;
                }

                std::vector<char> buffer(available);
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer.data(), available, &read)) {
                    readOk = false;
                    break;
                }
                body->append(buffer.data(), read);

                if (body->size() > HTTP_MAX_RESPONSE) {
                    readOk = false;
                    break;
                }
            }

            if (!readOk) {
                *error = "The connection was lost.";
            } else if (status < 200 || status > 299) {
                *error = http_status_error((int)status);
            } else {
                ok = true;
            }
        }
    }

    if (request != NULL) {
        WinHttpCloseHandle(request);
    }
    if (connection != NULL) {
        WinHttpCloseHandle(connection);
    }
    WinHttpCloseHandle(session);
    return ok;
}

static bool http_download(const std::string& urlText, FILE* stream, CoopHttpProgress* progress, void* data, std::string* error, int timeoutMs)
{
    HttpUrl url;
    if (!http_parse_url(urlText, &url, error)) {
        return false;
    }

    HINTERNET session = WinHttpOpen(L"fallout-ce-coop", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) {
        *error = "Networking unavailable.";
        return false;
    }

    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    bool ok = false;
    HINTERNET connection = WinHttpConnect(session, http_wide(url.host).c_str(), (INTERNET_PORT)atoi(url.port.c_str()), 0);
    HINTERNET request = connection != NULL
        ? WinHttpOpenRequest(connection, L"GET", http_wide(url.path).c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure ? WINHTTP_FLAG_SECURE : 0)
        : NULL;

    // (WinHTTP follows redirects itself.)
    if (request == NULL || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request, NULL)) {
        *error = "Cannot connect to the server " + url.host + ".";
    } else {
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        wchar_t lengthText[32] = L"";
        size = sizeof(lengthText);
        unsigned long long total = 0;
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &size, WINHTTP_NO_HEADER_INDEX)) {
            total = _wcstoui64(lengthText, NULL, 10);
        }

        if (status < 200 || status > 299) {
            *error = http_status_error((int)status);
        } else {
            unsigned long long done = 0;
            std::vector<char> buffer(256 * 1024);
            ok = true;
            for (;;) {
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer.data(), (DWORD)buffer.size(), &read)) {
                    *error = "The connection was lost.";
                    ok = false;
                    break;
                }
                if (read == 0) {
                    break;
                }
                if (fwrite(buffer.data(), 1, read, stream) != read) {
                    *error = "Could not write the download (is the disk full?).";
                    ok = false;
                    break;
                }
                done += read;
                if (progress != NULL && !progress(data, done, total)) {
                    *error = "Cancelled.";
                    ok = false;
                    break;
                }
            }
            if (ok && total != 0 && done != total) {
                *error = "The download was cut short.";
                ok = false;
            }
        }
    }

    if (request != NULL) {
        WinHttpCloseHandle(request);
    }
    if (connection != NULL) {
        WinHttpCloseHandle(connection);
    }
    WinHttpCloseHandle(session);
    return ok;
}

#else

// -----------------------------------------------------------------------------
// Elsewhere: sockets, and the system's OpenSSL (loaded when needed) for
// https://.

#if defined(MSG_NOSIGNAL)
#define HTTP_SEND_FLAGS MSG_NOSIGNAL
#else
#define HTTP_SEND_FLAGS 0
#endif

typedef struct OpenSsl {
    bool loaded;
    void* library;
    const void* (*TLS_client_method)();
    void* (*SSL_CTX_new)(const void* method);
    int (*SSL_CTX_set_default_verify_paths)(void* context);
    void (*SSL_CTX_set_verify)(void* context, int mode, void* callback);
    void (*SSL_CTX_free)(void* context);
    void* (*SSL_new)(void* context);
    int (*SSL_set_fd)(void* ssl, int fd);
    int (*SSL_set1_host)(void* ssl, const char* host);
    long (*SSL_ctrl)(void* ssl, int command, long value, void* data);
    int (*SSL_connect)(void* ssl);
    int (*SSL_read)(void* ssl, void* buffer, int length);
    int (*SSL_write)(void* ssl, const void* buffer, int length);
    int (*SSL_shutdown)(void* ssl);
    void (*SSL_free)(void* ssl);
    long (*SSL_get_verify_result)(const void* ssl);
} OpenSsl;

#define OPENSSL_VERIFY_PEER 1
#define OPENSSL_CTRL_SET_TLSEXT_HOSTNAME 55
#define OPENSSL_TLSEXT_NAMETYPE_HOST_NAME 0
#define OPENSSL_X509_V_OK 0

static OpenSsl http_openssl;

static bool http_load_openssl()
{
    if (http_openssl.loaded) {
        return http_openssl.library != NULL;
    }
    http_openssl.loaded = true;

    const char* names[] = { "libssl.so.3", "libssl.so.1.1", "libssl.so", "libssl.3.dylib", "libssl.dylib" };
    for (const char* name : names) {
        http_openssl.library = dlopen(name, RTLD_NOW);
        if (http_openssl.library != NULL) {
            break;
        }
    }

    if (http_openssl.library == NULL) {
        return false;
    }

#define HTTP_LOAD(name)                                                                      \
    *(void**)&http_openssl.name = dlsym(http_openssl.library, #name);                        \
    if (http_openssl.name == NULL) {                                                         \
        dlclose(http_openssl.library);                                                       \
        http_openssl.library = NULL;                                                         \
        return false;                                                                        \
    }

    HTTP_LOAD(TLS_client_method);
    HTTP_LOAD(SSL_CTX_new);
    HTTP_LOAD(SSL_CTX_set_default_verify_paths);
    HTTP_LOAD(SSL_CTX_set_verify);
    HTTP_LOAD(SSL_CTX_free);
    HTTP_LOAD(SSL_new);
    HTTP_LOAD(SSL_set_fd);
    HTTP_LOAD(SSL_set1_host);
    HTTP_LOAD(SSL_ctrl);
    HTTP_LOAD(SSL_connect);
    HTTP_LOAD(SSL_read);
    HTTP_LOAD(SSL_write);
    HTTP_LOAD(SSL_shutdown);
    HTTP_LOAD(SSL_free);
    HTTP_LOAD(SSL_get_verify_result);

#undef HTTP_LOAD

    return true;
}

static bool http_wait(int socket, bool forWriting, int timeoutMs)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);

    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;

    return select(socket + 1, forWriting ? NULL : &set, forWriting ? &set : NULL, NULL, &timeout) > 0;
}

// A connected, blocking socket with send/receive timeouts.
static int http_connect(const HttpUrl& url, int timeoutMs, std::string* error)
{
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addresses = NULL;
    if (getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &addresses) != 0 || addresses == NULL) {
        *error = "Cannot find the server " + url.host + ".";
        return -1;
    }

    int result = -1;
    for (addrinfo* it = addresses; it != NULL && result == -1; it = it->ai_next) {
        int s = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == -1) {
            continue;
        }

        int flags = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
        connect(s, it->ai_addr, it->ai_addrlen);

        int socketError = -1;
        if (http_wait(s, true, timeoutMs)) {
            socklen_t length = sizeof(socketError);
            getsockopt(s, SOL_SOCKET, SO_ERROR, &socketError, &length);
        }

        if (socketError == 0) {
            fcntl(s, F_SETFL, flags & ~O_NONBLOCK);

            timeval timeout;
            timeout.tv_sec = timeoutMs / 1000;
            timeout.tv_usec = (timeoutMs % 1000) * 1000;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
            int noSigPipe = 1;
            setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
            result = s;
        } else {
            close(s);
        }
    }

    freeaddrinfo(addresses);

    if (result == -1) {
        *error = "Cannot connect to the server " + url.host + ".";
    }

    return result;
}

static bool http_request(const std::string& method, const std::string& urlText, const std::string& contentType, const std::string& data, std::string* body, std::string* error, int timeoutMs)
{
    HttpUrl url;
    if (!http_parse_url(urlText, &url, error)) {
        return false;
    }

    if (url.secure && !http_load_openssl()) {
        *error = "HTTPS needs OpenSSL (libssl), which was not found.";
        return false;
    }

    int socket = http_connect(url, timeoutMs, error);
    if (socket == -1) {
        return false;
    }

    void* context = NULL;
    void* ssl = NULL;
    if (url.secure) {
        context = http_openssl.SSL_CTX_new(http_openssl.TLS_client_method());
        if (context != NULL) {
            http_openssl.SSL_CTX_set_default_verify_paths(context);
            http_openssl.SSL_CTX_set_verify(context, OPENSSL_VERIFY_PEER, NULL);
            ssl = http_openssl.SSL_new(context);
        }

        bool secured = ssl != NULL
            && http_openssl.SSL_set_fd(ssl, socket) == 1
            && http_openssl.SSL_ctrl(ssl, OPENSSL_CTRL_SET_TLSEXT_HOSTNAME, OPENSSL_TLSEXT_NAMETYPE_HOST_NAME, (void*)url.host.c_str()) == 1
            && http_openssl.SSL_set1_host(ssl, url.host.c_str()) == 1
            && http_openssl.SSL_connect(ssl) == 1
            && http_openssl.SSL_get_verify_result(ssl) == OPENSSL_X509_V_OK;

        if (!secured) {
            *error = "Could not open a secure connection to " + url.host + ".";
            if (ssl != NULL) {
                http_openssl.SSL_free(ssl);
            }
            if (context != NULL) {
                http_openssl.SSL_CTX_free(context);
            }
            close(socket);
            return false;
        }
    }

    std::string request = method + " " + url.path + " HTTP/1.0\r\n"
        + "Host: " + url.host + "\r\n"
        + "User-Agent: fallout-ce-coop\r\n"
        + "Connection: close\r\n";
    if (method == "POST") {
        char length[32];
        snprintf(length, sizeof(length), "%zu", data.size());
        request += "Content-Type: " + contentType + "\r\n";
        request += std::string("Content-Length: ") + length + "\r\n";
    }
    request += "\r\n";
    request += data;

    bool ok = true;
    size_t sent = 0;
    while (ok && sent < request.size()) {
        int n = ssl != NULL
            ? http_openssl.SSL_write(ssl, request.data() + sent, (int)(request.size() - sent))
            : (int)send(socket, request.data() + sent, request.size() - sent, HTTP_SEND_FLAGS);
        if (n <= 0) {
            *error = "The connection was lost.";
            ok = false;
        } else {
            sent += n;
        }
    }

    std::string response;
    char buffer[16 * 1024];
    while (ok) {
        int n = ssl != NULL
            ? http_openssl.SSL_read(ssl, buffer, sizeof(buffer))
            : (int)recv(socket, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }

        response.append(buffer, n);
        if (response.size() > HTTP_MAX_RESPONSE) {
            *error = "The answer was too large.";
            ok = false;
        }
    }

    if (ssl != NULL) {
        http_openssl.SSL_shutdown(ssl);
        http_openssl.SSL_free(ssl);
        http_openssl.SSL_CTX_free(context);
    }
    close(socket);

    if (!ok) {
        return false;
    }

    size_t headerEnd = response.find("\r\n\r\n");
    if (response.compare(0, 5, "HTTP/") != 0 || headerEnd == std::string::npos) {
        *error = response.empty() ? "The server did not answer." : "The server's answer was not understood.";
        return false;
    }

    int status = atoi(response.c_str() + response.find(' ') + 1);
    *body = response.substr(headerEnd + 4);

    if (status < 200 || status > 299) {
        *error = http_status_error(status);
        return false;
    }

    return true;
}

// One GET into `stream`; a redirect gives its target in `location` instead.
static bool http_download_once(const std::string& urlText, FILE* stream, CoopHttpProgress* progress, void* data, std::string* location, std::string* error, int timeoutMs)
{
    HttpUrl url;
    if (!http_parse_url(urlText, &url, error)) {
        return false;
    }

    if (url.secure && !http_load_openssl()) {
        *error = "HTTPS needs OpenSSL (libssl), which was not found.";
        return false;
    }

    int socket = http_connect(url, timeoutMs, error);
    if (socket == -1) {
        return false;
    }

    void* context = NULL;
    void* ssl = NULL;
    if (url.secure) {
        context = http_openssl.SSL_CTX_new(http_openssl.TLS_client_method());
        if (context != NULL) {
            http_openssl.SSL_CTX_set_default_verify_paths(context);
            http_openssl.SSL_CTX_set_verify(context, OPENSSL_VERIFY_PEER, NULL);
            ssl = http_openssl.SSL_new(context);
        }

        bool secured = ssl != NULL
            && http_openssl.SSL_set_fd(ssl, socket) == 1
            && http_openssl.SSL_ctrl(ssl, OPENSSL_CTRL_SET_TLSEXT_HOSTNAME, OPENSSL_TLSEXT_NAMETYPE_HOST_NAME, (void*)url.host.c_str()) == 1
            && http_openssl.SSL_set1_host(ssl, url.host.c_str()) == 1
            && http_openssl.SSL_connect(ssl) == 1
            && http_openssl.SSL_get_verify_result(ssl) == OPENSSL_X509_V_OK;

        if (!secured) {
            *error = "Could not open a secure connection to " + url.host + ".";
            if (ssl != NULL) {
                http_openssl.SSL_free(ssl);
            }
            if (context != NULL) {
                http_openssl.SSL_CTX_free(context);
            }
            close(socket);
            return false;
        }
    }

    std::string request = "GET " + url.path + " HTTP/1.0\r\n"
        + "Host: " + url.host + "\r\n"
        + "User-Agent: fallout-ce-coop\r\n"
        + "Connection: close\r\n\r\n";

    bool ok = true;
    size_t sent = 0;
    while (ok && sent < request.size()) {
        int n = ssl != NULL
            ? http_openssl.SSL_write(ssl, request.data() + sent, (int)(request.size() - sent))
            : (int)send(socket, request.data() + sent, request.size() - sent, HTTP_SEND_FLAGS);
        if (n <= 0) {
            *error = "The connection was lost.";
            ok = false;
        } else {
            sent += n;
        }
    }

    // The headers, then the body straight into the file.
    std::string head;
    bool inBody = false;
    int status = 0;
    unsigned long long total = 0;
    unsigned long long done = 0;
    std::vector<char> buffer(256 * 1024);
    while (ok) {
        int n = ssl != NULL
            ? http_openssl.SSL_read(ssl, buffer.data(), (int)buffer.size())
            : (int)recv(socket, buffer.data(), buffer.size(), 0);
        if (n <= 0) {
            if (!inBody) {
                *error = head.empty() ? "The server did not answer." : "The server's answer was not understood.";
                ok = false;
            }
            break;
        }

        const char* bodyStart = buffer.data();
        size_t bodyLength = (size_t)n;
        if (!inBody) {
            head.append(buffer.data(), n);
            size_t headerEnd = head.find("\r\n\r\n");
            if (headerEnd == std::string::npos) {
                if (head.size() > 64 * 1024) {
                    *error = "The server's answer was not understood.";
                    ok = false;
                }
                continue;
            }
            if (head.compare(0, 5, "HTTP/") != 0) {
                *error = "The server's answer was not understood.";
                ok = false;
                break;
            }
            status = atoi(head.c_str() + head.find(' ') + 1);
            std::string headers = head.substr(0, headerEnd + 2);
            // Content-Length and Location (any case).
            size_t at = 0;
            while (at < headers.size()) {
                size_t end = headers.find("\r\n", at);
                if (end == std::string::npos) {
                    break;
                }
                std::string line = headers.substr(at, end - at);
                at = end + 2;
                size_t colon = line.find(':');
                if (colon == std::string::npos) {
                    continue;
                }
                std::string name = line.substr(0, colon);
                for (char& ch : name) {
                    ch = (char)tolower((unsigned char)ch);
                }
                size_t valueStart = line.find_first_not_of(' ', colon + 1);
                std::string value = valueStart == std::string::npos ? "" : line.substr(valueStart);
                if (name == "content-length") {
                    total = strtoull(value.c_str(), NULL, 10);
                } else if (name == "location") {
                    *location = value;
                }
            }
            if (status >= 300 && status <= 399 && !location->empty()) {
                // A relative one is on the same server.
                if (location->compare(0, 4, "http") != 0) {
                    bool defaultPort = url.port == (url.secure ? "443" : "80");
                    *location = std::string(url.secure ? "https://" : "http://") + url.host + (defaultPort ? "" : ":" + url.port) + *location;
                }
                break;
            }
            location->clear();
            if (status < 200 || status > 299) {
                *error = http_status_error(status);
                ok = false;
                break;
            }
            inBody = true;
            size_t skipped = headerEnd + 4 - (head.size() - (size_t)n);
            bodyStart = buffer.data() + skipped;
            bodyLength = (size_t)n - skipped;
        }

        if (bodyLength != 0 && fwrite(bodyStart, 1, bodyLength, stream) != bodyLength) {
            *error = "Could not write the download (is the disk full?).";
            ok = false;
            break;
        }
        done += bodyLength;
        if (progress != NULL && !progress(data, done, total)) {
            *error = "Cancelled.";
            ok = false;
            break;
        }
    }

    if (ok && inBody && total != 0 && done != total) {
        *error = "The download was cut short.";
        ok = false;
    }

    if (ssl != NULL) {
        http_openssl.SSL_shutdown(ssl);
        http_openssl.SSL_free(ssl);
        http_openssl.SSL_CTX_free(context);
    }
    close(socket);
    return ok;
}

static bool http_download(const std::string& urlText, FILE* stream, CoopHttpProgress* progress, void* data, std::string* error, int timeoutMs)
{
    std::string url = urlText;
    for (int redirects = 0; redirects < 8; redirects++) {
        std::string location;
        if (!http_download_once(url, stream, progress, data, &location, error, timeoutMs)) {
            return false;
        }
        if (location.empty()) {
            return true;
        }
        url = location;
    }
    *error = "Too many redirects.";
    return false;
}

#endif

bool coop_http_download(const std::string& url, const std::string& path, CoopHttpProgress* progress, void* data, std::string* error, int timeoutMs)
{
    FILE* stream = fopen(path.c_str(), "wb");
    if (stream == NULL) {
        *error = "Could not write " + path + ".";
        return false;
    }
    bool ok = http_download(url, stream, progress, data, error, timeoutMs);
    if (fclose(stream) != 0 && ok) {
        *error = "Could not write the download (is the disk full?).";
        ok = false;
    }
    if (!ok) {
        remove(path.c_str());
    }
    return ok;
}

bool coop_http_get(const std::string& url, std::string* body, std::string* error, int timeoutMs)
{
    return http_request("GET", url, "", "", body, error, timeoutMs);
}

bool coop_http_post(const std::string& url, const std::string& contentType, const std::string& data, std::string* body, std::string* error, int timeoutMs)
{
    return http_request("POST", url, contentType, data, body, error, timeoutMs);
}

} // namespace fallout
