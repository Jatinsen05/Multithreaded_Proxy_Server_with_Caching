# Multithreaded Proxy Server with LRU Caching — Complete Documentation

---

## Project Paragraph (use this in interviews / viva)

This project implements a **multithreaded HTTP forward proxy server** in modern C++20, featuring an in-memory LRU (Least Recently Used) cache to eliminate redundant network round-trips. When a client sends an HTTP GET request through the proxy, the server first checks whether the response is already cached — a cache hit returns the stored response immediately without any network I/O, while a cache miss triggers a real TCP connection to the remote server, fetches the full response, stores it in the LRU cache, and forwards it to the client. Concurrency is managed using `std::thread` with a `std::counting_semaphore` to cap simultaneous active connections at a configurable limit. The LRU cache is implemented as a thread-safe combination of `std::list` (for O(1) recency-order maintenance via `splice`) and `std::unordered_map` (for O(1) key lookup by URL), replacing the original project's C-style linked list — which had O(n) lookup — with a substantially more efficient structure. The entire codebase is a ground-up C++ port of the original C project, designed around RAII memory management, type safety, and zero manual `malloc`/`free`.

---

## Thread Pool: 10 to 35 — How to Change It and Why

### The Change

In proxy_server.cpp, two lines must be updated together:

```cpp
// BEFORE
constexpr int MAX_CLIENTS = 10;
static std::counting_semaphore<MAX_CLIENTS> sem(MAX_CLIENTS);

// AFTER
constexpr int MAX_CLIENTS = 35;
static std::counting_semaphore<MAX_CLIENTS> sem(MAX_CLIENTS);
```

The template parameter is a compile-time maximum. Both must match. You cannot change this at runtime; you must recompile. Also update the listen() backlog:

```cpp
listen(serverSock, MAX_CLIENTS);
```

### Why I/O-Bound Work Benefits from More Threads

A proxy thread spends most of its time waiting — waiting on recv() from the client, connect() to the remote, recv() from the remote. The CPU is mostly idle. At 10 threads, if all 10 are blocked on slow network I/O, the 11th client waits even though the CPU is completely free. At 35 threads, more connections are served in parallel since each thread's CPU usage is tiny.

### Tradeoffs Table

| Factor | 10 Threads | 35 Threads |
|---|---|---|
| Max concurrent clients | 10 | 35 |
| Thread stack memory | ~800 KB | ~2.8 MB |
| Worst-case response buffers | 100 MB | 350 MB |
| Context switch cost | Low | Moderate |
| CPU utilisation (I/O) | Under-utilised | Better |
| Risk of OOM on 2 GB RAM | Very low | Low-moderate |

### Costs You Must Know for Interview

1. **Stack memory**: Linux default = 8 MB per thread virtual reservation. 35 threads = 280 MB virtual space.
2. **Response buffers**: Each thread can buffer up to MAX_ELEMENT_SIZE (10 MB). Worst case: 35 x 10 MB = 350 MB simultaneously.
3. **Context switching**: More threads = more scheduler overhead. For I/O-bound work this is small but non-zero.
4. **Thread creation cost**: std::thread spawns a new OS thread via clone(). At high connection rates this syscall overhead matters — a thread pool (fixed N threads pulling from a queue) is the correct fix.
5. **Semaphore is compile-time**: counting_semaphore<N> bakes N into the binary. Changing MAX_CLIENTS requires a recompile.

---

## DoS Protection — Current State and Fix

### What Is Currently in the Code

```cpp
if (buf.size() > 64 * 1024) break;  // in readFullRequest()
```

Caps request size at 64 KB. Prevents memory exhaustion from giant requests.

### What Is Missing — Slowloris Attack

A slow client that sends 3 bytes and then stops will hold a thread open forever via blocking recv(). With 35 thread slots, 35 such connections fully block the proxy. This is Slowloris.

### The Fix

```cpp
// Add after accept(), before spawning the thread:
struct timeval tv{};
tv.tv_sec  = 30;
tv.tv_usec = 0;
setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
```

After 30 seconds of silence, recv() returns -1 (EAGAIN), readFullRequest() returns empty, thread sends 400 and exits, slot freed.

---

## Interview Questions and Answers

### Conceptual

**Q: Time complexity of cache operations?**
get() is O(1) average: unordered_map::find is O(1), list::splice is O(1). put() is O(1) amortized. The original C project had O(n) lookup via linked list traversal.

**Q: Why std::list and not std::vector for LRU order?**
std::list supports O(1) splice() — moving a node to the front by relinking 3 pointers, no data copy. std::vector requires O(n) shifting to reorder elements. For a cache that frequently promotes recently-accessed entries, O(1) reorder is essential.

**Q: Why both a list AND an unordered_map?**
They serve different purposes. The list maintains access order (front = MRU, back = LRU) for O(1) eviction via pop_back(). The map provides O(1) lookup by URL key. Neither alone gives both capabilities.

