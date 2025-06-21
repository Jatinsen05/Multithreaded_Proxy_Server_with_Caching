/*
 * proxy_parse.hpp -- a HTTP Request Parsing Library.
 *
 * Written by: Matvey Arye
 * For: COS 518
 * Converted to C++ by Gemini
 *
 */

// Line 1: #pragma once
// This is a modern, non-standard but widely supported and highly efficient
// include guard. It ensures that this header file is included only once
// during a single compilation, preventing redefinition errors.
#pragma once

// Line 2: #include <iostream>
// Provides standard input/output stream objects like std::cout, std::cerr,
// and std::endl for console output, replacing C's <stdio.h> functions like printf.
#include <iostream>

// Line 3: #include <string>
// Provides the std::string class, which is the standard C++ way to handle
// dynamic strings. It manages memory automatically, unlike C-style char arrays.
// Replaces C's <string.h>.
#include <string>

// Line 4: #include <vector>
// Provides the std::vector template class, a dynamic array that manages its
// own memory. Useful for collections where element order might matter or iteration.
// It could be used for headers if order is strict, but unordered_map is often better for lookup.
#include <vector>

// Line 5: #include <unordered_map>
// Provides the std::unordered_map template class, a hash table implementation.
// This is an excellent choice for storing HTTP headers (key-value pairs)
// as it allows for very efficient (average O(1) time complexity) lookup, insertion,
// and deletion of elements by key. This replaces the C-style linked list for headers.
#include <unordered_map>

// Line 6: #include <stdexcept>
// Provides standard exception classes like std::runtime_error, std::logic_error, etc.
// In modern C++, exceptions are often preferred over error codes for handling
// exceptional conditions that prevent a function from completing successfully.
#include <stdexcept>

// Line 7: #include <cstddef>
// Provides definitions for standard types like size_t, which is used for sizes
// and counts. This is the C++ equivalent of C's <stddef.h>.
#include <cstddef>

// Line 8: #include <cerrno>
// Provides access to the C-style errno global variable and strerror function.
// This is the C++ equivalent of C's <errno.h> and is useful when interacting
// with system calls (e.g., socket operations) that set errno on failure.
#include <cerrno>

// Line 9: #include <cctype>
// Provides functions for character classification and conversion (e.g., isdigit,
// isspace, tolower). This is the C++ equivalent of C's <ctype.h> and is useful
// for parsing text-based protocols like HTTP.
#include <cctype>

// Line 10: // #include <cstdarg>
// This line is commented out. It would provide C-style variable argument handling
// (like va_list, va_start). While still usable in C++, modern C++ often prefers
// templates or function overloads for type-safe variadic functions.
// Only uncomment if your debug function's implementation specifically requires it.

// Line 11: #ifndef NDEBUG
// This preprocessor directive checks if the macro NDEBUG is not defined.
// NDEBUG (No DEBUG) is a standard macro often defined by build systems
// in "release" configurations to disable debugging features (like assert).
#ifndef NDEBUG

// Line 12: #define DEBUG_ENABLED 1
// If NDEBUG is not defined (meaning it's likely a "debug" build), then
// DEBUG_ENABLED is defined as 1, indicating that debugging code should be active.
#define DEBUG_ENABLED 1

// Line 13: #else
// If NDEBUG is defined (meaning it's likely a "release" build),
// the code block below #else will be processed.
#else

// Line 14: #define DEBUG_ENABLED 0
// DEBUG_ENABLED is defined as 0, indicating that debugging code should be inactive
// and optimized out by the compiler.
#define DEBUG_ENABLED 0

// Line 15: #endif
// Ends the #ifndef NDEBUG preprocessor block.
#endif

// Line 16: // Forward declaration of ParsedHeader class.
// This tells the compiler that ParsedHeader is a class, allowing ParsedRequest
// to declare pointers or references to it before ParsedHeader's full definition.
// However, for std::unordered_map<std::string, ParsedHeader>, the full
// definition of ParsedHeader is required BEFORE ParsedRequest is defined.
// So, the order of class definitions below is crucial.
// class ParsedHeader; // Not strictly needed here if ParsedHeader is defined first.

// Line 17: /*
// Line 18:  * ParsedHeader class
// Line 19:  *
// Line 20:  * This class represents a single HTTP header (key-value pair).
// Line 21:  * Converted from a C struct to a C++ class.
// Line 22:  * char* members are replaced with std::string for automatic memory management.
// Line 23:  */
// Line 24: class ParsedHeader {
// Class definition for an individual HTTP header.
class ParsedHeader {
public:
    // Line 25: std::string key;
    // Stores the header's key (e.g., "Content-Type"). std::string handles memory.
    std::string key;

    // Line 26: std::string value;
    // Stores the header's value (e.g., "text/html"). std::string handles memory.
    std::string value;

    // Line 27: // Constructors for convenience
    // Line 28: ParsedHeader() = default; // Default constructor: initializes key and value to empty strings.
    ParsedHeader() = default;

    // Line 29: ParsedHeader(const std::string& k, const std::string& v) : key(k), value(v) {}
    // Parameterized constructor: allows initializing key and value directly upon creation.
    ParsedHeader(const std::string& k, const std::string& v) : key(k), value(v) {}

