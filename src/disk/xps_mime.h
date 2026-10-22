#ifndef XPS_MIME_H
#define XPS_MIME_H

#include "../xps.h"

const char *xps_get_mime(const char *file_path);

struct xps_keyval_s {
    const char *key;
    const char *val;
};

typedef struct xps_keyval_s xps_keyval_t;

#endif