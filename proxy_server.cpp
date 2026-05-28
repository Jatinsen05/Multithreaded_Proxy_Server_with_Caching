/*
 * proxy_server.cpp
 *
 * Multithreaded HTTP Proxy Server with LRU Caching
 * C++ port of the original project by Lovepreet-Singh-LPSK
 *
 * Usage:  ./proxy <port>
 *
 * How it works:
 *   1. Main thread listens on <port> for client connections.
 *   2. Each accepted connection is handed off to a new std::thread.
 *   3. The thread parses the HTTP GET request.
 *   4. Checks the LRU cache:
 *        HIT  → send cached response back to client immediately.
 *        MISS → open TCP connection to remote server, forward request,
 *               read full response, store in cache, send to client.
 *   5. A std::counting_semaphore caps concurrent threads at MAX_CLIENTS.
 */

#include "proxy_parse.hpp"
#include "cache.hpp"

#include <iostream>
#include <string>
#include <thread>
#include <semaphore>

/* POSIX / Linux headers */
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

/* ─────────────────────────────────────────────────────────────
 *  Constants
 * ───────────────────────────────────────────────────────────── */
constexpr int    MAX_CLIENTS   = 10;      // max simultaneous connections
constexpr int    MAX_BYTES     = 4096;    // read buffer size
constexpr int    DEFAULT_PORT  = 8080;

/* ─────────────────────────────────────────────────────────────
 *  Globals
 * ───────────────────────────────────────────────────────────── */
static LRUCache cache;
static std::counting_semaphore<MAX_CLIENTS> sem(MAX_CLIENTS);

/* ─────────────────────────────────────────────────────────────
 *  sendErrorResponse()
 *
 *  Sends a minimal HTTP error page back to the client socket.
 * ───────────────────────────────────────────────────────────── */
static void sendErrorResponse(int clientSock, int statusCode) {
    std::string statusText;
    std::string body;

    switch (statusCode) {
        case 400:
            statusText = "Bad Request";
            body       = "<h1>400 Bad Request</h1><p>The proxy could not parse your request.</p>";
            break;
        case 403:
            statusText = "Forbidden";
            body       = "<h1>403 Forbidden</h1><p>Access denied.</p>";
            break;
        case 404:
            statusText = "Not Found";
            body       = "<h1>404 Not Found</h1><p>The requested resource was not found.</p>";
            break;
        case 504:
            statusText = "Gateway Timeout";
            body       = "<h1>504 Gateway Timeout</h1><p>The upstream server took too long to respond.</p>";
            break;
        case 500:
        default:
            statusCode = 500;
            statusText = "Internal Server Error";
            body       = "<h1>500 Internal Server Error</h1><p>The proxy encountered an error.</p>";
            break;
    }

    std::string response =
        "HTTP/1.0 " + std::to_string(statusCode) + " " + statusText + "\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    send(clientSock, response.c_str(), response.size(), 0);
}

/*Time out Helper function so that other request dont wait forever*/
static void setSocketTimeout(int sock, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
   }
   
/* ─────────────────────────────────────────────────────────────
 *  connectToRemoteServer()
 *
 *  Resolves host and opens a TCP connection to host:port.
 *  Returns connected socket fd on success, -1 on failure.
 * ───────────────────────────────────────────────────────────── */
static int connectToRemoteServer(const std::string& host, const std::string& port) {
    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (status != 0) {
        std::cerr << "[proxy] getaddrinfo error for " << host
                  << ": " << gai_strerror(status) << "\n";
        return -1;
    }

    int sock = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;
        setSocketTimeout(sock, 30);
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0)
            break;   // connected

        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0)
        std::cerr << "[proxy] could not connect to " << host << ":" << port << "\n";

    return sock;
}

/* ─────────────────────────────────────────────────────────────
 *  readFullRequest()
 *
 *  Reads from clientSock until we see the end-of-headers marker
 *  "\r\n\r\n".  Returns the full raw request string.
 * ───────────────────────────────────────────────────────────── */
static std::string readFullRequest(int clientSock) {
    std::string buf;
    char tmp[MAX_BYTES];

    while (true) {
        ssize_t n = recv(clientSock, tmp, sizeof(tmp) - 1, 0);
        if (n <= 0) break;
        tmp[n] = '\0';
        buf.append(tmp, n);

        /* HTTP headers end at the first blank line */
        if (buf.find("\r\n\r\n") != std::string::npos)
            break;

        /* Safety: bail out if request is unreasonably large */
        if (buf.size() > 64 * 1024) break;
    }
    return buf;
}
/* ─────────────────────────────────────────────────────────────
 *  handleClient()
 *
 *  Thread entry point.  Owns clientSock — closes it on exit.
 * ───────────────────────────────────────────────────────────── */
