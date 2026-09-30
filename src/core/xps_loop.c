#include "xps_loop.h"
#include <errno.h>
#include <fcntl.h>

loop_event_t *loop_event_create(u_int fd, void *ptr, xps_handler_t read_cb, xps_handler_t write_cb, xps_handler_t close_cb) {
  assert(ptr != NULL);

  // Alloc memory for 'event' instance
  loop_event_t *event = malloc(sizeof(loop_event_t));
  if (event == NULL) {
    logger(LOG_ERROR, "event_create()", "malloc() failed for 'event'");
    return NULL;
  }

  /* set fd, ptr, and callback fields of event */
    event->fd = fd;
    event->ptr = ptr;
    event->read_cb = read_cb;
    event->write_cb = write_cb;
    event->close_cb = close_cb;

  logger(LOG_DEBUG, "event_create()", "created event");

  return event;
}

void xps_loop_run(xps_loop_t *loop) {
	/* Validate params */
	assert(loop != NULL);

    while (1) {
      logger(LOG_DEBUG, "xps_loop_run()", "epoll wait");
      int n_events = epoll_wait(loop->epoll_fd,loop->epoll_events, MAX_EPOLL_EVENTS, -1);
      logger(LOG_DEBUG, "xps_loop_run()", "epoll wait over");

      logger(LOG_DEBUG, "xps_loop_run()", "handling %d events", n_events);

      // Handle events
      for (int i = 0; i < n_events; i++) {
        logger(LOG_DEBUG, "xps_loop_run()", "handling event no. %d", i + 1);

        struct epoll_event curr_epoll_event = loop->epoll_events[i];
        loop_event_t *curr_event = curr_epoll_event.data.ptr;

        // Check if event still exists. Could have been destroyed due to prev event
        int curr_event_idx = -1;
		for(int i = 0; i < loop->events.length; i++) {
			if(loop->events.data[i] == curr_event) {
				curr_event_idx = i;
				break;
			}
		}

        // 🟡 Above can be optimized using an RB tree
        if (curr_event_idx == -1) {
          logger(LOG_DEBUG, "handle_epoll_events()", "event not found. skipping");
          continue;
        }

        // Close Event
        if(curr_epoll_event.events & (EPOLLERR | EPOLLHUP)) {
          logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / close");
          if (curr_event->close_cb != NULL) {
            curr_event->close_cb(curr_event->ptr);
          }
        }

        // Read event
        if (curr_epoll_event.events & EPOLLIN) {
          logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / read");
          if (curr_event->read_cb != NULL)
            // Pass the ptr from loop_event_t as a parameter to the callback
            curr_event->read_cb(curr_event->ptr);
        }

        // Write event
        if(curr_epoll_event.events & EPOLLOUT) {
          logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / write");
          if (curr_event->write_cb != NULL) {
            curr_event->write_cb(curr_event->ptr);
          }
        }

      }
    }
}


/**
 * Creates a new event loop instance associated with the given core.
 *
 * This function creates an epoll file descriptor, allocates memory for the xps_loop instance,
 * and initializes its values.
 *
 * @param core : The core instance to which the loop belongs
 * @return A pointer to the newly created loop instance, or NULL on failure.
 */
xps_loop_t *xps_loop_create(xps_core_t *core) {
  assert(core != NULL);

  // Alloc memory for 'loop' instance
  xps_loop_t *loop = malloc(sizeof(xps_loop_t));
  if (loop == NULL) {
    logger(LOG_ERROR, "xps_loop_create()", "malloc() failed for 'loop'");
    return NULL;
  }

  // Create epoll file descriptor
  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
      logger(LOG_ERROR, "xps_loop_create()", "epoll_create1() failed");
      return NULL;
  }

  /* fill this */
  loop->core = core;
  loop->epoll_fd = epoll_fd;
  loop->n_null_events = 0;

  vec_init(&loop->events);

  logger(LOG_INFO, "xps_loop_create()", "created loop");

  return loop;

}

/**
 * Destroys the given loop instance and releases associated resources.
 *
 * This function destroys all loop_event_t instances present in loop->events list,
 * closes the epoll file descriptor and releases memory allocated for the loop instance,
 *
 * @param loop The loop instance to be destroyed.
 */
