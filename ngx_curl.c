// Nginx headers must go first.  It has something to do with competing
// preprocessor macros.
#include <ngx_core.h>
#include <ngx_event.h>

#include "ngx_curl.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const ngx_curl_allocator_t malloc_allocator = {&malloc, &calloc,
                                                      &realloc, &free, &strdup};

struct ngx_curl_s {
  const ngx_curl_allocator_t *allocator;
  CURLM *multi;
  ngx_connection_t dummy_connection;
  ngx_event_t timeout;
};

typedef struct ngx_curl_handle_context_s {
  void (*on_error)(CURL *, CURLcode);
  void (*on_done)(CURL *);
  // `user_data` is whatever was installed as the handle's "private" data
  // before we replaced it with this object.
  void *user_data;
} ngx_curl_handle_context_t;

static void process_messages(ngx_curl_t *curl);
static void on_connection_event(ngx_event_t *event);
static void on_timeout(ngx_event_t *event);
static int on_register_timer(CURLM *multi, long timeout_milliseconds,
                             void *user_data);
static int on_register_event(CURL *easy, curl_socket_t s, int what,
                             void *user_data, void *socket_context);

static void process_messages(ngx_curl_t *curl) {
  assert(curl);
  assert(curl->multi);
  assert(curl->allocator);

  struct CURLMsg *message;
  do {
    int num_messages = 0;
    message = curl_multi_info_read(curl->multi, &num_messages);
    if (!message || message->msg != CURLMSG_DONE) {
      continue;
    }

    CURL *handle = message->easy_handle;
    CURLMcode mrc = curl_multi_remove_handle(curl->multi, handle);
    if (mrc != CURLM_OK) {
      ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: DERP");
    }

    ngx_curl_handle_context_t *context;
    CURLcode rc = curl_easy_getinfo(handle, CURLINFO_PRIVATE, &context);
    if (rc != CURLE_OK) {
      ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: HERP DERP");
      // This is a leak.  What can we do?
      continue;
    }

    // Restore the original user data associated with the handle when it was
    // added.
    rc = curl_easy_setopt(handle, CURLOPT_PRIVATE, context->user_data);
    if (rc != CURLE_OK) {
      ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: giant burp");
      // This is a leak.  What can we do?
      continue;
    }

    // Finally, it's time to invoke a user-supplied callback.
    rc = message->data.result;
    if (rc == CURLE_OK) {
      context->on_done(handle);
    } else {
      context->on_error(handle, rc);
    }

    curl->allocator->free(context);
  } while (message);
}

static void on_connection_event(ngx_event_t *event) {
  assert(event);
  ngx_connection_t *connection = event->data;
  assert(connection);
  ngx_curl_t *curl = connection->data;
  assert(curl);
  assert(curl->multi);

  // From libcurl's documentation:
  //
  // > When the events on a socket are known, they can be passed as an events
  // > bitmask ev_bitmask by first setting ev_bitmask to 0, and then adding
  // > using bitwise OR (|) any combination of events to be chosen from
  // > CURL_CSELECT_IN, CURL_CSELECT_OUT or CURL_CSELECT_ERR. When the events
  // > on a socket are unknown, pass 0 instead, and libcurl will test the
  // > descriptor internally.
  int ev_bitmask = 0;
  if (connection->read->ready) {
    ev_bitmask |= CURL_CSELECT_IN;
  }
  if (connection->write->ready) {
    ev_bitmask |= CURL_CSELECT_OUT;
  }
  if (connection->read->error || connection->write->error) {
    ev_bitmask |= CURL_CSELECT_ERR;
  }

  int num_running_handles;
  CURLMcode mrc = curl_multi_socket_action(curl->multi, connection->fd,
                                           ev_bitmask, &num_running_handles);
  if (mrc != CURLM_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: slurp slurp");
  }

  process_messages(curl);
}

static void on_timeout(ngx_event_t *event) {
  assert(event);
  assert(event->data);
  // reminder: ngx_connection_t *dummy_connection = event->data;
  char *dummy_connection_address = event->data;
  ngx_curl_t *curl = (ngx_curl_t *)(dummy_connection_address -
                                    offsetof(ngx_curl_t, dummy_connection));
  assert(curl->multi);

  int num_running_handles;
  CURLMcode mrc = curl_multi_socket_action(curl->multi, CURL_SOCKET_TIMEOUT, 0,
                                           &num_running_handles);
  if (mrc != CURLM_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: timurp");
  }

  process_messages(curl);
}

