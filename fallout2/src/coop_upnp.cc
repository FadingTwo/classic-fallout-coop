#include "coop_upnp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <SDL.h>

namespace fallout {

#ifdef _WIN32
typedef SOCKET UpnpSocket;
#define UPNP_INVALID_SOCKET INVALID_SOCKET
static void upnp_close(UpnpSocket socket) { closesocket(socket); }
#else
typedef int UpnpSocket;
#define UPNP_INVALID_SOCKET (-1)
static void upnp_close(UpnpSocket socket) { close(socket); }
#endif

// How long the router is asked to keep the port open (seconds); the host
// asks again before that (coop_upnp_refresh).
#define UPNP_LEASE 3600

#define UPNP_DESCRIPTION "Classic Fallout Co-op"

// What coop_upnp_map opened, for refreshing and closing it.
typedef struct UpnpMapping {
    bool mapped = false;
    std::string controlUrl;
    std::string serviceType;
    std::string internalClient;
    int internalPort = 0;
    int externalPort = 0;
    int lease = UPNP_LEASE;
} UpnpMapping;

static std::mutex upnp_mutex;
static UpnpMapping upnp_mapping;

static bool upnp_startup()
{
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

static bool upnp_wait(UpnpSocket socket, bool write, int ms)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    timeval wait = { ms / 1000, (ms % 1000) * 1000 };
    return select((int)socket + 1, write ? NULL : &set, write ? &set : NULL, NULL, &wait) > 0;
}

static std::string upnp_lower(std::string text)
{
    for (char& ch : text) {
        ch = (char)tolower((unsigned char)ch);
    }
    return text;
}

// http://host[:port]/path
static bool upnp_parse_url(const std::string& url, std::string* host, int* port, std::string* path)
{
    if (upnp_lower(url.substr(0, 7)) != "http://") {
        return false;
    }
    size_t start = 7;
    size_t slash = url.find('/', start);
    std::string authority = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    *path = slash == std::string::npos ? "/" : url.substr(slash);
    size_t colon = authority.rfind(':');
    *port = 80;
    if (colon != std::string::npos) {
        *port = atoi(authority.c_str() + colon + 1);
        authority = authority.substr(0, colon);
    }
    *host = authority;
    return !host->empty() && *port > 0 && *port < 65536;
}

static bool upnp_address(const std::string& host, int port, sockaddr_in* address)
{
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host.c_str(), &(address->sin_addr)) == 1) {
        return true;
    }
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    addrinfo* result = NULL;
    if (getaddrinfo(host.c_str(), NULL, &hints, &result) != 0 || result == NULL) {
        return false;
    }
    address->sin_addr = ((sockaddr_in*)result->ai_addr)->sin_addr;
    freeaddrinfo(result);
    return true;
}

// A plain HTTP/1.1 request to the router (Connection: close). Returns the
// status (0: no answer); `body` gets the body.
static int upnp_http(const std::string& url, const std::string& method, const std::string& headers, const std::string& data, std::string* body, int timeoutMs)
{
    std::string host;
    int port;
    std::string path;
    if (!upnp_parse_url(url, &host, &port, &path)) {
        return 0;
    }
    sockaddr_in address;
    if (!upnp_address(host, port, &address)) {
        return 0;
    }

    UpnpSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == UPNP_INVALID_SOCKET) {
        return 0;
    }
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(socket, FIONBIO, &nonBlocking);
#else
    fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