**Q: What happens when two threads request the same uncached URL simultaneously?**
Both miss the cache, both fetch from the remote, both call put(). The second put() overwrites the first. No corruption, but one redundant network request. This is the cache stampede problem. Fix: a per-URL in-flight flag or promise/future.

**Q: Why force HTTP/1.0 when forwarding?**
HTTP/1.1 uses chunked transfer encoding by default, requiring complex chunk-boundary parsing. HTTP/1.0 sends the full body then closes the connection — trivial to read with a recv() loop. Tradeoff: no keep-alive with the remote server, so a new TCP connection is made for every cache miss.

**Q: What is RAII and where is it used here?**
RAII ties a resource's lifetime to a C++ object's scope — acquired in constructor, released in destructor. Used: std::string manages all char buffers (no malloc/free), std::lock_guard acquires and releases the cache mutex automatically, std::unordered_map manages the header table.

**Q: Difference between std::thread::detach() and join()?**
join() blocks the calling thread until the target finishes. detach() separates the thread — it runs independently. We use detach() because main() must return to accept() immediately. Tradeoff: detached threads cannot be waited on; abrupt process exit kills them.

**Q: Why counting_semaphore instead of a mutex?**
A mutex allows exactly 1 thread to proceed. A counting semaphore allows up to N simultaneously. counting_semaphore<35> allows 35 concurrent acquire() calls; the 36th blocks. A mutex would allow only 1 concurrent connection — useless for a proxy.

**Q: Why is sem.acquire() inside the thread a bug?**
The thread is created first, then sem.acquire() is called. With 1000 simultaneous connections, 1000 threads are spawned before any blocking occurs — consuming gigabytes of stack space. Fix: call sem.acquire() in main() before std::thread(...), so the main thread blocks and no thread is created until a slot is free.

**Q: What is SSRF?**
Server-Side Request Forgery — an attacker sends a crafted URL to reach internal services: localhost:22 (SSH), 169.254.169.254 (AWS metadata), 192.168.1.1 (router). ssrf_fix.cpp defends by resolving the hostname, checking the IP against RFC 1918/loopback/link-local blocklists, and only allowing ports 80 and 443.

---

### Scenarios

**100 clients connect simultaneously:**
Main thread spawns 100 threads. The first 35 acquire the semaphore and proceed. The other 65 block on sem.acquire(). As each of the 35 finishes and calls sem.release(), a waiting thread unblocks. No connections are dropped.

**Same URL requested 10 times in a row:**
Request 1 is a cache miss: DNS lookup + TCP connect + recv = 50-500ms. Requests 2-10 are cache hits: unordered_map::find + list::splice + send = microseconds. Speedup can be 100-10000x.

**Client requests a 15 MB file:**
MAX_ELEMENT_SIZE is 10 MB. The recv loop breaks early at 10 MB, leaving a truncated response. cache.put() silently ignores it. But the truncated response is forwarded to the client — a corrupt response. This is a bug. Fix: detect overflow, send 500, do not forward.

**Remote server takes 5 minutes to respond:**
Without SO_RCVTIMEO on remoteSock, the proxy thread blocks for 5 minutes holding a semaphore slot. If all 35 threads hit this, the proxy is fully blocked. Fix: apply SO_RCVTIMEO to remoteSock after connecting.

**X-Forwarded-For: 127.0.0.1 from client:**
Forwarded verbatim. Remote server sees localhost as the origin — may bypass IP-based access controls. Fix: strip or rewrite X-Forwarded-For before forwarding.

---

### Edge Cases

**URL with no path — http://example.com:**
parse() sets path = "/" when no slash follows the authority. Forwarded as GET / HTTP/1.0. Correct.

**Same host+path, different ports — :80 vs :8080:**
Original cacheKey = host + path maps both to the same entry. Bug. Fix: cacheKey = host + ":" + port + path gives distinct keys.

**unordered_map hash collision:**
Handled automatically by chaining. Worst case O(n) lookup with adversarial URL set. In practice negligible with standard library hash functions.

**Proxy restart with active connections:**
Detached threads are killed by the OS when main() exits. Connections in progress are abruptly terminated. Graceful shutdown requires a signal handler, a stop flag, and waiting for active threads.

**recv() returns -1 mid-response:**
errno == EINTR: should retry. errno == EAGAIN: timeout, abort with 504. Other: abort with 500. Current code does not distinguish — it sends a corrupt partial response. A production proxy would need full errno handling.

**Two threads call cache.put() for the same URL simultaneously:**
Both acquire the mutex sequentially. The second finds the key exists, erases and reinserts. Final state: second thread's version stored. No corruption, one redundant round-trip. Cache stampede.

---

## Build Reference

```bash
cd /mnt/c/Users/dell/Desktop/Web\ Server
make
./proxy 8080

# Test
curl -v --proxy http://localhost:8080 http://example.com

# Time cache hit vs miss
time curl --proxy http://localhost:8080 http://example.com > /dev/null
time curl --proxy http://localhost:8080 http://example.com > /dev/null

# Release build
make CXXFLAGS="-std=c++20 -O2 -DNDEBUG -pthread"
```