static int on_register_timer(CURLM *multi, long timeout_milliseconds,
                             void *user_data) {
  assert(multi);
  assert(user_data);
  ngx_curl_t *curl = user_data;
  assert(curl->multi == multi);

  // From the libcurl docs:
  //
  // > Your callback function timer_callback should install a non-repeating
  // > timer with an expire time of timeout_ms milliseconds. When that timer
  // > fires, call either curl_multi_socket_action or curl_multi_perform,
  // > depending on which interface you use.
  // >
  // > A timeout_ms value of -1 passed to this callback means you should delete
  // > the timer. All other values are valid expire times in number of
  // > milliseconds.

  if (timeout_milliseconds == -1) {
    if (curl->timeout.timer_set) {
      ngx_del_timer(&curl->timeout);
    }
    return 0;
  }

  assert(curl->timeout.data == &curl->dummy_connection);
  curl->timeout.log = ngx_cycle->log;
  curl->timeout.handler = &on_timeout;
  curl->timeout.cancelable =
      true; // otherwise a pending timeout will prevent shutdown
  ngx_add_timer(&curl->timeout, timeout_milliseconds);
  return 0;
}

static int call_counter;

static int on_register_event(CURL *easy, curl_socket_t s, int what,
                             void *user_data, void *socket_context) {
  assert(easy);
  assert(user_data);
  ngx_curl_t *curl = user_data;

  // TODO: no
  ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "{{{{{{{{{{ call count %d",
                call_counter);
  ++call_counter;
  switch (what) {
  case CURL_POLL_IN:
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "[CURL_POLL_IN]");
    break;
  case CURL_POLL_OUT:
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "[CURL_POLL_OUT]");
    break;
  case CURL_POLL_INOUT:
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "[CURL_POLL_INOUT]");
    break;
  case CURL_POLL_REMOVE:
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "[CURL_POLL_REMOVE]");
    break;
  }

  ngx_connection_t *connection;
  if (socket_context == NULL) {
    // TODO: no
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "()()()()() No socket context.");

    // This socket (`s`) is new to us. Get an nginx connection for it and
    // associate that connection with the socket.
    connection = ngx_get_connection(s, ngx_cycle->log);
    if (connection == NULL) {
      // Returning -1 from this function screws up the whole multi-handle, but
      // what else can we do? Nginx has probably hit its connection limit.
      return -1;
    }

    // `on_connection_event` will dig this value out via `event->data->data`.
    connection->data = curl;

    // Associate the connection with the socket. That will be `socket_context`
    // the next time libcurl calls us about this socket (`s`).
    CURLMcode mrc = curl_multi_assign(curl->multi, s, connection);
    if (mrc != CURLM_OK) {
      ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                    "TODO: why is there so much error handling?");
      abort(); // TODO
      return -1;
    }
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "curl fd: %d nginx fd: %d", s,
                  connection->fd); // TODO
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "read active? %d write active? %d", connection->read->active,
                  connection->write->active); // TODO
  } else {
    connection = socket_context;
  }

  // TODO: In debug mode, we're crashing below due to the connection's read
  // event's `.log` being null.  Does this indicate that the connection
  // management is borked, or is it ok to just set the loggers here?
  connection->read->log = ngx_cycle->log;
  connection->write->log = ngx_cycle->log;
  ngx_set_connection_log(connection, ngx_cycle->log); // probably unnecessary

  // `what` values, from the curl docs:
  //
  // > CURL_POLL_IN
  // > Wait for incoming data. For the socket to become readable.
  // >
  // > CURL_POLL_OUT
  // > Wait for outgoing data. For the socket to become writable.
  // >
  // > CURL_POLL_INOUT
  // > Wait for incoming and outgoing data. For the socket to become readable or
  // > writable.
  // >
  // > CURL_POLL_REMOVE
  // > The specified socket/file descriptor is no longer used by libcurl for any
  // > active transfer. It might soon be added again.

  switch (what) {
  case CURL_POLL_IN:
    connection->read->handler = &on_connection_event;
    if (!connection->read->active) {
      if (ngx_add_event(connection->read, NGX_READ_EVENT, 0) != NGX_OK) {
        abort(); // TODO
        return -1;
      }
    }
    if (connection->write->active) {
      if (ngx_del_event(connection->write, NGX_WRITE_EVENT, 0) != NGX_OK) {
        abort(); // TODO
        return -1;
      }
    }
    break;
  case CURL_POLL_OUT:
    connection->write->handler = &on_connection_event;
    if (!connection->write->active) {
      if (ngx_add_event(connection->write, NGX_WRITE_EVENT, 0) != NGX_OK) {
        abort(); // TODO
        return -1;
      }
    }
    if (connection->read->active) {
      if (ngx_del_event(connection->read, NGX_READ_EVENT, 0) != NGX_OK) {
        abort(); // TODO
        return -1;
      }
    }
    break;
  case CURL_POLL_INOUT:
    connection->read->handler = &on_connection_event;
    if (!connection->read->active) {
      if (ngx_add_event(connection->read, NGX_READ_EVENT, 0) != NGX_OK) {
        abort(); // TODO
        return -1;
      }
    }
    connection->write->handler = &on_connection_event;
    if (!connection->write->active) {
      if (ngx_add_event(connection->write, NGX_WRITE_EVENT, 0) != NGX_OK) {
        abort(); // TODO
        return -1;
      }
    }
    break;
  case CURL_POLL_REMOVE: {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "[][][][][][][] removing..."); // TODO: no
    // Remove the connection from nginx's event loop.
    // Pass zero for the flags so that nginx actually removes the socket from
    // the event loop.
    if (ngx_del_conn(connection, 0) != NGX_OK) {
      abort(); // TODO
      return -1;
    }
    ngx_free_connection(connection); // TODO?

    // The user data associated with the socket appears to be cleared by
    // libcurl anyway, but let's be explicit with cleanup here.
    CURLMcode mrc = curl_multi_assign(curl->multi, s, NULL);
    if (mrc != CURLM_OK) {
      ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: catch on fire!");
      abort(); // TODO
      return -1;
    }
  } break;
  default:
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: eeeeeeeeeeeeeeeeee!");
    abort(); // TODO
    return -1;
  }

  ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "}}}}}}}}}} end call");
  return 0; // success
}

