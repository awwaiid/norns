#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nanomsg/nn.h>
#include <nanomsg/reqrep.h>

#include "sidecar.h"

//---------------------------------
//--- common

const char *url = "ipc:///tmp/norns-sidecar.ipc";

//---------------------------------
//--- server

const size_t CMD_CAPTURE_BYTES = 8192 * 8;
const size_t CMD_LINE_BYTES = 1024;

// allocates and returns a string buffer
static int sidecar_server_run_cmd(char **result, const char *cmd, size_t *sz) {
  const size_t CMD_LINE_CHARS = CMD_LINE_BYTES / sizeof(char);
  FILE *f = popen((char *)cmd, "r");
  if (f == NULL) {
    fprintf(stderr, "popen() failed\n");
    return -1;
  }
  // FIXME: would be more efficition to allocate incrementally in chunks
  // instead of getting a huge blob up front and resizing down
  char *buf = (char*)malloc(CMD_CAPTURE_BYTES);
  buf[0] = '\0';
  size_t nb = fread(buf, 1, CMD_CAPTURE_BYTES - 1, f);
  printf("captured %d bytes\n", nb);
  buf[nb] = '\0';
  buf = (char*)realloc(buf, nb);
  *result = buf;
  *sz = nb;
  return 0;
}

int sidecar_server_main() {
  int fd = nn_socket(AF_SP, NN_REP);
  if (fd < 0) {
    fprintf(stderr, "nn_socket: %s\n", nn_strerror(nn_errno()));
    return (-1);
  }

  if (nn_bind(fd, url) < 0) {
    fprintf(stderr, "nn_bind: %s\n", nn_strerror(nn_errno()));
    nn_close(fd);
    return (-1);
  }

  int nb;
  for (;;) {
    char *cmd = NULL;
    nb = nn_recv(fd, &cmd, NN_MSG, 0);
    if (nb < 0) {
      fprintf(stderr, "nn_recv: %s\n", nn_strerror(nn_errno()));
      return -1;
    }
    if (nb < 1) {
      fprintf(stderr, "empty command\n");
      return -1;
    }
    size_t sz;
    printf("sidecar received %d bytes\n", nb);
    printf("running cmd: %s\n", cmd);
    char *result;

    sidecar_server_run_cmd(&result, cmd, &sz);

    if (sz > 0) {
      nn_send(fd, result, sz, 0);
      free(result);
    }
    nn_freemsg(cmd);
  }
  return 0;
}

//---------------------------------
//--- client

struct client_state {
  int fd;
  char *buf;
};

static struct client_state cs;

int sidecar_client_init() {
  cs.fd = nn_socket(AF_SP, NN_REQ);
  if (cs.fd < 0) {
    fprintf(stderr, "nn_socket (sidecar client): %s\n",
            nn_strerror(nn_errno()));
    return (-1);
  }

  int res = nn_connect(cs.fd, url);
  if (res < 0) {
    fprintf(stderr, "nn_connect (sidecar client): %s\n",
            nn_strerror(nn_errno()));
    return (-1);
  }
}

void sidecar_client_cmd(char **result, size_t *size, const char *cmd) {
  size_t sz = nn_send(cs.fd, cmd, strlen(cmd) + 1, 0);
  if (sz < 0) {
    fprintf(stderr, "nn_send (sidecar client): %s\n", nn_strerror(nn_errno()));
    return;
  }
  fprintf(stderr, "receiving reply...\n");
  sz = nn_recv(cs.fd, &cs.buf, NN_MSG, 0);
  if (sz < 0) {
    fprintf(stderr, "nn_recv (sidecar client): %s\n", nn_strerror(nn_errno()));
    return;
  }
  if (sz > 0) {
    printf("main rx; bytes=%d, txt = \n%s\n", sz, cs.buf);
    nn_freemsg(cs.buf);
  } else {
    fprintf(stderr, "received empty result\n");
    return;
  }
}