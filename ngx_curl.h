#pragma once

// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO

#include <curl/curl.h>

/* notes:
ngx_curl_add_handle(
        ngx_curl_t*,
        CURL*,
        on_error,
        on_done,
        on_response_header,
        on_response_data);

// Handle management:
// - When does the handle get cleaned up?
// - When are the nginx read/write/timeout events removed?

// Memory management:
// - nginx pool: per-request or per-cycle?

// Name resolving:
// - before adding the curl handle to the underlying multi-handle,
//   do an async name lookup using nginx's resolver, and then use
//   CURLOPT_RESOLVE to override the DNS cache with the resulting
//   resolved addresses.

*/

typedef struct ngx_curl_s ngx_curl_t;

// TODO
ngx_curl_t *ngx_create_curl();

// TODO
void ngx_destroy_curl(ngx_curl_t *);

ngx_curl_add_handle(ngx_curl_t *curl, CURL *handle,
                    ngx_curl_error_callback_t on_error,
                    ngx_curl_done_callback_t on_done,
                    ngx_curl_response_header_callback_t on_response_header,
                    ngx_curl_response_data_callback_t on_response_data);

ngx_curl_remove_handle(ngx_curl_t *curl, CURL *handle);
