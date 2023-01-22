#include "pool.h"

#include <array> // std::size
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
#include <ostream>
#include <stdexcept>

Pool::BlockHeader::BlockHeader(std::size_t log2_size)
    : next(nullptr), log2_size(log2_size) {}

Pool::BlockHeader *Pool::block_cast(void *pointer) {
  char *bytes = static_cast<char *>(pointer);
  bytes -= sizeof(BlockHeader);
  return static_cast<BlockHeader *>(static_cast<void *>(bytes));
}

void Pool::delete_pool(std::size_t exponent) {
  BlockHeader *pool = pools[exponent].load();
  while (pool) {
    auto doomed = pool;
    pool = doomed->next;
    // No need to call ~BlockHeader(), it doesn't do anything.
    std::free(doomed);
  }
}

Pool::BlockHeader *Pool::new_block(std::size_t exponent, bool zeroed) {
  const std::size_t size = (1ULL << exponent) + sizeof(BlockHeader);
  void *pointer = zeroed ? std::calloc(size, 1)
                         : std::aligned_alloc(alignof(BlockHeader), size);
  return new (pointer) BlockHeader(exponent);
}

Pool::BlockHeader *Pool::get_block(std::size_t exponent, bool zeroed) {
  BlockHeader *current = pools[exponent].load();
  do {
    if (current == nullptr) {
      BlockHeader *fresh = new_block(exponent, zeroed);
      return fresh;
    }
  } while (!pools[exponent].compare_exchange_weak(current, current->next));

  if (zeroed) {
    char *bytes = static_cast<char *>(static_cast<void *>(current));
    std::memset(bytes + sizeof(BlockHeader), 0, 1ULL << exponent);
  }

  return current;
}

void Pool::put_block(BlockHeader *block) {
  auto exponent = block->log2_size;
  BlockHeader *head = pools[exponent].load();
  do {
    block->next = head;
  } while (!pools[exponent].compare_exchange_weak(head, block));
}

Pool::Pool() : pools{} {}

Pool::~Pool() {
  for (std::size_t exponent = 0; exponent < std::size(pools); ++exponent) {
    delete_pool(exponent);
  }
}

void *Pool::allocate_impl(std::size_t size, bool zeroed) {
  if (size == 0) {
    return nullptr;
  }

  if (size > (1ULL << (std::size(pools) - 1))) {
    throw std::bad_alloc();
  }

  std::size_t exponent = 0;
  std::size_t remaining = size;
  while (remaining >>= 1) {
    ++exponent;
  }

  if (1ULL << exponent != size) {
    ++exponent;
  }

  BlockHeader *block = get_block(exponent, zeroed);
  char *bytes = static_cast<char *>(static_cast<void *>(block));
  bytes += sizeof(BlockHeader);
  return bytes;
}

void *Pool::allocate(std::size_t size) { return allocate_impl(size, false); }

void *Pool::callocate(std::size_t count, std::size_t size_each) {
  return allocate_impl(count * size_each, true);
}

void *Pool::reallocate(void *pointer, std::size_t new_size) {
  if (pointer == nullptr) {
    return allocate(new_size);
  }

  auto block = block_cast(pointer);
  auto old_size = 1ULL << block->log2_size;
  if (new_size <= old_size) {
    return pointer;
  }

  auto result = allocate(new_size);
  std::memcpy(result, pointer, old_size);
  free(pointer);
  return result;
}

void Pool::free(void *pointer) {
  if (pointer == nullptr) {
    return;
  }
  put_block(block_cast(pointer));
}

char *Pool::duplicate(const char *string) {
  auto length = std::strlen(string);
  auto memory = allocate(length + 1);
  char *bytes = static_cast<char *>(memory);
  std::memcpy(bytes, string, length + 1);
  return bytes;
}

void Pool::debug(std::ostream &log) const {
  for (std::size_t i = 0; i < std::size(pools); ++i) {
    auto node = pools[i].load();
    if (!node) {
      continue;
    }
    log << i << ": ";
    do {
      auto address = static_cast<void *>(
          static_cast<char *>(static_cast<void *>(node)) + sizeof(BlockHeader));
      log << address << " -> ";
      node = node->next;
    } while (node);
    log << "nullptr\n";
  }
}
