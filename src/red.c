/**********************************************************/
/*           Programación para mecatrónicos               */
/*  Nombre:    Layla Garcia                               */
/*  Matricula: 2026-0288                                  */
/*  Seccion:   Miercoles                                  */
/*  Practica:  Practica #1                                 */
/*  Fecha:     13/10/2026                                 */                       
/* Link Practica: https://github.com/LMiche-coding/Estacion-Practica-1 */
/**********************************************************/
/*****************************************************************************//*!
   *
   * @brief funcion principal. 
   *        
   * @param[in] recive ninguno 
   *
   * @return devuelve un entero que puede ser utilizado como indicador de error
   *
   * @Pass/ Fail criteria:  none.
*****************************************************************************/

#include "red.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #include <windows.h>
  void pausa_ms(int ms) { Sleep((DWORD)ms); }
#else
  #include <unistd.h>
  void pausa_ms(int ms) { usleep((useconds_t)ms * 1000); }
#endif

int  red_iniciar(void) { return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1; }
void red_cerrar(void)  { curl_global_cleanup(); }
void buf_liberar(struct Buf *b) { free(b->d); b->d = NULL; b->n = 0; }

static size_t al_escribir(char *p, size_t s, size_t m, void *ud) {
    struct Buf *b = ud;
    size_t k = s * m;
    char *nd = realloc(b->d, b->n + k + 1);
    if (!nd) return 0;
    b->d = nd;
    memcpy(b->d + b->n, p, k);
    b->n += k;
    b->d[b->n] = '\0';
    return k;
}

static size_t al_descartar(char *p, size_t s, size_t m, void *ud) {
    (void)p; (void)ud;
    return s * m;
}

static CURLcode http_peticion(CURL *h, const char *url, const char *token, const char *json,
                              struct Buf *out, long *http, long *retry) {
    *http = 0;
    *retry = -1;
    curl_easy_reset(h);                                   /* reutiliza el handle */
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_HTTPAUTH, (long)CURLAUTH_BEARER);
    curl_easy_setopt(h, CURLOPT_XOAUTH2_BEARER, token);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "estacion-itla/1.0");
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, al_escribir);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, out);

    struct curl_slist *hdr = NULL;
    if (json) {
        hdr = curl_slist_append(hdr, "Content-Type: application/json");
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, json);
    }

    CURLcode rc = curl_easy_perform(h);
    if (rc == CURLE_OK) {
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, http);
        curl_off_t ra = 0;                                /* requiere libcurl >= 7.66 */
        if (curl_easy_getinfo(h, CURLINFO_RETRY_AFTER, &ra) == CURLE_OK && ra > 0)
            *retry = (long)ra;
    }
    curl_slist_free_all(hdr);
    return rc;
}

static CURLcode con_ruta(CURL *h, const struct Conf *c, const char *ruta, const char *json,
                         struct Buf *out, long *http, long *retry) {
    char url[256];
    int w = snprintf(url, sizeof url, "http://%s:4000%s", c->host, ruta);
    if (w < 0 || (size_t)w >= sizeof url) { *http = 0; *retry = -1; return CURLE_URL_MALFORMAT; }
    return http_peticion(h, url, c->token, json, out, http, retry);
}

CURLcode red_get(CURL *h, const struct Conf *c, const char *ruta,
                 struct Buf *out, long *http, long *retry) {
    return con_ruta(h, c, ruta, NULL, out, http, retry);
}

CURLcode red_post(CURL *h, const struct Conf *c, const char *ruta, const char *json,
                  struct Buf *out, long *http, long *retry) {
    return con_ruta(h, c, ruta, json, out, http, retry);
}

/* ---------- MQTT: publicar el latido (QoS 0, si falla se pierde) ---------- */

