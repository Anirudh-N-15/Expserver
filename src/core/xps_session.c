#include "../xps.h"
#include "xps_session.h"

void client_source_handler(void *ptr);
void client_source_close_handler(void *ptr);
void client_sink_handler(void *ptr);
void client_sink_close_handler(void *ptr);
void upstream_source_handler(void *ptr);
void upstream_source_close_handler(void *ptr);
void upstream_sink_handler(void *ptr);
void upstream_sink_close_handler(void *ptr);
void file_sink_handler(void *ptr);
void file_sink_close_handler(void *ptr);

void set_to_client_buff(xps_session_t *session, xps_buffer_t *buff);
void set_from_client_buff(xps_session_t *session, xps_buffer_t *buff);
void session_check_destroy(xps_session_t *session);
void xps_session_destroy(xps_session_t *session);
void upstream_error_res(xps_session_t *session);

xps_session_t *xps_session_create(xps_core_t *core, xps_connection_t *client) {
  /* validate parameters */
  assert(core != NULL);
  assert(client != NULL);

  // Alloc memory for session instance
  xps_session_t *session = malloc(sizeof(xps_session_t));
  if (session == NULL) {
    logger(LOG_ERROR, "xps_session_create()", "malloc() failed for 'session'");
    return NULL;
  }

  session->client_source = xps_pipe_source_create(session, client_source_handler, client_source_close_handler);
  session->client_sink = xps_pipe_sink_create(session, client_sink_handler, client_sink_close_handler);
  session->upstream_source = xps_pipe_source_create(session, upstream_source_handler, upstream_source_close_handler);
  session->upstream_sink = xps_pipe_sink_create(session, upstream_sink_handler, upstream_sink_close_handler);
  session->file_sink = xps_pipe_sink_create(session, file_sink_handler, file_sink_close_handler);

  if (!(session->client_source && session->client_sink && session->upstream_source &&
        session->upstream_sink && session->file_sink)) {
    logger(LOG_ERROR, "xps_session_create()", "failed to create some sources/sinks");

    if (session->client_source)
      xps_pipe_source_destroy(session->client_source);
    if (session->client_sink)
      xps_pipe_sink_destroy(session->client_sink);
    if (session->upstream_source)
      xps_pipe_source_destroy(session->upstream_source);
    if (session->upstream_sink)
      xps_pipe_sink_destroy(session->upstream_sink);
    if (session->file_sink)
      xps_pipe_sink_destroy(session->file_sink);

    free(session);
    return NULL;
  }

  // Init values
  session->core = core;
  session->client = client;
  session->upstream = NULL;
  session->upstream_connected = false;
  session->upstream_error_res_set = false;
  session->upstream_write_bytes = 0;
  session->file = NULL;
  session->to_client_buff = NULL;
  session->from_client_buff = NULL;
  session->client_sink->ready = true;
  session->upstream_sink->ready = true;
  session->file_sink->ready = true;

  /*NOTE: We will be adding list of sessions as vec_void_t sessions to xps_core_s in xps_core module below*/
  // Add current session to core->sessions
  vec_push(&(core->sessions), session);

  // Attach client
  if (xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, client->source, session->client_sink) ==
        NULL ||
      xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, session->client_source, client->sink) ==
        NULL) {
    logger(LOG_ERROR, "xps_session_create()", "failed to create client pipes");

    if (session->client_source)
      xps_pipe_source_destroy(session->client_source);
    if (session->client_sink)
      xps_pipe_sink_destroy(session->client_sink);
    if (session->upstream_source)
      xps_pipe_source_destroy(session->upstream_source);
    if (session->upstream_sink)
      xps_pipe_sink_destroy(session->upstream_sink);
    if (session->file_sink)
      xps_pipe_sink_destroy(session->file_sink);

    free(session);
    return NULL;
  }

  /*
   * Endpoint readiness is driven by epoll or by a queued buffer.  It must
   * not be enabled merely because a pipe was created.
   */
  client->source->ready = false;
  client->sink->ready = false;
  set_to_client_buff(session, NULL);
  set_from_client_buff(session, NULL);

  logger(LOG_DEBUG, "xps_session_create()", "created session");

  if (client->listener->port == 8001) {
    xps_connection_t *upstream = xps_upstream_create(core, "0.0.0.0", 3000);
    if (upstream == NULL) {
      logger(LOG_ERROR, "xps_session_create()", "xps_upstream_create() failed");
      perror("Error message");
      xps_session_destroy(session);
      return NULL;
    }
    session->upstream = upstream;
    xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, upstream->source, session->upstream_sink);
    xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, session->upstream_source, upstream->sink);
  }

  else if (client->listener->port == 8002) {
    int error;
    xps_file_t *file = xps_file_create(core, "../public/sample.txt", &error);
    if (file == NULL) {
      logger(LOG_ERROR, "xps_session_create()", "xps_file_create() failed");
      perror("Error message");
      xps_session_destroy(session);
      return NULL;
    }
    /* assign to the file member */
    session->file = file;
    xps_pipe_create(core, DEFAULT_PIPE_BUFF_THRESH, file->source, session->file_sink);
  } 

  return session;
}

