#include <ngx_config.h>
#include <ngx_core.h>

#include "ngx_curl.h"

typedef struct {
  ngx_flag_t enable;
} ngx_curl_example_conf_t;

static void *ngx_curl_example_create_conf(ngx_cycle_t *cycle);
static char *ngx_curl_example_init_conf(ngx_cycle_t *cycle, void *conf);

static ngx_int_t ngx_curl_example_init_process(ngx_cycle_t *cycle);
static void ngx_curl_example_exit_process(ngx_cycle_t *cycle);

static char *ngx_curl_example_enable(ngx_conf_t *cf, void *post, void *data);
static ngx_conf_post_t ngx_curl_example_enable_post = {ngx_curl_example_enable};

static ngx_command_t ngx_curl_example_commands[] = {

    {ngx_string("curl_example_enabled"),
     NGX_MAIN_CONF | NGX_DIRECT_CONF | NGX_CONF_FLAG, ngx_conf_set_flag_slot, 0,
     offsetof(ngx_curl_example_conf_t, enable), &ngx_curl_example_enable_post},

    ngx_null_command};

static ngx_core_module_t ngx_curl_example_module_ctx = {
    ngx_string("curl_example"), ngx_curl_example_create_conf,
    ngx_curl_example_init_conf};

ngx_module_t ngx_curl_example_module = {
    NGX_MODULE_V1,
    &ngx_curl_example_module_ctx,   /* module context */
    ngx_curl_example_commands,      /* module directives */
    NGX_CORE_MODULE,                /* module type */
    NULL,                           /* init master */
    NULL,                           /* init module */
    &ngx_curl_example_init_process, /* init process */
    NULL,                           /* init thread */
    NULL,                           /* exit thread */
    &ngx_curl_example_exit_process, /* exit process */
    NULL,                           /* exit master */
    NGX_MODULE_V1_PADDING};

static void *ngx_curl_example_create_conf(ngx_cycle_t *cycle) {
  ngx_curl_example_conf_t *fcf;

  fcf = ngx_pcalloc(cycle->pool, sizeof(ngx_curl_example_conf_t));
  if (fcf == NULL) {
    return NULL;
  }

  fcf->enable = NGX_CONF_UNSET;

  return fcf;
}

static char *ngx_curl_example_init_conf(ngx_cycle_t *cycle, void *conf) {
  ngx_curl_example_conf_t *fcf = conf;

  ngx_conf_init_value(fcf->enable, 0);

  return NGX_CONF_OK;
}

static char *ngx_curl_example_enable(ngx_conf_t *cf, void *post, void *data) {
  ngx_flag_t *fp = data;

  if (*fp == 0) {
    return NGX_CONF_OK;
  }

  ngx_log_error(NGX_LOG_ERR, cf->log, 0, "curl example module is enabled");

  return NGX_CONF_OK;
}

static ngx_curl_t *curl;

static void on_error(CURL *handle, CURLcode error) {
  ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "oh no!");
  curl_easy_cleanup(handle);
}

static void on_done(CURL *handle) {
  ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "yay!");
  curl_easy_cleanup(handle);
}

static ngx_int_t ngx_curl_example_init_process(ngx_cycle_t *) {
  curl = ngx_create_curl();
  CURL *handle = curl_easy_init();
  curl_easy_setopt(handle, CURLOPT_URL, "http://www.google.com");
  ngx_curl_add_handle(curl, handle, &on_error, &on_done);
  return 0;
}

static void ngx_curl_example_exit_process(ngx_cycle_t *) {
  ngx_destroy_curl(curl);
}