void xps_loop_destroy(xps_loop_t *loop) {
  assert(loop != NULL);

  logger(LOG_DEBUG, "xps_loop_destroy()", "destroying loop: %p, core: %p, epoll_fd: %d", loop, loop->core, loop->epoll_fd);

  // Destroy all loop_event_t instances in loop->events list
  for (int i = 0; i < loop->events.length; i++) {
    loop_event_t *event = loop->events.data[i];
    
    if(event != NULL) {
      logger(LOG_DEBUG, "xps_loop_destroy()", "destroying event: %p, fd: %d", event, event->fd);
      free(event);
    }
  }

  // Close epoll file descriptor
  vec_deinit(&loop->events);
  logger(LOG_DEBUG, "xps_loop_destroy()", "closing epoll_fd: %d", loop->epoll_fd);
  close(loop->epoll_fd);
  logger(LOG_DEBUG, "xps_loop_destroy()", "destroyed loop: %p", loop);
  free(loop);

}

/**
 * Attaches a FD to be monitored using epoll
 *
 * The function creates an intance of loop_event_t and attaches it to epoll.
 * Add the pointer to loop_event_t to the events list in loop
 *
 * @param loop : loop to which FD should be attached
 * @param fd : FD to be attached to epoll
 * @param event_flags : epoll event flags
 * @param ptr : Pointer to instance of xps_listener_t or xps_connection_t
 * @param read_cb : Callback function to be called on a read event
 * @param write_cb : Callback function to be called on a write event
 * @param close_cb : Callback function to be called on a close event
 * @return : OK on success and E_FAIL on error
 */
int xps_loop_attach(xps_loop_t *loop, u_int fd, int event_flags, void *ptr, xps_handler_t read_cb, xps_handler_t write_cb, xps_handler_t close_cb) {
  assert(loop != NULL);
  assert(ptr != NULL);

  logger(LOG_DEBUG, "xps_loop_attach()", "attaching fd: %d to loop: %p", fd, loop);
  int flags = fcntl(fd, F_GETFD);

  if(flags == -1) {
    logger(LOG_ERROR, "xps_loop_attach()", "fcntl() failed for fd: %d", fd);
    return E_FAIL;
  }

  loop_event_t *event = loop_event_create(fd, ptr, read_cb, write_cb, close_cb);

  if(event == NULL) {
    logger(LOG_ERROR, "xps_loop_attach()", "loop_event_create() failed for fd: %d", fd);
    return E_FAIL;
  }

  struct epoll_event epoll_event ;

  epoll_event.events = event_flags ;
  epoll_event.data.ptr = event ;

  if(epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &epoll_event) == -1) {
    logger(LOG_ERROR, "xps_loop_attach()", "epoll_ctl() failed for fd: %d", fd);
    loop_event_destroy(event);
    return E_FAIL;
  }


  vec_push(&loop->events, event);
  logger(LOG_DEBUG, "xps_loop_attach()", "attached event: %p to loop: %p", event, loop);

  return OK;

}

/**
 * Remove FD from epoll
 *
 * Find the instance of loop_event_t from loop->events that matches fd param
 * and detach FD from epoll. Destroy the loop_event_t instance and set the pointer
 * to NULL in loop->events list. Increment loop->n_null_events.
 *
 * @param loop : loop instnace from which to detach fd
 * @param fd : FD to be detached
 * @return : OK on success and E_FAIL on error
 */
int xps_loop_detach(xps_loop_t *loop, u_int fd) {
  assert(loop != NULL);

  for(int i = 0; i < loop->events.length; i++) {
    loop_event_t *event = loop->events.data[i];

    if(event != NULL && event->fd == fd) {
      logger(LOG_DEBUG, "xps_loop_detach()", "detaching event: %p from loop: %p", event, loop);

      if(epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        logger(LOG_ERROR, "xps_loop_detach()", "epoll_ctl() failed for fd: %d", fd);
        return E_FAIL;
      }

      loop_event_destroy(event);
      loop->events.data[i] = NULL;
      loop->n_null_events++;

      logger(LOG_DEBUG, "xps_loop_detach()", "detached event: %p from loop: %p", event, loop);
      return OK;
    }
  }

  return E_FAIL; ;
}

void loop_event_destroy(loop_event_t *event) {
  assert(event != NULL);

  free(event);

  logger(LOG_DEBUG, "event_destroy()", "destroyed event");
}