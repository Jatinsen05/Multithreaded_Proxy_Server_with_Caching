#pragma once

/*
 * cache.hpp
 *
 * Thread-safe LRU Cache for the multithreaded proxy server.
 * Stores HTTP responses keyed by URL string.
 *
 * Design:
 *   - std::list  keeps insertion/access order (front = most recent)
 *   - std::unordered_map  gives O(1) lookup by URL key
 *   - std::mutex  makes every operation thread-safe
 *
 * This replaces the C linked-list approach from the original project
 * with a proper O(1) LRU implementation.
 */

#include <string>
#include <list>
#include <unordered_map>
#include <mutex>

/* Maximum total size of cached data in bytes (200 MB) */
constexpr size_t MAX_CACHE_SIZE = 200 * 1024 * 1024;

/* Maximum size of a single cached element (10 MB) */
constexpr size_t MAX_ELEMENT_SIZE = 10 * 1024 * 1024;

/* ─────────────────────────────────────────────────────────────
 *  CacheEntry  –  one cached HTTP response
 * ───────────────────────────────────────────────────────────── */
struct CacheEntry {
    std::string url;      // key  (full request URL)
    std::string data;     // raw HTTP response bytes
    size_t      size;     // data.size() cached for speed

    CacheEntry(const std::string& u, const std::string& d)
        : url(u), data(d), size(d.size()) {}
};

/* ─────────────────────────────────────────────────────────────
 *  LRUCache
 * ───────────────────────────────────────────────────────────── */
class LRUCache {
public:
    explicit LRUCache(size_t maxSize = MAX_CACHE_SIZE);
    ~LRUCache() = default;

    /* Returns true and fills 'out' if URL is cached. */
    bool get(const std::string& url, std::string& out);

    /* Stores response. Silently ignores if response > MAX_ELEMENT_SIZE.
     * Evicts LRU entries until there is room. */
    void put(const std::string& url, const std::string& data);

    /* Remove a specific entry (e.g. on error). */
    bool remove(const std::string& url);

    /* Current number of cached entries. */
    size_t count() const;

    /* Current total bytes used. */
    size_t usedBytes() const;

private:
    /* Evict least-recently-used entries until 'needed' bytes are free.
     * Caller must hold mutex_. */
    void evictUntilFits(size_t needed);

    using ListIt = std::list<CacheEntry>::iterator;

    size_t                                    maxSize_;
    size_t                                    usedBytes_;
    std::list<CacheEntry>                     lruList_;   // front = MRU
    std::unordered_map<std::string, ListIt>   index_;     // url → iterator
    mutable std::mutex                        mutex_;
};