void client_source_handler(void *ptr) {
  /* validate parameters */
  assert(ptr != NULL);


  xps_pipe_source_t *source = ptr;
  xps_session_t *session = (xps_session_t *)source->ptr;

  // write to session->to_client_buff
  if (xps_pipe_source_write(source, session->to_client_buff) != OK) {
    logger(LOG_ERROR, "client_source_handler()", "xps_pipe_source_write() failed");
    return;
  }
  xps_buffer_destroy(session->to_client_buff);

  set_to_client_buff(session, NULL);
  session_check_destroy(session);
}

void client_source_close_handler(void *ptr) {
  assert(ptr != NULL);

  xps_pipe_source_t *source = ptr;
  xps_session_t *session = source->ptr;

  session_check_destroy(session);
}

void client_sink_handler(void *ptr) {
  assert(ptr != NULL);

  xps_pipe_sink_t *sink = ptr;
  xps_session_t *session = sink->ptr;

  size_t in_pipe = sink->pipe->buff_list->len;

  xps_buffer_t *buff = xps_pipe_sink_read(sink, in_pipe);
  if (buff == NULL) {
    logger(LOG_ERROR, "client_sink_handler()", "xps_pipe_sink_read() failed");
    return;
  }

  if (session->client->listener->port == 8003 ||
      session->client->listener->port == 8004) {
    set_to_client_buff(session, buff);
  } else {
    set_from_client_buff(session, buff);
  }
  xps_pipe_sink_clear(sink,in_pipe);
}

void client_sink_close_handler(void *ptr) {
    assert(ptr != NULL);
    xps_pipe_sink_t * sink = (xps_pipe_sink_t * ) ptr;
    xps_session_t * session = (xps_session_t * ) sink -> ptr;
    assert(session != NULL);
    session_check_destroy(session);
}

void upstream_source_handler(void *ptr) {
  /*assert*/
  assert(ptr != NULL);
  
  /*set ptr as source*/
  xps_pipe_source_t *source = (xps_pipe_source_t *) ptr;
  /*set session as source->ptr*/
  xps_session_t *session = (xps_session_t *) source->ptr;

  if (xps_pipe_source_write(source, session->from_client_buff) != OK) {
    logger(LOG_ERROR, "upstream_source_handler()", "xps_pipe_source_write() failed");
    return;
  }

  // Checking if upstream is connected
  if (session->upstream_connected == false) {
    session->upstream_write_bytes += session->from_client_buff->len;
    if (session->upstream_write_bytes > session->upstream_source->pipe->buff_list->len)
      session->upstream_connected = true;
  }

  xps_buffer_destroy(session->from_client_buff);

  set_from_client_buff(session, NULL);
  session_check_destroy(session);
}

void upstream_source_close_handler(void *ptr) {
  /* fill this */
  assert(ptr != NULL);
  xps_pipe_source_t *source = (xps_pipe_source_t *) ptr;
  xps_session_t *session = (xps_session_t *) source->ptr;

  if (!session->upstream_connected && !session->upstream_error_res_set) {
    upstream_error_res(session);
  }

  /* fill this */
  session_check_destroy(session);
}

void upstream_sink_handler(void *ptr) {
     /* fill this */
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = (xps_pipe_sink_t *) ptr;
    xps_session_t *session = (xps_session_t *) sink->ptr;

    session->upstream_connected = true;

    xps_buffer_t *buff = xps_pipe_sink_read(sink,sink->pipe->buff_list->len);
    if (buff == NULL) {
        logger(LOG_ERROR, "upstream_sink_handler()", "xps_pipe_sink_read() failed");
        return;
    }

    set_to_client_buff(session,buff);
    xps_pipe_sink_clear(sink,sink->pipe->buff_list->len);
}

