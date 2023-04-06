#include <bits/stdc++.h>

#include "status.h"
#include "xx_hash.h"

#define SHARD_NUM_ 1
#define CACHE_LINE_SIZE 8

struct alignas(CACHE_LINE_SIZE) lru_node_t {
  std::string key_;
  std::string value_;
  lru_node_t *prev_;
  lru_node_t *next_;
  uint64_t hash_;
  bool hit_;
  int ref_cnt_;
  enum flag_t : uint8_t { HIGH, LOW, HASH_TBL };
  uint8_t flag_;
  bool in_cache_;
};

class LRUHashTable {
 private:
  struct Node {
    std::string key_;
    lru_node_t *node_ptr_;
    Node *next_;
    Node(const std::string &key, lru_node_t *node_ptr)
        : key_(key), node_ptr_(node_ptr), next_(nullptr) {}
  };

 public:
  LRUHashTable(int num_of_buckets) {
    // allocate space for buckets
    buckets_ =
        reinterpret_cast<Node **>(malloc(sizeof(Node *) * num_of_buckets));
    for (int i = 0; i < num_of_buckets; ++i) {
      buckets_[i] = reinterpret_cast<Node *>(malloc(sizeof(Node)));
    }
    bucket_size_ = num_of_buckets;
  }
  void insert(const std::string &key, lru_node_t *node_ptr,
              const uint64_t hash) {
    // find bucket
    uint64_t bucket_num = hash % bucket_size_;
    // add node at front
    Node *cur = new Node(key, node_ptr);
    cur->next_ = buckets_[bucket_num]->next_;
    buckets_[bucket_num]->next_ = cur;
  }
  void erase(const std::string &key, const uint64_t hash) {
    Node *target = localLookUp(key, hash);
    if (target == nullptr) {
      return;
    }
    Node *target_nxt = target->next_;
    target->next_ = target->next_->next_;
    delete target_nxt;
  }
  lru_node_t *lookup(const std::string &key, const uint64_t hash) const {
    Node *target = localLookUp(key, hash);
    if (target == nullptr) return nullptr;
    return target->next_->node_ptr_;
  }

 private:
  Node **buckets_;
  int bucket_size_;
  // lookup the node before the key
  Node *localLookUp(const std::string &key, const uint64_t hash) const {
    // find bucket
    uint64_t bucket_num = hash % bucket_size_;
    Node *prev = buckets_[bucket_num];
    Node *dummy = prev->next_;
    while (dummy != nullptr) {
      if (dummy->key_ == key) return prev;
      prev = dummy;
      dummy = dummy->next_;
    }
    return nullptr;
  }
};

class LRUCacheInstance {
 public:
  explicit LRUCacheInstance(const int capacity, const float ratio) {
    hash_tbl_ = new LRUHashTable(capacity);
    lru_ = new lru_node_t();
    lru_low_pri_ = lru_;
    lru_low_pri_->prev_ = lru_;
    lru_low_pri_->next_ = lru_;
    lru_->prev_ = lru_;
    lru_->next_ = lru_;
    cache_capacity_ = capacity;
    high_ratio_ = ratio;
    cache_size_ = 0;
    lru_size_ = 0;
    lru_size_high_ = 0;
  }
  ~LRUCacheInstance() {
    delete hash_tbl_;
    lru_node_t *dummy = lru_->next_;
    while (dummy != lru_) {
      lru_node_t *cur = dummy;
      delete cur;
      dummy = dummy->next_;
    }
    delete lru_;
  }

  Status insertNew(const std::string &key, const std::string &value,
                   const uint64_t hash) {
    // build lru_node_t
    lru_node_t *tmp = new lru_node_t();
    tmp->key_ = key;
    tmp->value_ = value;
    tmp->hash_ = hash;
    tmp->hit_ = false;
    tmp->ref_cnt_ = 2;  // being referenced by both user and the cache component
    tmp->flag_ = tmp->flag_t::HASH_TBL;
    tmp->in_cache_ = true;
    cache_size_++;

    // insert into hash table;
    hash_tbl_->insert(key, tmp, hash);

    return Status::Ok;
  }

  /**
   * User should call release every time they are done using the element
   * fetched.
   */
  Status release(const std::string &key, const uint64_t hash) {
    lru_node_t *tmp = hash_tbl_->lookup(key, hash);
    tmp->ref_cnt_--;
    if (tmp->ref_cnt_ == 0) {  // clean up the erase work.
      hash_tbl_->erase(key, hash);
      free(tmp);
      return Status::Ok;
    } else if (tmp->ref_cnt_ == 1) {  // re-enter lru
      insertBack(tmp);
      return Status::Ok;
    }
    return Status::Fail;
  }

  Status lookup(const std::string &key, std::string &value,
                const uint64_t hash) {
    lru_node_t *target = hash_tbl_->lookup(key, hash);
    if (target == nullptr) return Status::NotFound;
    target->hit_ = true;
    target->ref_cnt_++;
    // Take target out of lru_list
    if (target->ref_cnt_ > 1) {
      removeNode(target);
    }
    value = target->value_;
    return Status::Ok;
  }

  /**
   * Erase key-value pair from cache. After this call only the external caller
   * is responsible for the item.
   */
  Status erase(const std::string &key, const uint64_t hash) {
    lru_node_t *target = hash_tbl_->lookup(key, hash);
    if (target->in_cache_ && target->ref_cnt_ == 2) {
      target->in_cache_ = false;
      target->ref_cnt_--;
      return Status::Ok;
    } else
      return Status::Fail;
  }