#endif
    connect(socket, (sockaddr*)&address, sizeof(address));
    if (!upnp_wait(socket, true, timeoutMs)) {
        upnp_close(socket);
        return 0;
    }
    int socketError = 0;
    socklen_t length = sizeof(socketError);
    getsockopt(socket, SOL_SOCKET, SO_ERROR, (char*)&socketError, &length);
    if (socketError != 0) {
        upnp_close(socket);
        return 0;
    }

    std::string request = method + " " + path + " HTTP/1.1\r\n"
        + "Host: " + host + ":" + std::to_string(port) + "\r\n"
        + "Connection: close\r\n"
        + headers;
    if (method == "POST") {
        request += "Content-Length: " + std::to_string(data.size()) + "\r\n";
    }
    request += "\r\n" + data;

    size_t sent = 0;
    while (sent < request.size()) {
        if (!upnp_wait(socket, true, timeoutMs)) {
            upnp_close(socket);
            return 0;
        }
        int count = (int)send(socket, request.data() + sent, (int)(request.size() - sent), 0);
        if (count <= 0) {
            upnp_close(socket);
            return 0;
        }
        sent += count;
    }

    std::string response;
    char buffer[4096];
    while (response.size() < 256 * 1024 && upnp_wait(socket, false, timeoutMs)) {
        int count = (int)recv(socket, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            break;
        }
        response.append(buffer, count);
    }
    upnp_close(socket);

    size_t headerEnd = response.find("\r\n\r\n");
    if (response.compare(0, 5, "HTTP/") != 0 || headerEnd == std::string::npos) {
        return 0;
    }
    size_t space = response.find(' ');
    int status = space != std::string::npos ? atoi(response.c_str() + space + 1) : 0;
    std::string head = upnp_lower(response.substr(0, headerEnd));
    *body = response.substr(headerEnd + 4);

    // Chunked bodies (some routers): put together.
    if (head.find("transfer-encoding: chunked") != std::string::npos) {
        std::string chunked = *body;
        body->clear();
        size_t at = 0;
        while (at < chunked.size()) {
            size_t lineEnd = chunked.find("\r\n", at);
            if (lineEnd == std::string::npos) {
                break;
            }
            unsigned long size = strtoul(chunked.c_str() + at, NULL, 16);
            if (size == 0) {
                break;
            }
            body->append(chunked, lineEnd + 2, size);
            at = lineEnd + 2 + size + 2;
        }
    }
    return status;
}

// The text of the first <tag> at or after `from` (and where it ended).
static std::string upnp_tag(const std::string& xml, const std::string& tag, size_t from, size_t* end = NULL)
{
    // (Case doesn't matter: routers differ.)
    std::string lower = upnp_lower(xml);
    std::string open = "<" + upnp_lower(tag) + ">";
    size_t start = lower.find(open, from);
    if (start == std::string::npos) {
        return std::string();
    }
    start += open.size();
    size_t close = lower.find("</" + upnp_lower(tag) + ">", start);
    if (close == std::string::npos) {
        return std::string();
    }
    if (end != NULL) {
        *end = close;
    }
    std::string text = xml.substr(start, close - start);
    // Trim.
    size_t first = text.find_first_not_of(" \t\r\n");
    size_t last = text.find_last_not_of(" \t\r\n");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

// The routers on the local network that answer an SSDP search: their
// description URLs.
static std::vector<std::string> upnp_discover(std::string* log)
{
    std::vector<std::string> locations;

    std::string target = "239.255.255.250";
    int targetPort = 1900;
    if (const char* ssdp = getenv("COOP_UPNP_SSDP")) {
        std::string text = ssdp;
        size_t colon = text.rfind(':');
        if (colon != std::string::npos) {
            target = text.substr(0, colon);
            targetPort = atoi(text.c_str() + colon + 1);
        }
    }

    UpnpSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == UPNP_INVALID_SOCKET) {
        *log += "no UDP socket; ";
        return locations;
    }
    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)targetPort);
    inet_pton(AF_INET, target.c_str(), &(address.sin_addr));

    const char* searches[] = {
        "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
        "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
        "urn:schemas-upnp-org:service:WANIPConnection:1",
        "urn:schemas-upnp-org:service:WANPPPConnection:1",
    };
    // Twice: a router can miss a datagram.
    for (int round = 0; round < 2; round++) {
        for (const char* search : searches) {
            std::string message = std::string("M-SEARCH * HTTP/1.1\r\n")
                + "HOST: 239.255.255.250:1900\r\n"
                + "MAN: \"ssdp:discover\"\r\n"
                + "MX: 2\r\n"
                + "ST: " + search + "\r\n\r\n";
            sendto(socket, message.data(), (int)message.size(), 0, (sockaddr*)&address, sizeof(address));
        }
    }

    unsigned int start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 2500) {
        if (!upnp_wait(socket, false, 100)) {
            // Answers came: a moment more for others, then on.
            if (!locations.empty() && SDL_GetTicks() - start >= 1000) {
                break;
            }
            continue;
        }
        char buffer[2048];
        int count = (int)recv(socket, buffer, sizeof(buffer) - 1, 0);
        if (count <= 0) {
            continue;
        }
        buffer[count] = '\0';
        std::string answer = buffer;
        std::string lower = upnp_lower(answer);
        size_t at = lower.find("\nlocation:");
        if (at == std::string::npos) {
            continue;
        }
        at += 10;
        size_t end = answer.find_first_of("\r\n", at);
        std::string location = answer.substr(at, end == std::string::npos ? std::string::npos : end - at);
        size_t first = location.find_first_not_of(" \t");
        location = first == std::string::npos ? std::string() : location.substr(first);
        bool known = false;
        for (const std::string& other : locations) {
            known = known || other == location;
        }
        if (!location.empty() && !known) {
            locations.push_back(location);
        }
    }
    upnp_close(socket);
    return locations;
}

