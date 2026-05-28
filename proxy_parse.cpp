/*
 * proxy_parse.cpp
 *
 * C++ implementation of HTTP request parsing for a multithreaded proxy server.
 * This is a modern C++ port of the original proxy_parse.c from the
 * MultiThreadedProxyServerClient project by Lovepreet-Singh-LPSK.
 *
 * Key improvements over the C version:
 *   - RAII: std::string replaces all malloc/free for char* fields
 *   - std::unordered_map replaces the linked list of headers → O(1) lookup
 *   - Member methods replace global functions operating on raw pointers
 *   - No manual memory management needed by callers
 */

#include "proxy_parse.hpp"

#include <sstream>
#include <stdexcept>
#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <cctype>

/* ─────────────────────────────────────────────────────────────
 *  debug()  –  variadic logger (mirrors the original C version)
 * ───────────────────────────────────────────────────────────── */
void debug(const char* format, ...) {
#ifdef NDEBUG
    (void)format;
#else
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
#endif
}

/* ─────────────────────────────────────────────────────────────
 *  ParsedHeader
 * ───────────────────────────────────────────────────────────── */

ParsedHeader::ParsedHeader() = default;

ParsedHeader::ParsedHeader(const std::string& k, const std::string& v)
    : key(k), value(v) {}

/* Returns   "Key: Value\r\n"  */
std::string ParsedHeader::toString() const {
    return key + ": " + value + "\r\n";
}

/* ─────────────────────────────────────────────────────────────
 *  ParsedRequest  –  constructor / destructor
 * ───────────────────────────────────────────────────────────── */

ParsedRequest::ParsedRequest()
    : port("80") {}          // default HTTP port

ParsedRequest::~ParsedRequest() = default;   // std::string / map clean up themselves

/* ─────────────────────────────────────────────────────────────
 *  parse()
 *
 *  Accepts a raw HTTP request string such as:
 *
 *      GET http://www.example.com:8080/index.html HTTP/1.0\r\n
 *      Host: www.example.com\r\n
 *      Connection: close\r\n
 *      \r\n
 *
 *  Fills:  method, host, port, path, version, headers map.
 *  Returns true on success, false on any parse error.
 * ───────────────────────────────────────────────────────────── */
bool ParsedRequest::parse(const std::string& raw) {
    /* ── 1. Split into lines on \r\n ── */
    std::vector<std::string> lines;
    {
        size_t pos = 0;
        while (pos < raw.size()) {
            size_t end = raw.find("\r\n", pos);
            if (end == std::string::npos) {
                /* Tolerate missing final CRLF */
                lines.push_back(raw.substr(pos));
                break;
            }
            lines.push_back(raw.substr(pos, end - pos));
            pos = end + 2;
        }
    }

    if (lines.empty()) {
        debug("parse: empty request");
        return false;
    }

    /* ── 2. Parse the request line  "METHOD URI VERSION" ── */
    {
        std::istringstream rl(lines[0]);
        std::string uri;
        if (!(rl >> method >> uri >> version)) {
            debug("parse: malformed request line: %s", lines[0].c_str());
            return false;
        }

        /* ── 3. Decompose URI  →  host, port, path ── */
        /*
         * Proxy absolute-form URI:   http://host[:port]/path
         * Origin-form  (CONNECT/transparent): /path   (host in Host header)
         */
        if (uri.substr(0, 7) == "http://") {
            std::string rest = uri.substr(7);          // "host[:port]/path"

            size_t slash = rest.find('/');
            std::string authority;
            if (slash == std::string::npos) {
                authority = rest;
                path      = "/";
            } else {
                authority = rest.substr(0, slash);
                path      = rest.substr(slash);        // includes leading '/'
            }

            /* Split authority into host and optional port */
            size_t colon = authority.find(':');
            if (colon == std::string::npos) {
                host = authority;
                port = "80";
            } else {
                host = authority.substr(0, colon);
                port = authority.substr(colon + 1);
            }
        } else {
            /* Relative URI — host must be in the Host header */
            path = uri;
            host = "";
            port = "80";
        }
    }

    /* ── 4. Parse headers  "Key: Value" ── */
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty()) break;          // blank line → end of headers

        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            debug("parse: skipping malformed header line: %s", line.c_str());
            continue;
        }

        std::string key   = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        /* Trim leading whitespace from value */
        size_t start = value.find_first_not_of(" \t");
        if (start != std::string::npos)
            value = value.substr(start);

        /* Trim trailing whitespace */
        size_t end = value.find_last_not_of(" \t\r\n");
        if (end != std::string::npos)
            value = value.substr(0, end + 1);

        /* If Host header is present and host was not in the URI, use it */
        if (key == "Host" && host.empty()) {
            size_t colon2 = value.find(':');
            if (colon2 == std::string::npos) {
                host = value;
            } else {
                host = value.substr(0, colon2);
                port = value.substr(colon2 + 1);
            }
        }

        headers[key] = ParsedHeader(key, value);
    }

    if (host.empty()) {
        debug("parse: could not determine host");
        return false;
    }

    debug("parse: method=%s host=%s port=%s path=%s version=%s",
          method.c_str(), host.c_str(), port.c_str(),
          path.c_str(), version.c_str());

    return true;
}

