#pragma once

// This component allows libcurl requests to be fulfilled using nginx's event
// loop.
//
// Requests are managed by a `ngx_curl_t*`, which is created by the functions
// `ngx_create_curl` and `ngx_create_curl_with_options`. `ngx_curl_t*` is freed
// by the function `ngx_destroy_curl`.
//
// Then libcurl "easy" handles (`CURL*`) can be added by the function
// `ngx_curl_add_handle`.  Associated with the request handle is an `on_done`
// callback and an `on_error` callback. Request context can be associated with
// the handle via `curl_easy_setopt(...CURLOPT_PRIVATE...)`.
//
// If `ngx_curl_add_handle` returns zero, indicating success, then the request
// is registered with libcurl to be fulfilled using nginx's event loop.
// Eventually one of either `on_error` or `on_done` will be invoked. `on_done`
// is invoked when the request has completed successfully. `on_error` is
// invoked if something goes wrong with the request before a complete response
// is received.
//
// If `ngx_curl_add_handle` returns a nonzero value, then an error occurred
// before the request could begin.
//
// The caller of `ngx_curl_add_handle` is responsible for freeing the `CURL*`
// handle. It is natural to do this in the `on_done` and `on_error` callbacks.
//
// A `CURL*` handle is removed from the `ngx_curl_t*` when the request is
// complete, or when an error occurs. To remove a handle before then, use the
// `ngx_curl_remove_handle` function.
//
// Before calling `ngx_destroy_curl` to free a `ngx_curl_t*`, the caller must
// ensure that no `CURL*` handles remain registered. Outstanding handles can
// be removed by the `ngx_curl_remove_handle` function. There is no way to
// enumerate the outstanding handles, so calling code might need to keep track
// of the `CURL*` handles added.
//
// `ngx_create_curl_with_options` allows the specification of a memory
// allocator to be used by this library and by libcurl. The allocator will be
// used by libcurl only if this library is the first to initialize libcurl (the
// first to call one of the `curl_global_init*` family of functions). This
// library will use the allocator regardless. The default allocator uses the
// C standard library functions (e.g. `malloc`).

#include <curl/curl.h>

typedef struct ngx_curl_s ngx_curl_t;

typedef struct ngx_curl_allocator_s {
  void *(*allocate)(size_t size); // e.g. malloc
  void *(*callocate)(size_t count, size_t size_each); // e.g. calloc
  void *(*reallocate)(void *pointer, size_t new_size); // e.g. realloc
  void (*free)(void *pointer); // e.g. free
  char *(*duplicate)(const char *string); // e.g. strdup
} ngx_curl_allocator_t;

typedef struct ngx_curl_options_s {
  const ngx_curl_allocator_t *allocator;
} ngx_curl_options_t;

ngx_curl_t *ngx_create_curl(void);

ngx_curl_t *ngx_create_curl_with_options(const ngx_curl_options_t *);

void ngx_destroy_curl(ngx_curl_t *);

int ngx_curl_add_handle(ngx_curl_t *curl, CURL *handle,
                        void (*on_error)(CURL *, CURLcode),
                        void (*on_done)(CURL *));

int ngx_curl_remove_handle(ngx_curl_t *curl, CURL *handle);

const ngx_curl_allocator_t *ngx_curl_allocator(const ngx_curl_t *);
