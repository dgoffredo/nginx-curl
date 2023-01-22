#include "ngx_curl.h"

#include <nginx.h>    // TODO?
#include <ngx_core.h> // TODO?

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// TODO static ngx_curl_pool_t pool = {0};

typedef struct ngx_curl_allocator_s {
  void *(*allocate)(size_t size);
  void *(*callocate)(size_t count, size_t size_each);
  void *(*reallocate)(void *pointer, size_t new_size);
  void (*free)(void *pointer);
  char *(*duplicate)(const char *string);
} ngx_curl_allocator_t;

/*
TODO

static void *allocate_from_pool(size_t size) {
  return ngx_curl_pool_allocate(&pool, size);
}

static void *callocate_from_pool(size_t count, size_t size_each) {
  return ngx_curl_pool_callocate(&pool, count, size_each);
}

static void *reallocate_from_pool(void* pointer, size_t new_size) {
  return ngx_curl_pool_reallocate(&pool, pointer, new_size);
}

static void *free_from_pool(void* pointer) {
  ngx_curl_pool_free(&pool, pointer);
}

static char *duplicate_from_pool(const char *string) {
  return ngx_curl_pool_duplicate(&pool, string);
}
*/

static const ngx_curl_allocator_t system_allocator = {&malloc, &calloc,
                                                      &realloc, &free, &strdup};

/* TODO
static const ngx_curl_allocator_t pool_allocator = {
  &allocate_from_pool,
  &callocate_from_pool,
  &reallocate_from_pool,
  &free_from_pool,
  &duplicate_from_pool,
};
*/

struct ngx_curl_s {
  ngx_curl_allocator_t *allocator;
  CURLM *multi;
  ngx_event_t timeout;
};

typedef struct ngx_curl_handle_context_s {
  void (*on_error)(CURL *, CURLcode), void (*on_done)(CURL *);
  // `user_data` is whatever was installed as the handle's "private" data
  // before we replaced it with this object.
  void *user_data;
} ngx_curl_handle_context_t;

static int on_register_event(CURL *easy, curl_socket_t s, int what,
                             void *user_data, void *socket_context) {
  assert(user_data);
  ngx_curl_t *curl = user_data;

  ngx_connection_t *connection;
  if (socket_context == NULL) {
    // This socket (`s`) is new to us. Get an nginx connection for it and
    // associate that connection with the socket.
    connection = ngx_get_connection(s, ngx_cycle->log);
    if (connection == NULL) {
      // Returning -1 from this function screws up the whole multi-handle, but
      // what else can we do? Nginx has probably hit its connection limit.
      return -1;
    }
    CURLMcode mrc = curl_multi_assign(curl->multi, s, connection);
    if (mrc != CURLM_OK) {
      // TODO: log with ngx_cycle->log
      return -1;
    }
    ngx_int_t rc = ngx_add_conn(connection);
    if (rc != NGX_OK) {
      return -1;
    }
  } else {
    connection = socket_context;
  }

  /* `what` values, from the curl docs:

  CURL_POLL_IN
  Wait for incoming data. For the socket to become readable.

  CURL_POLL_OUT
  Wait for outgoing data. For the socket to become writable.

  CURL_POLL_INOUT
  Wait for incoming and outgoing data. For the socket to become readable or
  writable.

  CURL_POLL_REMOVE
  The specified socket/file descriptor is no longer used by libcurl for any
  active transfer. It might soon be added again.
  */

  switch (what) {
  case CURL_POLL_IN:
    if (ngx_add_event(connection->read, NGX_READ_EVENT, 0) != NGX_OK) {
      return -1;
    }
    if (ngx_del_event(connection->write, NGX_WRITE_EVENT, 0) != NGX_OK) {
      return -1;
    }
    break;
  case CURL_POLL_OUT:
    if (ngx_add_event(connection->write, NGX_WRITE_EVENT, 0) != NGX_OK) {
      return -1;
    }
    if (ngx_del_event(connection->read, NGX_READ_EVENT, 0) != NGX_OK) {
      return -1;
    }
    break;
  case CURL_POLL_INOUT:
    if (ngx_add_event(connection->read, NGX_READ_EVENT, 0) != NGX_OK) {
      return -1;
    }
    if (ngx_add_event(connection->write, NGX_WRITE_EVENT, 0) != NGX_OK) {
      return -1;
    }
    break;
  case CURL_POLL_REMOVE: {
    if (ngx_del_conn(connection, NGX_CLOSE_EVENT) != NGX_OK) {
      return -1;
    }
    CURLMcode mrc = curl_multi_assign(curl->multi, s, NULL);
    if (mrc != CURLM_OK) {
      // TODO: log with ngx_cycle->log
      return -1;
    }
  } break;
  default:
    // TODO: log with ngx_cycle->log
    return -1;
  }

  return 0; // success
}

ngx_curl_t *ngx_create_curl(ngx_curl_allocation_policy_t policy) {
  ngx_curl_allocator_t *allocator;
  switch (policy) {
  case NGX_CURL_SYSTEM_ALLOCATOR:
    allocator = &system_allocator;
    break;
  default:
    assert(policy == NGX_CURL_POOL_ALLOCATOR);
    allocator = &pool_allocator;
  }

  CURLcode rc = curl_global_init_mem(
      CURL_GLOBAL_DEFAULT, allocator->allocate, allocator->free,
      allocator->reallocate, allocator->callocate, allocator->duplicate);
  if (rc != CURLE_OK) {
    // TODO: log (ngx_cycle->log)
    abort(); // TODO
    return NULL;
  }

  ngx_curl_t *curl = allocator->callocate(1, sizeof(ngx_curl_t));
  curl->allocator = allocator;

  curl->multi = curl_multi_init();
  if (curl->multi == NULL) {
    // TODO: log (ngx_cycle->log)
    curl_global_cleanup();
    allocator->free(curl);
    abort(); // TODO
    return NULL;
  }

  CURLMcode mrc = curl_multi_setopt(curl->multi, CURLMOPT_SOCKETFUNCTION,
                                    on_register_event);
  assert(mrc == CURLM_OK); // that's what the docs say
  mrc = curl_multi_setopt(curl->multi, CURLMOPT_SOCKETDATA, curl);
  assert(mrc == CURLM_OK); // that's what the docs say

  return curl;
}

// TODO: Make the contract that `ngx_curl_remove_handle` must have been
// called for all registered easy handles first.
void ngx_destroy_curl(ngx_curl_t *curl) {
  CURLMcode rc = curl_multi_cleanup(curl->multi);
  if (rc != CURLM_OK) {
    // TODO: log (ngx_cycle->log)
    abort(); // TODO
  }

  curl->allocator->free(curl);
}

ngx_curl_add_handle(ngx_curl_t *curl, CURL *handle,
                    void (*on_error)(CURL *, CURLcode),
                    void (*on_done)(CURL *)) {
  // TODO
}

ngx_curl_remove_handle(ngx_curl_t *curl, CURL *handle) {
  // TODO
}