/* ─────────────────────────────────────────────────────────────
 *  unparse()
 *
 *  Reconstructs the HTTP request line WITHOUT headers.
 *  e.g.  "GET /index.html HTTP/1.0\r\n"
 *
 *  (The original sends the request line separately from headers
 *   when forwarding to the remote server, so we mirror that.)
 * ───────────────────────────────────────────────────────────── */
std::string ParsedRequest::unparse() const {
    /* Use path only (not the full absolute URI) when forwarding */
    return method + " " + path + " " + version + "\r\n";
}

/* ─────────────────────────────────────────────────────────────
 *  unparseHeaders()
 *
 *  Serialises all headers as   "Key: Value\r\n"  blocks,
 *  ending with the mandatory blank line  "\r\n".
 * ───────────────────────────────────────────────────────────── */
std::string ParsedRequest::unparseHeaders() const {
    std::string result;
    result.reserve(headersLen() + 2);
    for (const auto& [key, hdr] : headers) {
        result += hdr.toString();
    }
    result += "\r\n";          // blank line terminator
    return result;
}

/* ─────────────────────────────────────────────────────────────
 *  totalLen()
 *
 *  Length of unparse() + unparseHeaders() combined —
 *  useful for allocating send buffers.
 * ───────────────────────────────────────────────────────────── */
size_t ParsedRequest::totalLen() const {
    return unparse().size() + headersLen() + 2;   // +2 for final \r\n
}

/* ─────────────────────────────────────────────────────────────
 *  headersLen()
 *
 *  Total byte length of all serialised headers (no blank line).
 * ───────────────────────────────────────────────────────────── */
size_t ParsedRequest::headersLen() const {
    size_t len = 0;
    for (const auto& [key, hdr] : headers) {
        len += hdr.toString().size();
    }
    return len;
}

/* ─────────────────────────────────────────────────────────────
 *  setHeader()  /  getHeader()  /  removeHeader()
 * ───────────────────────────────────────────────────────────── */

void ParsedRequest::setHeader(const std::string& key, const std::string& value) {
    headers[key] = ParsedHeader(key, value);
}

/*
 * Returns a pointer to the header, or nullptr if not found.
 * Two overloads: const and non-const so both can be used.
 */
ParsedHeader* ParsedRequest::getHeader(const std::string& key) {
    auto it = headers.find(key);
    if (it == headers.end()) return nullptr;
    return &it->second;
}

const ParsedHeader* ParsedRequest::getHeader(const std::string& key) const {
    auto it = headers.find(key);
    if (it == headers.end()) return nullptr;
    return &it->second;
}

bool ParsedRequest::removeHeader(const std::string& key) {
    return headers.erase(key) > 0;
}