    // Line 30: // No need for keylen/valuelen, std::string handles their lengths automatically.
};

// Line 31: /*
// Line 32:  * ParsedRequest class
// Line 33:  *
// Line 34:  * This class represents a parsed HTTP request.
// Line 35:  * It's converted from a C struct to a C++ class to encapsulate data and behavior.
// Line 36:  * All char* pointers are replaced with std::string for automatic memory management.
// Line 37:  * The headers linked list is replaced with a std::unordered_map for efficient management.
// Line 38:  */
// Line 39: class ParsedRequest {
// Class definition for the entire parsed HTTP request.
class ParsedRequest {
public:
    // Line 40: // Member variables for the request line components.
    // Line 41: // Using std::string eliminates the need for manual memory management (malloc/free)
    // Line 42: // and provides rich string manipulation capabilities.
    std::string method;   // Line 43: HTTP method (e.g., "GET", "POST").
    std::string protocol; // Line 44: Protocol (e.g., "http").
    std::string host;     // Line 45: Hostname (e.g., "www.google.com").
    std::string port;     // Line 46: Port number (e.g., "80").
    std::string path;     // Line 47: Request path (e.g., "/index.html").
    std::string version;  // Line 48: HTTP version (e.g., "HTTP/1.1").
    std::string buf;      // Line 49: Internal buffer to store the original request line/full request if needed.
                          //          No 'buflen' needed as std::string::length() provides this.

    // Line 50: // Using std::unordered_map<std::string, ParsedHeader> to store headers.
    // Line 51: // This replaces the C-style linked list (struct ParsedHeader *)
    // Line 52: // and automatically manages memory for the headers, offering fast lookups.
    std::unordered_map<std::string, ParsedHeader> headers;

    // Line 53: // No direct equivalent for headersused/headerslen needed;
    // Line 54: // std::unordered_map::size() provides the count of headers.

    // Line 55: // Constructor: Replaces ParsedRequest_create().
    // Line 56: // Initializes member strings to empty.
    ParsedRequest();

    // Line 57: // Destructor: Replaces ParsedRequest_destroy().
    // Line 58: // std::string and std::unordered_map automatically handle memory cleanup (RAII).
    ~ParsedRequest();

    /* Line 59:
     * Line 60: parse() method: Parses the request buffer.
     * Line 61: Replaces ParsedRequest_parse().
     * Line 62: Takes a const std::string& for the buffer, which is efficient and safe.
     * Line 63: Returns 0 on success, -1 on failure. Consider throwing exceptions in modern C++.
     */
    int parse(const std::string& buffer);

    /* Line 64:
     * Line 65: unparse() method: Reconstructs the entire request into a string.
     * Line 66: Replaces ParsedRequest_unparse().
     * Line 67: Returns the unparsed request as a std::string.
     */
    std::string unparse() const;

    /* Line 68:
     * Line 69: unparseHeaders() method: Reconstructs only the headers into a string.
     * Line 70: Replaces ParsedRequest_unparse_headers().
     * Line 71: Returns the unparsed headers as a std::string.
     */
    std::string unparseHeaders() const;

    /* Line 72:
     * Line 73: totalLen() method: Calculates the total length of the request.
     * Line 74: Replaces ParsedRequest_totalLen().
     * Line 75: Returns size_t.
     */
    size_t totalLen() const;

    /* Line 76:
     * Line 77: headersLen() method: Calculates the total length of the headers.
     * Line 78: Replaces ParsedHeader_headersLen().
     * Line 79: Returns size_t.
     */
    size_t headersLen() const;

    /* Line 80:
     * Line 81: setHeader() method: Sets or adds a header key-value pair.
     * Line 82: Replaces ParsedHeader_set().
     * Line 83: Returns 0 on success, -1 on failure.
     */
    int setHeader(const std::string& key, const std::string& value);

    /* Line 84:
     * Line 85: getHeader() method: Retrieves a pointer to a ParsedHeader object by key.
     * Line 86: Replaces ParsedHeader_get().
     * Line 87: Returns a pointer to the ParsedHeader if found, nullptr otherwise.
     * Line 88: Note: Returning a non-const pointer means the header can be modified.
     */
    ParsedHeader* getHeader(const std::string& key);

    // Line 89: const ParsedHeader* getHeader(const std::string& key) const; // Overload for const objects
    // Provides a const-correct version for when the ParsedRequest object is const.
    const ParsedHeader* getHeader(const std::string& key) const;

    /* Line 90:
     * Line 91: removeHeader() method: Removes a header by key.
     * Line 92: Replaces ParsedHeader_remove().
     * Line 93: Returns 0 on success, -1 on failure.
     */
    int removeHeader(const std::string& key);

private:
    // Line 94: // Private helper for parsing the initial request line buffer.
    // Line 95: // You might add private helper methods here as you implement the parsing logic.
    // Line 96: // Example: int parseRequestLine(const std::string& request_line);
};

// Line 97: // Global debug function.
// Line 98: // Using a function in a namespace or a static member function of a utility class
// Line 99: // is more C++ idiomatic than a global C-style function.
// Line 100: // For simplicity, keeping it as a free function but using std::cerr for output.
// Line 101: // The implementation for this function will be in the .cpp file.
void debug(const char * format, ...);