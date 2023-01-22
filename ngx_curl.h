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

// "user data" considerations:
// - Associate ngx_curl_t* with CURLMOPT_SOCKETFUNCTION
// - When the CURLMOPT_SOCKETFUNCTION is called, see whether the user data is
null
//     - if null, use curl_multi_assign to associate nginx events with the
socket
//     - if not null, interpret the user data as nginx events
//     - the nginx events are an nginx connection, which contains a read event
//       and a write event.
//     - connections are pooled. Get one with ngx_get_connection(socket,
ngx_cycle->log)
//     - when you ngx_add_connection, both events are added
//     - if curl is interested in only read, or only write, then ngx_del_event
//       the other (connection->read or connection->write) (with flags = 0)
//     - if curl indicates that it's done with the socket, call
ngx_del_connection
//       (with flags = NGX_CLOSE_EVENT)

*/

typedef struct ngx_curl_s ngx_curl_t;

typedef enum ngx_curl_allocation_policy_e {
  NGX_CURL_SYSTEM_ALLOCATOR,
  NGX_CURL_POOL_ALLOCATOR
} ngx_curl_allocation_policy_t;

ngx_curl_t *ngx_create_curl(ngx_curl_allocation_policy_t);

void ngx_destroy_curl(ngx_curl_t *);

ngx_curl_add_handle(ngx_curl_t *curl, CURL *handle,
                    void (*on_error)(CURL *, CURLcode),
                    void (*on_done)(CURL *));

ngx_curl_remove_handle(ngx_curl_t *curl, CURL *handle);
