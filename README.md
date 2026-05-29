# Multi-threaded HTTP Proxy Server with LRU Caching

## 📌 Project Overview
This project is a high-performance **Multi-threaded HTTP Proxy Server** written in C++. It acts as an intermediary between a client (like a web browser or `curl`) and a remote web server. The proxy enhances performance by implementing an **LRU (Least Recently Used) Cache**, which stores recently accessed web pages to serve them faster on subsequent requests.

The server is designed for Linux-based systems (and WSL) and utilizes modern C++20 features for thread management and synchronization.

---

## 🏗️ Architecture & How It Works

### 1. The Request Flow
1.  **Listen:** The main thread starts a socket listener on a user-specified port (default 8080).
2.  **Accept:** When a client connects, the main thread "acquires" a slot from a semaphore.
3.  **Spawn:** A new worker thread is spawned to handle that specific client.
4.  **Parse:** The proxy reads the raw HTTP request and parses it into a `ParsedRequest` object.
5.  **Cache Lookup:** The proxy checks the internal LRU Cache using the URL as a key.
    *   **Cache HIT:** The stored response is sent back to the client immediately.
    *   **Cache MISS:** The proxy connects to the remote server, forwards the request, reads the response, stores it in the cache, and then sends it to the client.
6.  **Cleanup:** The connection is closed, and the semaphore slot is released for the next client.

### 2. Key Components
*   **`proxy_server.cpp`**: The heart of the project. Manages the socket lifecycle, multi-threading, and the coordination between the parser and the cache.
*   **`proxy_parse.cpp/hpp`**: A custom HTTP parser that decomposes raw strings into method, host, path, and headers.
*   **`cache.cpp/hpp`**: A thread-safe LRU cache using a `std::list` (for ordering) and a `std::unordered_map` (for O(1) lookup).

---

## 🛠️ Build & Installation (WSL/Ubuntu)

### Prerequisites
*   **GCC/G++ 10+**: Required for C++20 features.
*   **WSL/Linux Environment**.

### Step-by-Step Compilation
1. Open your WSL terminal.
2. Navigate to the project directory:
   ```bash
   cd "/mnt/c/Users/your_user/Desktop/Web Server"
   ```
3. Run the compiler:
   ```bash
   g++ -std=c++20 -pthread proxy_server.cpp proxy_parse.cpp cache.cpp -o proxy
   ```

---

## 🚀 Usage Guide

### Running the Server
Start the proxy by specifying a port:
```bash
./proxy 8080
```

### Scenario 1: Basic Proxying (Cache Miss)
When you first request a website:
1. Run: `curl.exe -v -x http://localhost:8080 http://example.com`
2. **Proxy Output:** `[proxy] CACHE MISS example.com/`
3. The proxy fetches the page from the internet and saves it.

### Scenario 2: Caching in Action (Cache Hit)
Request the same website again:
1. Run: `curl.exe -v -x http://localhost:8080 http://example.com`
2. **Proxy Output:** `[proxy] CACHE HIT example.com/`
3. The response is instantaneous because it's served from RAM.

### Scenario 3: Handling Multiple Clients
Because the server is multi-threaded, you can open multiple terminals and run `curl` commands simultaneously. The proxy handles them in parallel, up to the `MAX_CLIENTS` limit.

### Scenario 4: Handling Timeouts
If you try to connect to a website that is down or extremely slow:
1. The proxy will wait for 30 seconds (our configured timeout).
2. If the site doesn't respond, the proxy sends a **504 Gateway Timeout** back to the client.

---

## 🛡️ Implemented Security Features (DDoS Protection)
*   **Thread Throttling:** Uses a semaphore in the main loop to prevent "Thread Exhaustion" attacks. It limits concurrent connections to a safe number (default 10).
*   **Socket Timeouts:** Implements `SO_RCVTIMEO` and `SO_SNDTIMEO` on all sockets to prevent "Slowloris" attacks where clients hold connections open indefinitely.
*   **Gateway Feedback:** Provides explicit HTTP error codes (400, 403, 504) instead of crashing or hanging.

---

## 🚧 Roadmap & Future Work
*   **SSRF Protection:** Adding an allow-list for IP ranges.
*   **HTTPS Support:** Implementing the `CONNECT` method for SSL tunneling.
*   **Header Sanitization:** Stripping malicious CRLF characters from headers.
