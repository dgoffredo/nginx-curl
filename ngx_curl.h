#pragma once

#include <curl/curl.h>

typedef struct ngx_curl_s ngx_curl_t;

typedef struct ngx_curl_allocator_s {
  void *(*allocate)(size_t size);
  void *(*callocate)(size_t count, size_t size_each);
  void *(*reallocate)(void *pointer, size_t new_size);
  void (*free)(void *pointer);
  char *(*duplicate)(const char *string);
} ngx_curl_allocator_t;

ngx_curl_t *ngx_create_curl(void);

ngx_curl_t *ngx_create_curl_with_allocator(const ngx_curl_allocator_t *);

void ngx_destroy_curl(ngx_curl_t *);

int ngx_curl_add_handle(ngx_curl_t *curl, CURL *handle,
                        void (*on_error)(CURL *, CURLcode),
                        void (*on_done)(CURL *));

int ngx_curl_remove_handle(ngx_curl_t *curl, CURL *handle);

const ngx_curl_allocator_t *ngx_curl_allocator(const ngx_curl_t *);
