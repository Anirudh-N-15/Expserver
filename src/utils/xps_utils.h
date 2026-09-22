#ifndef XPS_UTILS_H
#define XPS_UTILS_H

#include "../xps.h"

// Sockets
bool is_valid_port(u_int port);
int make_socket_non_blocking(u_int sock_fd);
struct addrinfo *xps_getaddrinfo(const char *host, u_int port);
char *get_remote_ip(u_int sock_fd);

struct addrinfo {
  int              ai_flags;
  int              ai_family;
  int              ai_socktype;
  int              ai_protocol;
  socklen_t        ai_addrlen;
  struct sockaddr *ai_addr;
  char            *ai_canonname;
  struct addrinfo *ai_next;
};

// Other functions

#endif