CURLcode mqtt_publicar(CURL *h, const struct Conf *c, const char *texto) {
    char url[256];
    int w = snprintf(url, sizeof url, "mqtt://%s:1883/itla/estudiantes/%s/latido", c->host, c->mat);
    if (w < 0 || (size_t)w >= sizeof url) return CURLE_URL_MALFORMAT;

    curl_easy_reset(h);
    curl_easy_setopt(h, CURLOPT_URL, url);
    curl_easy_setopt(h, CURLOPT_USERNAME, c->mat);
    curl_easy_setopt(h, CURLOPT_PASSWORD, c->token);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, texto);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "estacion-itla/1.0");
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, al_descartar);
    return curl_easy_perform(h);
}

/* ---------- MQTT: suscribirse ---------- */

struct Mqtt { char buf[4096]; size_t len; mqtt_cb cb; void *ud; };

static size_t al_recibir_mqtt(char *ptr, size_t size, size_t nmemb, void *ud) {
    size_t n = size * nmemb;
    struct Mqtt *m = ud;
    if (n > sizeof m->buf - m->len) {          /* no cabe: descarta lo acumulado */
        m->len = 0;
        if (n > sizeof m->buf) return n;
    }
    memcpy(m->buf + m->len, ptr, n);
    m->len += n;

    for (;;) {                                  /* procesa todos los mensajes completos */
        if (m->len < 2) break;
        size_t tlen = ((size_t)(unsigned char)m->buf[0] << 8) | (unsigned char)m->buf[1];
        if (m->len < 2 + tlen) break;
        char *carga = m->buf + 2 + tlen;
        char *fin = memchr(carga, '\n', m->len - 2 - tlen);
        if (!fin) break;                        /* el mensaje aun no llego entero */
        *fin = '\0';
        m->cb(m->buf + 2, tlen, carga, m->ud);
        size_t usado = (size_t)(fin - m->buf) + 1;
        memmove(m->buf, m->buf + usado, m->len - usado);
        m->len -= usado;
    }
    return n;                                   /* siempre size * nmemb */
}

static int al_progreso(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)a; (void)b; (void)c; (void)d;
    volatile sig_atomic_t *salir = ud;
    return *salir ? 1 : 0;                      /* 1 aborta perform */
}

void red_escuchar(const struct Conf *c, mqtt_cb cb, void *ud, volatile sig_atomic_t *salir) {
    char url[256];
    int w = snprintf(url, sizeof url, "mqtt://%s:1883/itla/planta/%s/alarma", c->host, c->mat);
    if (w < 0 || (size_t)w >= sizeof url) { fprintf(stderr, "URL demasiado larga\n"); return; }

    int espera = 1;
    while (!*salir) {
        struct Mqtt *m = calloc(1, sizeof *m);
        CURL *h = curl_easy_init();
        if (!m || !h) {
            fprintf(stderr, "Sin memoria\n");
            free(m);
            if (h) curl_easy_cleanup(h);
            return;
        }
        m->cb = cb;
        m->ud = ud;

        curl_easy_setopt(h, CURLOPT_URL, url);
        curl_easy_setopt(h, CURLOPT_USERNAME, c->mat);
        curl_easy_setopt(h, CURLOPT_PASSWORD, c->token);
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, al_recibir_mqtt);
        curl_easy_setopt(h, CURLOPT_WRITEDATA, m);
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, al_progreso);
        curl_easy_setopt(h, CURLOPT_XFERINFODATA, (void *)salir);

        time_t ini = time(NULL);
        CURLcode rc = curl_easy_perform(h);     /* bloquea mientras viva la conexion */
        curl_easy_cleanup(h);
        free(m);
        if (*salir) break;

        fprintf(stderr, "Suscripcion cerrada (%s). Reconectando en %d s...\n",
                curl_easy_strerror(rc), espera);
        if (time(NULL) - ini > 30) espera = 1;  /* la conexion duro: reinicia la espera */
        for (int i = 0; i < espera * 10 && !*salir; i++) pausa_ms(100);
        espera = espera * 2 > 30 ? 30 : espera * 2;
    }
}