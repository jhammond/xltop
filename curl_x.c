#include <stdio.h>
#include <stdlib.h>
#include "curl_x.h"
#include "n_buf.h"
#include "string1.h"
#include "trace.h"

int curl_x_init(struct curl_x *cx, const char *host, const char *port)
{
  int rc = -1;

  memset(cx, 0, sizeof(*cx));

  cx->cx_curl = curl_easy_init();
  if (cx->cx_curl == NULL) {
    if (errno == 0)
      errno = ENOMEM;
    goto out;
  }

  cx->cx_host = strdup(host);
  if (host == NULL)
    goto out;

  cx->cx_port = strtol(port, NULL, 0);
  if (!(0 <= cx->cx_port && cx->cx_port < 65536)) {
    errno = EINVAL;
    goto out;
  }

  rc = 0;
 out:
  if (rc < 0) {
    if (cx->cx_curl != NULL)
      curl_easy_cleanup(cx->cx_curl);
    free(cx->cx_host);
  }

  return rc;
}

int curl_x_get_url(struct curl_x *cx, char *url, struct n_buf *nb)
{
  FILE *file = NULL;
  int rc = -1;

  n_buf_destroy(nb);

  file = open_memstream(&nb->nb_buf, &nb->nb_size);
  if (file == NULL) {
    ERROR("cannot open memory stream: %m\n");
    goto out;
  }

  curl_easy_reset(cx->cx_curl);
  curl_easy_setopt(cx->cx_curl, CURLOPT_URL, url);
  if (cx->cx_port > 0)
    curl_easy_setopt(cx->cx_curl, CURLOPT_PORT, cx->cx_port);
  curl_easy_setopt(cx->cx_curl, CURLOPT_UPLOAD, 0L);
  curl_easy_setopt(cx->cx_curl, CURLOPT_WRITEDATA, file);

#if DEBUG
  curl_easy_setopt(cx->cx_curl, CURLOPT_VERBOSE, 1L);
#endif

  int curl_rc = curl_easy_perform(cx->cx_curl);
  if (curl_rc != 0) {
    ERROR("cannot GET `%s': %s\n", url, curl_easy_strerror(rc));
    /* Reset curl... */
    goto out;
  }

  if (ferror(file)) {
    ERROR("error writing to memory stream: %m\n");
    goto out;
  }

  rc = 0;

 out:
  if (file != NULL)
    fclose(file);

  nb->nb_end = nb->nb_size;

  return rc;
}

int curl_x_get(struct curl_x *cx, const char *path, const char *qstr,
               struct n_buf *nb)
{
  char *url = NULL;
  int rc = -1;

  url = strf("http://%s/%s%s%s", cx->cx_host, path,
             qstr != NULL ? "?" : "", qstr != NULL ? qstr : "");
  if (url == NULL)
    OOM();

  TRACE("url `%s'\n", url);

  if (curl_x_get_url(cx, url, nb) < 0)
    goto out;

  rc = 0;

 out:
  free(url);

  return rc;
}

int curl_x_get_iter(struct curl_x *cx,
                    const char *path, const char *query,
                    msg_cb_t *cb, void *data)
{
  N_BUF(nb);
  char *msg;
  size_t msg_len;
  int rc = -1;

  if (curl_x_get(cx, path, query, &nb) < 0)
    goto out;

  while (n_buf_get_msg(&nb, &msg, &msg_len) == 0) {
    rc = (*cb)(data, msg, msg_len);
    if (rc < 0)
      goto out;
  }

 out:
  n_buf_destroy(&nb);

  return rc;
}