static void handleClient(int clientSock) {

    /* ── 1. Read raw request from client ── */
    std::string rawRequest = readFullRequest(clientSock);
    if (rawRequest.empty()) {
        sendErrorResponse(clientSock, 400);
        close(clientSock);
        sem.release();
        return;
    }

    /* ── 2. Parse the HTTP request ── */
    ParsedRequest req;
    if (!req.parse(rawRequest)) {
        std::cerr << "[proxy] parse failed\n";
        sendErrorResponse(clientSock, 400);
        close(clientSock);
        sem.release();
        return;
    }

    /* Only handle GET for now */
    if (req.method != "GET") {
        std::cerr << "[proxy] unsupported method: " << req.method << "\n";
        sendErrorResponse(clientSock, 403);
        close(clientSock);
        sem.release();
        return;
    }

    /* Build the cache key from host + path */
    std::string cacheKey = req.host + req.path;

    /* ── 3. Cache lookup ── */
    {
        std::string cached;
        if (cache.get(cacheKey, cached)) {
            std::cout << "[proxy] CACHE HIT  " << cacheKey << "\n";
            send(clientSock, cached.c_str(), cached.size(), 0);
            close(clientSock);
            sem.release();
            return;
        }
    }

    std::cout << "[proxy] CACHE MISS " << cacheKey << "\n";

    /* ── 4. Connect to remote server ── */
    int remoteSock = connectToRemoteServer(req.host, req.port);
    if (remoteSock < 0) {
        sendErrorResponse(clientSock, 504);
        close(clientSock);
        sem.release();
        return;
    }

    /* ── 5. Build and forward the request to the remote server ── */
    /*
     * Reconstruct as HTTP/1.0 to avoid chunked transfer encoding,
     * which simplifies reading the full response.
     */
    req.version = "HTTP/1.0";

    /* Ensure Connection: close so the remote closes after response */
    req.setHeader("Connection", "close");

    /* Ensure correct Host header */
    req.setHeader("Host", req.host);

    std::string forwardReq = req.unparse() + req.unparseHeaders();

    ssize_t sent = send(remoteSock, forwardReq.c_str(), forwardReq.size(), 0);
    if (sent < 0) {
        std::cerr << "[proxy] send to remote failed: " << strerror(errno) << "\n";
        sendErrorResponse(clientSock, 500);
        close(remoteSock);
        close(clientSock);
        sem.release();
        return;
    }

    /* ── 6. Read full response from remote server ── */
    std::string response;
    {
        char buf[MAX_BYTES];
        ssize_t n;
        while ((n = recv(remoteSock, buf, sizeof(buf), 0)) > 0) {
            response.append(buf, n);

            /* Safety cap: don't buffer more than MAX_ELEMENT_SIZE */
            if (response.size() > MAX_ELEMENT_SIZE) break;
        }
    }
    close(remoteSock);

    if (response.empty()) {
        sendErrorResponse(clientSock, 500);
        close(clientSock);
        sem.release();
        return;
    }

    /* ── 7. Store in cache ── */
    cache.put(cacheKey, response);

    /* ── 8. Forward response to client ── */
    size_t offset = 0;
    while (offset < response.size()) {
        ssize_t n = send(clientSock, response.c_str() + offset,
                         response.size() - offset, 0);
        if (n <= 0) break;
        offset += n;
    }

    close(clientSock);
    sem.release();
}

/* ─────────────────────────────────────────────────────────────
 *  main()
 * ───────────────────────────────────────────────────────────── */
int main(int argc, char* argv[]) {
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        try { port = std::stoi(argv[1]); }
        catch (...) {
            std::cerr << "Usage: " << argv[0] << " <port>\n";
            return 1;
        }
    }

    /* ── Create server socket ── */
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "socket() failed: " << strerror(errno) << "\n";
        return 1;
    }

    /* Allow port reuse to avoid "Address already in use" on restart */
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* ── Bind ── */
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    if (bind(serverSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << strerror(errno) << "\n";
        close(serverSock);
        return 1;
    }

    /* ── Listen ── */
    if (listen(serverSock, MAX_CLIENTS) < 0) {
        std::cerr << "listen() failed: " << strerror(errno) << "\n";
        close(serverSock);
        return 1;
    }

    std::cout << "[proxy] Listening on port " << port << "\n";
    std::cout << "[proxy] Max concurrent clients: " << MAX_CLIENTS << "\n";

    /* ── Accept loop ── */
    while (true) {
        struct sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        int clientSock = accept(serverSock,
                                reinterpret_cast<struct sockaddr*>(&clientAddr),
                                &clientLen);
        setSocketTimeout(clientSock, 30);
        if (clientSock < 0) {
            std::cerr << "[proxy] accept() error: " << strerror(errno) << "\n";
            continue;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
        std::cout << "[proxy] New connection from " << clientIP << "\n";

        sem.acquire();
        /* Detach thread — it manages its own lifetime */
        std::thread(handleClient, clientSock).detach();
    }

    close(serverSock);
    return 0;
}