// This machine's address on the way to `host` (what the router forwards to).
static std::string upnp_local_address(const std::string& host, int port)
{
    sockaddr_in address;
    if (!upnp_address(host, port, &address)) {
        return std::string();
    }
    UpnpSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == UPNP_INVALID_SOCKET) {
        return std::string();
    }
    std::string result;
    if (connect(socket, (sockaddr*)&address, sizeof(address)) == 0) {
        sockaddr_in local;
        socklen_t length = sizeof(local);
        char text[INET_ADDRSTRLEN];
        if (getsockname(socket, (sockaddr*)&local, &length) == 0 && inet_ntop(AF_INET, &(local.sin_addr), text, sizeof(text)) != NULL) {
            result = text;
        }
    }
    upnp_close(socket);
    return result;
}

static int upnp_soap(const UpnpMapping& mapping, const std::string& action, const std::string& arguments, std::string* body)
{
    std::string envelope = std::string("<?xml version=\"1.0\"?>\r\n")
        + "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        + "<s:Body><u:" + action + " xmlns:u=\"" + mapping.serviceType + "\">" + arguments + "</u:" + action + "></s:Body></s:Envelope>\r\n";
    std::string headers = "Content-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPAction: \"" + mapping.serviceType + "#" + action + "\"\r\n";
    return upnp_http(mapping.controlUrl, "POST", headers, envelope, body, 2000);
}

static int upnp_add(const UpnpMapping& mapping, std::string* errorCode)
{
    std::string arguments = "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>" + std::to_string(mapping.externalPort) + "</NewExternalPort>"
        "<NewProtocol>TCP</NewProtocol>"
        "<NewInternalPort>" + std::to_string(mapping.internalPort) + "</NewInternalPort>"
        "<NewInternalClient>" + mapping.internalClient + "</NewInternalClient>"
        "<NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>" UPNP_DESCRIPTION "</NewPortMappingDescription>"
        "<NewLeaseDuration>" + std::to_string(mapping.lease) + "</NewLeaseDuration>";
    std::string body;
    int status = upnp_soap(mapping, "AddPortMapping", arguments, &body);
    *errorCode = upnp_tag(body, "errorCode", 0);
    return status;
}

