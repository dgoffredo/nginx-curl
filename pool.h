#pragma once

// It doesn't do much.

#include <array> // std::size
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>

class Pool {
  struct alignas(std::max_align_t) BlockHeader {
    BlockHeader *next;
    std::uint8_t log2_size : 5;

    explicit BlockHeader(std::size_t log2_size);
  };

  void delete_pool(std::size_t exponent);
  BlockHeader *new_block(std::size_t exponent, bool zeroed);
  BlockHeader *get_block(std::size_t exponent, bool zeroed);
  void put_block(BlockHeader *block);
  BlockHeader *block_cast(void *pointer);
  void *allocate_impl(std::size_t size, bool zeroed);

  std::atomic<BlockHeader *> pools[32];

public:
  Pool();
  ~Pool();

  void *allocate(std::size_t size);
  void *callocate(std::size_t count, std::size_t size_each);
  void *reallocate(void *pointer, std::size_t new_size);
  void free(void *pointer);
};
