#include "pool.h"

#include <cassert>
#include <new>

int main() {
  struct Foo {
    int x;
    double y;
    char cusip[9];
  };

  Pool pool;
  void *storage = pool.allocate(sizeof(Foo));
  Foo *foo = new (storage) Foo{1, 2.0, "foo"};
  foo->~Foo();
  pool.free(foo);

  void *new_storage = pool.allocate(sizeof(Foo));
  assert(new_storage == storage);
  pool.free(new_storage);
}