void upstream_sink_close_handler(void *ptr) {
    /* fill this */
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = (xps_pipe_sink_t *) ptr;
    xps_session_t *session = (xps_session_t *) sink->ptr;

    if (!session->upstream_connected && !session->upstream_error_res_set) {
        upstream_error_res(session);
    }

    /* fill this */
    xps_session_destroy(session);
}

void upstream_error_res(xps_session_t *session) {
    assert(session != NULL);

    session->upstream_error_res_set = true;
}

void file_sink_handler(void *ptr) {
  /* fill this */
  assert(ptr != NULL);
  xps_pipe_sink_t *sink = (xps_pipe_sink_t *)ptr;
  xps_session_t *session = (xps_session_t *)sink->ptr;

  xps_buffer_t *buff = xps_pipe_sink_read(sink,sink->pipe->buff_list->len);
  if (buff == NULL) {
    logger(LOG_ERROR, "file_sink_handler()", "xps_pipe_sink_read() failed");
    return;
  }

  set_to_client_buff(session,buff);
  xps_pipe_sink_clear(sink,sink->pipe->buff_list->len);
}

void file_sink_close_handler(void *ptr) {

    /* fill this */
    assert(ptr != NULL);
    xps_pipe_sink_t *sink = (xps_pipe_sink_t *)ptr;
    xps_session_t *session = (xps_session_t *)sink->ptr;

    session_check_destroy(session);

}

void set_to_client_buff(xps_session_t *session, xps_buffer_t *buff) {
  /* validate parameters */
  assert(session != NULL);

  session->to_client_buff = buff;

  if (buff == NULL) {
    session->client_source->ready = false;
    session->upstream_sink->ready = true;
    session->file_sink->ready = true;
  } else {
    session->client_source->ready = true;
    session->upstream_sink->ready = false;
    session->file_sink->ready = false;
  }
}

void set_from_client_buff(xps_session_t *session, xps_buffer_t *buff) {
  /* validate parameters */
  assert(session != NULL);

  session->from_client_buff = buff;

  if (buff == NULL) {
    session->client_sink->ready = true;
    session->upstream_source->ready = false;
  } else {
    session->client_sink->ready = false;
    session->upstream_source->ready = true;
  }
}

void session_check_destroy(xps_session_t *session) {
  /* validate parameters */
  assert(session != NULL);

  /*
   * Persistent client sessions are owned by the connection and are cleaned
   * up when that connection closes, not after an individual echo response.
   */
  if (session->client->listener->port == 8003 ||
      session->client->listener->port == 8004) {
    return;
  }

  bool c2u_flow =
    session->upstream_source->active && (session->client_sink->active || session->from_client_buff);

  bool u2c_flow = session->client_source->active && (session->upstream_sink->active || session->to_client_buff);

  bool f2c_flow = session->file_sink->active && (session->file_sink->active || session->to_client_buff);

  bool flowing = c2u_flow || u2c_flow || f2c_flow;

  if (!flowing) xps_session_destroy(session);
}

void xps_session_destroy(xps_session_t *session) {
     /* validate parameters */
    assert(session != NULL);

    /* destroy client_source, client_sink, upstream_source, upstream_sink and file_sink attached to session */
    if (session->client_source != NULL)
        xps_pipe_source_destroy(session->client_source);
    if (session->client_sink != NULL)
        xps_pipe_sink_destroy(session->client_sink);
    if (session->upstream_source != NULL)
        xps_pipe_source_destroy(session->upstream_source);
    if (session->upstream_sink != NULL)
        xps_pipe_sink_destroy(session->upstream_sink);
    if (session->file_sink != NULL)
        xps_pipe_sink_destroy(session->file_sink);

    if (session->to_client_buff != NULL)
        xps_buffer_destroy(session->to_client_buff);
    if (session->from_client_buff != NULL)
        xps_buffer_destroy(session->from_client_buff);

    // Set NULL in core's list of sessions
    /* fill this */
    for (int i = 0; i < session -> core -> sessions.length; i++) {
        if (session -> core -> sessions.data[i] == session) {
            session -> core -> sessions.data[i] = NULL;
            session -> core -> n_null_sessions++;
            break;
        }
    }

    free(session);

    logger(LOG_DEBUG, "xps_session_destroy()", "destroyed session");
}