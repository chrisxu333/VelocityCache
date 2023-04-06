#include "lru_cache.h"
#include <gtest/gtest.h>

std::string gen_random(const int len) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::string tmp_s;
    tmp_s.reserve(len);

    for (int i = 0; i < len; ++i) {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return tmp_s;
}

LRUCache cache(5, 0.6);

TEST(Test_Insertion, Modification) {
  for(size_t i = 0; i < 100; ++i) {
    std::string key = std::to_string(i);
    std::string value = gen_random(rand() % 10 + 1);
    cache.insert(key, value);
    cache.release(key);
    cache.show();
  }
}

TEST(Test_Lookup, Modification) {
  for(size_t i = 95; i < 100; ++i) {
    std::string key = std::to_string(i);
    std::string value;
    Status status = cache.lookup(key, value);
    if(status == Status::Ok) cache.release(key);
    cache.show();
  }
}