  void show() {
    std::cout << "LRU list" << std::endl;
    lru_node_t *dummy = lru_->next_;
    while (dummy != lru_) {
      if (dummy == lru_low_pri_) {
        std::cout << "[" << dummy->key_ << "]"
                  << " ";
      } else {
        std::cout << dummy->key_ << " ";
      }
      dummy = dummy->next_;
    }
    std::cout << std::endl;
  }

 private:
  inline int isFull() const { return cache_size_ > cache_capacity_; }
  inline int isLruFull() const { return lru_size_ > cache_capacity_; }
  inline int isHighStrictFull() const {
    return lru_size_high_ > (cache_capacity_ * high_ratio_);
  }
  inline int isHighFull() const {
    return lru_size_high_ >= (cache_capacity_ * high_ratio_);
  }
  inline int isLowFull() const {
    return (lru_size_ - lru_size_high_) >=
           (cache_capacity_ * (1 - high_ratio_));
  }
  void insertHigh(lru_node_t *tmp) {
    if (lru_size_ == 0) {
      tmp->next_ = lru_;
      tmp->prev_ = lru_;
      lru_->prev_ = tmp;
      lru_->next_ = tmp;
    } else {
      tmp->prev_ = lru_;
      tmp->next_ = lru_->next_;
      lru_->next_->prev_ = tmp;
      lru_->next_ = tmp;
    }
    tmp->flag_ = tmp->flag_t::HIGH;
  }
  void insertLow(lru_node_t *tmp) {
    if (lru_size_ == 0) {
      tmp->next_ = lru_;
      tmp->prev_ = lru_;
      lru_->prev_ = tmp;
      lru_->next_ = tmp;
    } else {
      tmp->prev_ = lru_low_pri_->prev_;
      tmp->next_ = lru_low_pri_;
      lru_low_pri_->prev_->next_ = tmp;
      lru_low_pri_->prev_ = tmp;
    }
    lru_low_pri_ = tmp;
    tmp->flag_ = tmp->flag_t::LOW;
  }
  void insertBack(lru_node_t *tmp) {
    // depends on the priority of the key, insert back to either high or low
    // pool
    if (tmp->hit_ || isLowFull()) {
      insertHigh(tmp);
      lru_size_high_++;
    } else
      insertLow(tmp);
    lru_size_++;

    if (isLruFull()) {
      lru_node_t *oldest = lru_->prev_;
      hash_tbl_->erase(oldest->key_, oldest->hash_);
      // advance low pri head
      lru_low_pri_ = lru_low_pri_->prev_;
      // update lru_
      lru_->prev_ = oldest->prev_;
      oldest->prev_->next_ = lru_;
      // free oldest node.
      delete oldest;
      cache_size_--;
      lru_size_--;
      lru_size_high_--;
    } else {
      if (isHighStrictFull()) {
        if (lru_low_pri_ == nullptr)
          lru_low_pri_ = lru_->prev_;
        else
          lru_low_pri_ = lru_low_pri_->prev_;
        lru_size_high_--;
      }
    }
  }
  void removeNode(lru_node_t *tmp) {
    tmp->prev_->next_ = tmp->next_;
    tmp->next_->prev_ = tmp->prev_;
    tmp->next_ = nullptr;
    tmp->prev_ = nullptr;
    tmp->hit_ = true;  // this node will appear in high pool next time.
    // reset low pool head, it'll auto adjust next time when high pool is full.
    if (tmp == lru_low_pri_) lru_low_pri_ = nullptr;
    lru_size_--;
  }

 private:
  LRUHashTable *hash_tbl_;
  lru_node_t *lru_;
  lru_node_t *lru_low_pri_;
  int cache_capacity_;
  int cache_size_;
  int lru_size_;
  int lru_size_high_;
  float high_ratio_;
};

/**
 * Each shard contains its own lru cache instance.
 */
class LRUCache {
 public:
  explicit LRUCache(int size, const float ratio) {
    for (size_t i = 0; i < SHARD_NUM_; ++i) {
      shard_[i] = new LRUCacheInstance(size, ratio);
    }
  }
  ~LRUCache() {
    for (size_t i = 0; i < SHARD_NUM_; ++i) {
      delete shard_[i];
    }
  }
  Status insert(const std::string &key, const std::string &value) {
    // generate hash key
    XXHash64 hasher(10);
    hasher.add(key.c_str(), key.length());
    uint64_t hash_val = hasher.hash();
    return shard_[hash_val % SHARD_NUM_]->insertNew(key, value, hash_val);
  }
  Status lookup(const std::string &key, std::string &value) {
    // generate hash key
    XXHash64 hasher(10);
    hasher.add(key.c_str(), key.length());
    uint64_t hash_val = hasher.hash();
    return shard_[hash_val % SHARD_NUM_]->lookup(key, value, hash_val);
  }
  Status release(const std::string &key) {
    // generate hash key
    XXHash64 hasher(10);
    hasher.add(key.c_str(), key.length());
    uint64_t hash_val = hasher.hash();
    return shard_[hash_val % SHARD_NUM_]->release(key, hash_val);
  }
  Status erase(const std::string &key) {
    XXHash64 hasher(10);
    hasher.add(key.c_str(), key.length());
    uint64_t hash_val = hasher.hash();
    return shard_[hash_val % SHARD_NUM_]->erase(key, hash_val);
  }
  void show() {
    // for each shard
    for (int i = 0; i < SHARD_NUM_; ++i) {
      std::cout << "shard " << i << std::endl;
      shard_[i]->show();
      std::cout << "====================" << std::endl;
    }
  }

 private:
  LRUCacheInstance *shard_[SHARD_NUM_];
};