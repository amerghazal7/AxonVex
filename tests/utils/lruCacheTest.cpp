#include <gtest/gtest.h>
#include <axonvex_core/utils/caching/lruCache.hpp>

using axonvex::utils::caching::LRUCache;

TEST(LRUCacheTest, PutGetEvict) {
    LRUCache<int,int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);
    ASSERT_TRUE(cache.get(1).has_value());
    EXPECT_EQ(cache.get(1).value(), 10);
    // Insert third -> evict LRU (key 2 becomes LRU after get(1) moved 1 to front)
    cache.put(3, 30);
    auto v2 = cache.get(2);
    auto v1 = cache.get(1);
    auto v3 = cache.get(3);
    EXPECT_FALSE(v2.has_value());
    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v3.has_value());
    EXPECT_EQ(v1.value(), 10);
    EXPECT_EQ(v3.value(), 30);
}