// The router's WAN connection service in a description: its control URL
// (absolute) and type.
static bool upnp_find_service(const std::string& location, const std::string& xml, std::string* controlUrl, std::string* serviceType)
{
    std::string host;
    int port;
    std::string path;
    if (!upnp_parse_url(location, &host, &port, &path)) {
        return false;
    }
    std::string base = upnp_tag(xml, "URLBase", 0);
    if (base.empty()) {
        base = "http://" + host + ":" + std::to_string(port);
    }
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }

    size_t at = 0;
    for (;;) {
        size_t end;
        std::string type = upnp_tag(xml, "serviceType", at, &end);
        if (type.empty()) {
            return false;
        }
        at = end;
        if (type.find(":service:WANIPConnection:") == std::string::npos && type.find(":service:WANPPPConnection:") == std::string::npos) {
            continue;
        }
        std::string control = upnp_tag(xml, "controlURL", at);
        if (control.empty()) {
            continue;
        }
        if (upnp_lower(control.substr(0, 7)) == "http://") {
            *controlUrl = control;
        } else {
            *controlUrl = base + (control[0] == '/' ? "" : "/") + control;
        }
        *serviceType = type;
        return true;
    }
}

int coop_upnp_map(int port, std::string* log)
{
    std::lock_guard<std::mutex> lock(upnp_mutex);
    log->clear();
    if (upnp_mapping.mapped) {
        return upnp_mapping.externalPort;
    }
    if (!upnp_startup()) {
        *log = "no networking";
        return 0;
    }

    std::vector<std::string> locations = upnp_discover(log);
    if (locations.empty()) {
        *log += "no router answered";
        return 0;
    }

    for (const std::string& location : locations) {
        std::string xml;
        int status = upnp_http(location, "GET", "", "", &xml, 2000);
        UpnpMapping mapping;
        if (status != 200 || !upnp_find_service(location, xml, &mapping.controlUrl, &mapping.serviceType)) {
            *log += location + ": no port forwarding service; ";
            continue;
        }

        std::string host;
        int hostPort;
        std::string path;
        upnp_parse_url(mapping.controlUrl, &host, &hostPort, &path);
        mapping.internalClient = upnp_local_address(host, hostPort);
        if (mapping.internalClient.empty()) {
            *log += location + ": no local address; ";
            continue;
        }
        mapping.internalPort = port;

        // Our port, or the next free ones (another computer behind the same
        // router may have it).
        for (int attempt = 0; attempt < 8; attempt++) {
            mapping.externalPort = port + attempt;
            std::string errorCode;
            status = upnp_add(mapping, &errorCode);
            if (status != 200 && errorCode == "725") {
                // Only "for ever": then that, closed again when we stop.
                mapping.lease = 0;
                status = upnp_add(mapping, &errorCode);
            }
            if (status == 200) {
                mapping.mapped = true;
                upnp_mapping = mapping;
                *log += "port " + std::to_string(mapping.externalPort) + " opened on " + host + " for " + mapping.internalClient;
                return mapping.externalPort;
            }
            if (errorCode != "718") {
                *log += location + ": refused (" + std::to_string(status) + (errorCode.empty() ? "" : ", error " + errorCode) + "); ";
                break;
            }
            if (attempt == 7) {
                *log += location + ": ports " + std::to_string(port) + "-" + std::to_string(port + 7) + " taken; ";
            }
        }
    }
    return 0;
}

bool coop_upnp_refresh(std::string* log)
{
    std::lock_guard<std::mutex> lock(upnp_mutex);
    if (!upnp_mapping.mapped || upnp_mapping.lease == 0) {
        return upnp_mapping.mapped;
    }
    std::string errorCode;
    int status = upnp_add(upnp_mapping, &errorCode);
    *log = status == 200 ? "port kept open" : "the router refused to keep the port open (" + std::to_string(status) + ")";
    return status == 200;
}

void coop_upnp_unmap()
{
    std::lock_guard<std::mutex> lock(upnp_mutex);
    if (!upnp_mapping.mapped) {
        return;
    }
    std::string arguments = "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>" + std::to_string(upnp_mapping.externalPort) + "</NewExternalPort>"
        "<NewProtocol>TCP</NewProtocol>";
    std::string body;
    upnp_soap(upnp_mapping, "DeletePortMapping", arguments, &body);
    upnp_mapping = UpnpMapping();
}

} // namespace fallout
