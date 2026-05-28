/*
 * cache.cpp
 *
 * Thread-safe LRU Cache implementation.
 * See cache.hpp for design notes.
 */

#include "cache.hpp"

/* ─────────────────────────────────────────────────────────────
 *  Constructor
 * ───────────────────────────────────────────────────────────── */
LRUCache::LRUCache(size_t maxSize)
    : maxSize_(maxSize), usedBytes_(0) {}

/* ─────────────────────────────────────────────────────────────
 *  get()
 *
 *  Cache hit  → moves entry to front (MRU), fills 'out', returns true.
 *  Cache miss → returns false.
 * ───────────────────────────────────────────────────────────── */
bool LRUCache::get(const std::string& url, std::string& out) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = index_.find(url);
    if (it == index_.end())
        return false;   // cache miss

    /* Move to front (most recently used) */
    lruList_.splice(lruList_.begin(), lruList_, it->second);

    out = it->second->data;
    return true;
}

/* ─────────────────────────────────────────────────────────────
 *  put()
 *
 *  If URL already cached → update in place.
 *  If response > MAX_ELEMENT_SIZE → skip silently.
 *  Otherwise evict LRU entries until there is room, then insert.
 * ───────────────────────────────────────────────────────────── */
void LRUCache::put(const std::string& url, const std::string& data) {
    if (data.size() > MAX_ELEMENT_SIZE)
        return;   // too large to cache

    std::lock_guard<std::mutex> lock(mutex_);

    /* If already exists, remove old entry first */
    auto existing = index_.find(url);
    if (existing != index_.end()) {
        usedBytes_ -= existing->second->size;
        lruList_.erase(existing->second);
        index_.erase(existing);
    }

    /* Evict LRU entries until there is room */
    evictUntilFits(data.size());

    /* Insert at front (MRU position) */
    lruList_.emplace_front(url, data);
    index_[url] = lruList_.begin();
    usedBytes_ += data.size();
}

/* ─────────────────────────────────────────────────────────────
 *  remove()
 * ───────────────────────────────────────────────────────────── */
bool LRUCache::remove(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = index_.find(url);
    if (it == index_.end())
        return false;

    usedBytes_ -= it->second->size;
    lruList_.erase(it->second);
    index_.erase(it);
    return true;
}

/* ─────────────────────────────────────────────────────────────
 *  count() / usedBytes()
 * ───────────────────────────────────────────────────────────── */
size_t LRUCache::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lruList_.size();
}

size_t LRUCache::usedBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return usedBytes_;
}

/* ─────────────────────────────────────────────────────────────
 *  evictUntilFits()   [private, caller holds mutex_]
 *
 *  Removes entries from the back (LRU end) of lruList_
 *  until (usedBytes_ + needed) <= maxSize_.
 * ───────────────────────────────────────────────────────────── */
void LRUCache::evictUntilFits(size_t needed) {
    while (!lruList_.empty() && (usedBytes_ + needed) > maxSize_) {
        CacheEntry& lru = lruList_.back();
        usedBytes_ -= lru.size;
        index_.erase(lru.url);
        lruList_.pop_back();
    }
}
