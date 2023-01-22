#include "ngx_curl.h"

#include <nginx.h>    // TODO?
#include <ngx_core.h> // TODO?

struct ngx_curl_s {
  CURLM *multi;
  // ...
};