ngx_curl_t *ngx_create_curl(void) {
  const ngx_curl_options_t default_options = {
      // `ngx_create_curl_with_options` will choose defaults for
      // any options that are NULL.
      .allocator = NULL};
  return ngx_create_curl_with_options(&default_options);
}

ngx_curl_t *ngx_create_curl_with_options(const ngx_curl_options_t *options) {
  const ngx_curl_allocator_t *allocator = options->allocator;
  if (allocator == NULL) {
    allocator = &malloc_allocator;
  }

  CURLcode rc = curl_global_init_mem(
      CURL_GLOBAL_DEFAULT, allocator->allocate, allocator->free,
      allocator->reallocate, allocator->duplicate, allocator->callocate);
  if (rc != CURLE_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: the horror!");
    abort(); // TODO
    return NULL;
  }

  ngx_curl_t *curl = allocator->callocate(1, sizeof(ngx_curl_t));
  curl->allocator = allocator;

  // Initialize the dummy connection.  In debug mode, nginx assumes that the
  // `void *ngx_event_t::data` member of the argument to `ngx_add_timer` points
  // to an `ngx_connection_t`. This is understandable, because nginx uses timer
  // events for I/O timeouts associated with connections.  The debug form of
  // `ngx_add_timer` logs the file descriptor (`fd`) associated with the
  // connection associated with the timer event.  However, we use timers that
  // aren't associated with a connection, and we need the event's data pointer
  // to refer to our context, the `ngx_curl_t`. Crash!
  //
  // To work around this, we have our timer event's data pointer point to a
  // dummy `ngx_connection_t`. We then populate the connection's `fd` with a
  // clearly-not-real value (`-1`), and we could have the connection's data
  // pointer refer to `ngx_curl_t`. However, because the dummy connection is a
  // member of `ngx_curl_t`, we instead ignore the connection's data pointer
  // and use `offsetof` to get a pointer to the enclosing `ngx_curl_t`.
  curl->dummy_connection.fd = -1;
  curl->timeout.data = &curl->dummy_connection;

  curl->multi = curl_multi_init();
  if (curl->multi == NULL) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: no no no no!");
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

  mrc = curl_multi_setopt(curl->multi, CURLMOPT_TIMERDATA, curl);
  if (mrc != CURLM_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: it isn't right!");
    curl_multi_cleanup(curl->multi);
    curl_global_cleanup();
    allocator->free(curl);
    abort(); // TODO
    return NULL;
  }

  mrc = curl_multi_setopt(curl->multi, CURLMOPT_TIMERFUNCTION,
                          &on_register_timer);

  return curl;
}

