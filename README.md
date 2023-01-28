![nginx and curl logos combined](nginx-curl.png)

Why
---
I want to use [libcurl][1] to make HTTP requests from inside an [nginx
module][2], but I want to do it using nginx's event loop.

What
----
This is a collection of C functions providing an asynchronous wrapper around
libcurl.  `CURL` requests made with this library are fulfilled by libcurl using
nginx's event loop.

If you're writing an nginx module and want to make requests that are unrelated
to nginx's request processing, you can use this to make the "extra" requests.

It's a lot easier than [using nginx's HTTP facilities directly][3], but
minimizes the overhead incurred by using libcurl within nginx.

How
---
See [ngx_curl.h](ngx_curl.h).

Copy [ngx_curl.h](ngx_curl.h) and [ngx_curl.c](ngx_curl.c) into your nginx
module and compile with the rest of it. The code depends on nginx headers
and on libcurl.

[1]: https://curl.se/libcurl/
[2]: http://nginx.org/en/docs/dev/development_guide.html#Modules
[3]: http://nginx.org/en/docs/dev/development_guide.html#http_requests_to_ext
