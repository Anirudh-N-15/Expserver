#include "xps.h"

xps_core_t *core ;

void sigint_handler(int signum) {
	logger(LOG_INFO, "sigint_handler()", "SIGINT received, shutting down server");

	xps_core_destroy(core);

	exit(EXIT_SUCCESS);
}


int main() {
    signal(SIGINT, sigint_handler);

    core = xps_core_create();
    if (core == NULL) {
      logger(LOG_ERROR, "main()", "xps_core_create() failed");
      return E_FAIL; 
    }

	xps_core_start(core);
}


