#pragma once

// It doesn't do much.

#include <array> // std::size
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

class Pool {
  struct alignas(std::max_align_t) BlockHeader {
    BlockHeader *next;
    std::uint8_t log2_size;

    explicit BlockHeader(std::size_t log2_size);
  };

  void delete_pool(std::size_t exponent);
  BlockHeader *new_block(std::size_t exponent);
  BlockHeader *get_block(std::size_t exponent);
  void put_block(BlockHeader *block);

  std::atomic<BlockHeader *> pools[32];

public:
  Pool();
  ~Pool();

  void *allocate(std::size_t size);
  void free(void *pointer);
};

inline Pool::BlockHeader::BlockHeader(std::size_t log2_size)
    : next(nullptr), log2_size(log2_size) {}

inline void Pool::delete_pool(std::size_t exponent) {
  BlockHeader *pool = pools[exponent].load();
  while (pool) {
    auto doomed = pool;
    pool = doomed->next;
    // No need to call ~BlockHeader(), it doesn't do anything.
    std::free(doomed);
  }
}

inline Pool::BlockHeader *Pool::new_block(std::size_t exponent) {
  void *pointer = std::aligned_alloc(alignof(BlockHeader),
                                     (1ULL << exponent) + sizeof(BlockHeader));
  return new (pointer) BlockHeader(exponent);
}

inline Pool::BlockHeader *Pool::get_block(std::size_t exponent) {
  BlockHeader *current = pools[exponent].load();
  do {
    if (current == nullptr) {
      BlockHeader *fresh = new_block(exponent);
      return fresh;
    }
  } while (!pools[exponent].compare_exchange_weak(current, current->next));
  return current;
}

inline void Pool::put_block(BlockHeader *block) {
  auto exponent = block->log2_size;
  BlockHeader *head = pools[exponent].load();
  do {
    block->next = head;
  } while (!pools[exponent].compare_exchange_weak(head, block));
}

inline Pool::Pool() : pools{} {}

inline Pool::~Pool() {
  for (std::size_t exponent = 0; exponent < std::size(pools); ++exponent) {
    delete_pool(exponent);
  }
}

inline void *Pool::allocate(std::size_t size) {
  if (size == 0) {
    return nullptr;
  }

  std::size_t exponent = 0;
  std::size_t remaining = size;
  while (remaining >>= 1) {
    ++exponent;
  }

  if (1ULL << exponent != size) {
    ++exponent;
  }

  BlockHeader *block = get_block(exponent);
  char *bytes = static_cast<char *>(static_cast<void *>(block));
  bytes += sizeof(BlockHeader);
  return bytes;
}

inline void Pool::free(void *pointer) {
  if (pointer == nullptr) {
    return;
  }
  char *bytes = static_cast<char *>(pointer);
  bytes -= sizeof(BlockHeader);
  auto *block = static_cast<BlockHeader *>(static_cast<void *>(bytes));
  put_block(block);
}