// TODO: Make the contract that `ngx_curl_remove_handle` must have been
// called for all registered easy handles first.
void ngx_destroy_curl(ngx_curl_t *curl) {
  CURLMcode rc = curl_multi_cleanup(curl->multi);
  if (rc != CURLM_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: shit!");
    abort(); // TODO
  }

  if (curl->timeout.timer_set) {
    ngx_del_timer(&curl->timeout);
  }

  curl->allocator->free(curl);
}

int ngx_curl_add_handle(ngx_curl_t *curl, CURL *handle,
                        void (*on_error)(CURL *, CURLcode),
                        void (*on_done)(CURL *)) {
  assert(curl);
  assert(handle);
  assert(on_error);
  assert(on_done);
  assert(curl->multi);
  assert(curl->allocator);

  ngx_curl_handle_context_t *context =
      curl->allocator->callocate(1, sizeof(ngx_curl_handle_context_t));
  context->on_error = on_error;
  context->on_done = on_done;

  CURLcode rc =
      curl_easy_getinfo(handle, CURLINFO_PRIVATE, &context->user_data);
  if (rc != CURLE_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: why? WHY?");
    curl->allocator->free(context);
    return -1;
  }

  rc = curl_easy_setopt(handle, CURLOPT_PRIVATE, context);
  if (rc != CURLE_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "TODO: deliver us from this evil, Lord");
    curl->allocator->free(context);
    return -2;
  }

  CURLMcode mrc = curl_multi_add_handle(curl->multi, handle);
  if (mrc != CURLM_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: I am a giant egg");
    (void)curl_easy_setopt(handle, CURLOPT_PRIVATE, context->user_data);
    curl->allocator->free(context);
    return -3;
  }

  // From the libcurl docs:
  //
  // > When you have added your initial set of handles, you call
  // > curl_multi_socket_action with CURL_SOCKET_TIMEOUT set in the sockfd
  // > argument, and you will get callbacks call that sets you up and you then
  // > continue to call curl_multi_socket_action accordingly when you get
  // > activity on the sockets you have been asked to wait on, or if the timeout
  // > timer expires.
  int num_running_handles;
  mrc = curl_multi_socket_action(curl->multi, CURL_SOCKET_TIMEOUT, 0,
                                 &num_running_handles);
  if (mrc != CURLM_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: wahwahwahwahwah");
  }

  return 0;
}

int ngx_curl_remove_handle(ngx_curl_t *curl, CURL *handle) {
  assert(curl);
  assert(handle);
  assert(curl->multi);
  assert(curl->allocator);

  // Goals:
  // - Restore the user_data associated with handle.
  // - Delete the context associated with handle.
  // - Remove handle from curl->multi.

  ngx_curl_handle_context_t *context;
  CURLcode rc = curl_easy_getinfo(handle, CURLINFO_PRIVATE, &context);
  if (rc != CURLE_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: hurrrrrrrr");
    return -1;
  }

  rc = curl_easy_setopt(handle, CURLOPT_PRIVATE, context->user_data);
  curl->allocator->free(context);
  if (rc != CURLE_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "TODO: durrrrrrrrrr");
    return -2;
  }

  CURLMcode mrc = curl_multi_remove_handle(curl->multi, handle);
  if (mrc != CURLM_OK) {
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                  "TODO: shit shit shit shit shit");
    return -3;
  }

  return 0;
}

const ngx_curl_allocator_t *ngx_curl_allocator(const ngx_curl_t *curl) {
  assert(curl);
  return curl->allocator;
}
