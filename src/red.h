#ifndef RED_H
#define RED_H
#include <signal.h>
#include <stddef.h>
#include <curl/curl.h>

struct Conf { char host[128]; char mat[32]; char token[64]; };
struct Buf  { char *d; size_t n; };

typedef void (*mqtt_cb)(const char *topico, size_t tlen, const char *texto, void *ud);

void pausa_ms(int ms);
int  red_iniciar(void);
void red_cerrar(void);
void buf_liberar(struct Buf *b);

/* Devuelven el CURLcode; el codigo HTTP en *http; Retry-After en *retry (-1 si no hay) */
CURLcode red_get(CURL *h, const struct Conf *c, const char *ruta,
                 struct Buf *out, long *http, long *retry);
CURLcode red_post(CURL *h, const struct Conf *c, const char *ruta, const char *json,
                  struct Buf *out, long *http, long *retry);

CURLcode mqtt_publicar(CURL *h, const struct Conf *c, const char *texto);
void red_escuchar(const struct Conf *c, mqtt_cb cb, void *ud, volatile sig_atomic_t *salir);
#endif