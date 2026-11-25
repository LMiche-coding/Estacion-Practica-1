#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "datos.h"
#include "red.h"
#include "sensor.h"

#define DB_RUTA "estacion.db"

static struct Conf C;
static volatile sig_atomic_t salir = 0;

static void al_sigint(int s) { (void)s; salir = 1; }

static int copiar(char *dst, size_t n, const char *src) {
    int w = snprintf(dst, n, "%s", src);
    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

static int cargar_entorno(void) {
    const char *h = getenv("ITLA_HOST");
    const char *m = getenv("ITLA_MATRICULA");
    const char *t = getenv("ITLA_TOKEN");
    if (!h || !m || !t) {
        fprintf(stderr, "Faltan variables: ITLA_HOST, ITLA_MATRICULA, ITLA_TOKEN\n");
        return -1;
    }
    if (copiar(C.host, sizeof C.host, h) || copiar(C.mat, sizeof C.mat, m) ||
        copiar(C.token, sizeof C.token, t)) {
        fprintf(stderr, "Alguna variable de entorno es demasiado larga\n");
        return -1;
    }
    if (strlen(C.token) != 32)
        fprintf(stderr, "Aviso: el token tiene %zu caracteres (deben ser 32; revisa espacios o saltos)\n",
                strlen(C.token));
    return 0;
}

/* Devuelve la siguiente linea de *p (modifica el texto) o NULL si no hay mas. */
static char *siguiente_linea(char **p) {
    if (!*p || !**p) return NULL;
    char *l = *p;
    char *nl = strchr(l, '\n');
    if (nl) { *nl = '\0'; *p = nl + 1; } else *p = NULL;
    size_t k = strlen(l);
    if (k && l[k - 1] == '\r') l[k - 1] = '\0';
    return l;
}

static int respuesta_ok(CURLcode rc, long http, const char *que) {
    if (rc != CURLE_OK) { fprintf(stderr, "%s: error de red: %s\n", que, curl_easy_strerror(rc)); return 0; }
    if (http != 200)    { fprintf(stderr, "%s: HTTP %ld\n", que, http); return 0; }
    return 1;
}

/* ================= Nivel 0 ================= */

static int cmd_config(sqlite3 *db) {
    CURL *h = curl_easy_init();
    if (!h) return 1;
    struct Buf b = {0};
    long http, retry;
    CURLcode rc = red_get(h, &C, "/api/v1/config", &b, &http, &retry);
    curl_easy_cleanup(h);
    if (!respuesta_ok(rc, http, "config") || !b.d) { buf_liberar(&b); return 1; }

    char *p = b.d, *l;
    while ((l = siguiente_linea(&p)) != NULL) {
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = '\0';
        if (cfg_guardar(db, l, eq + 1) != 0) fprintf(stderr, "No se pudo guardar %s\n", l);
    }
    buf_liberar(&b);

    char v[64];
    if (cfg_leer(db, "umbral_temp", v, sizeof v) == 0)  printf("umbral_temp = %s\n", v);
    if (cfg_leer(db, "intervalo_s", v, sizeof v) == 0)  printf("intervalo_s = %s\n", v);
    if (cfg_leer(db, "hora", v, sizeof v) == 0)         printf("hora del servidor = %s\n", v);
    return 0;
}

/* ================= Nivel 1 ================= */

static int cmd_descargar(sqlite3 *db) {
    sqlite3_stmt *ins = lectura_preparar(db);
    CURL *h = curl_easy_init();
    if (!ins || !h) { if (ins) sqlite3_finalize(ins); if (h) curl_easy_cleanup(h); return 1; }
    if (db_begin(db) != 0) { sqlite3_finalize(ins); curl_easy_cleanup(h); return 1; }

    long desde = 0;
    int ok = 1;
    while (ok && !salir) {
        char ruta[96];
        snprintf(ruta, sizeof ruta, "/api/v1/lecturas?desde_id=%ld&limite=200", desde);
        struct Buf b = {0};
        long http, retry;
        CURLcode rc = red_get(h, &C, ruta, &b, &http, &retry);

        if (rc == CURLE_OK && (http == 429 || http == 503)) {   /* espera y repite la pagina */
            buf_liberar(&b);
            pausa_ms(retry > 0 ? (int)retry * 1000 : 1000);
            continue;
        }
        if (!respuesta_ok(rc, http, "lecturas") || !b.d) { buf_liberar(&b); ok = 0; break; }

        int filas = 0;
        long maximo = desde;
        char *p = b.d, *l;
        while ((l = siguiente_linea(&p)) != NULL) {
            if (strncmp(l, "id,", 3) == 0 || !*l) continue;     /* cabecera */
            long id = lectura_insertar_csv(ins, l);
            if (id == -2) { ok = 0; break; }
            if (id < 0) continue;
            filas++;
            if (id > maximo) maximo = id;
        }
        buf_liberar(&b);
        printf("pagina desde_id=%ld: %d filas\n", desde, filas);
        if (!ok || filas < 200) break;
        if (maximo <= desde) { fprintf(stderr, "La paginacion no avanza\n"); ok = 0; break; }
        desde = maximo;
        pausa_ms(250);                                          /* no pasar de 5 peticiones/s */
    }
    sqlite3_finalize(ins);

    if (!ok) { db_rollback(db); curl_easy_cleanup(h); return 1; }
    if (db_commit(db) != 0) { curl_easy_cleanup(h); return 1; }

    long filas, suma;
    if (descarga_totales(db, &filas, &suma) != 0) { curl_easy_cleanup(h); return 1; }
    printf("filas_unicas=%ld suma_ids=%ld\n", filas, suma);

    char json[96];
    snprintf(json, sizeof json, "{\"filas_unicas\":%ld,\"suma_ids\":%ld}", filas, suma);
    struct Buf b = {0};
    long http, retry;
    CURLcode rc = red_post(h, &C, "/api/v1/respuestas/descarga", json, &b, &http, &retry);
    curl_easy_cleanup(h);
    if (respuesta_ok(rc, http, "descarga") && b.d) printf("%s\n", b.d);
    buf_liberar(&b);
    return 0;
}

/* ================= Nivel 2 ================= */

static int cmd_analizar(sqlite3 *db) {
    char v[64];
    if (cfg_leer(db, "umbral_temp", v, sizeof v) != 0) {
        fprintf(stderr, "Falta umbral_temp: ejecuta primero ./estacion config\n");
        return 1;
    }
    char json[4096];
    if (analisis_json(db, atof(v), json, sizeof json) != 0) {
        fprintf(stderr, "No se pudo generar el analisis\n");
        return 1;
    }
    printf("Enviando: %s\n", json);

    CURL *h = curl_easy_init();
    if (!h) return 1;
    struct Buf b = {0};
    long http, retry;
    CURLcode rc = red_post(h, &C, "/api/v1/respuestas/analisis", json, &b, &http, &retry);
    curl_easy_cleanup(h);
    if (rc != CURLE_OK) fprintf(stderr, "analisis: %s\n", curl_easy_strerror(rc));
    else printf("HTTP %ld\n%s\n", http, b.d ? b.d : "");
    buf_liberar(&b);
    return rc == CURLE_OK ? 0 : 1;
}

/* ================= Niveles 3 y 4 ================= */

struct Envio { sqlite3 *db; CURL *h; time_t no_enviar; int espera; };

static void tick_cola(struct Envio *e) {
    while (!salir && time(NULL) >= e->no_enviar) {
        struct Pendiente p;
        if (tel_obtener_pendiente(e->db, &p) != 1) return;

        char json[160];
        snprintf(json, sizeof json, "{\"seq\":%ld,\"ts\":%ld,\"temp\":%.2f,\"corriente\":%.2f}",
                 p.seq, p.ts, p.temp, p.corriente);
        struct Buf b = {0};
        long http, retry;
        CURLcode rc = red_post(e->h, &C, "/api/v1/telemetria", json, &b, &http, &retry);
        buf_liberar(&b);
        tel_sumar_intento(e->db, p.seq);

        if (rc != CURLE_OK) {                       /* fallo de red: 1, 2, 4 ... 30 s */
            fprintf(stderr, "seq %ld: %s (reintento en %d s)\n", p.seq, curl_easy_strerror(rc), e->espera);
            e->no_enviar = time(NULL) + e->espera;
            e->espera = e->espera * 2 > 30 ? 30 : e->espera * 2;
            return;
        }
        if (http == 201 || http == 200) {
            if (tel_marcar_enviado(e->db, p.seq) != 0) return;
            e->espera = 1;
            pausa_ms(250);                          /* evita 429 al vaciar la cola */
            continue;
        }
        if (http == 503) {                          /* respeta Retry-After (+1 s de margen) */
            long w = retry > 0 ? retry : 5;
            fprintf(stderr, "503: servidor caido, espero %ld s\n", w + 1);
            e->no_enviar = time(NULL) + w + 1;
            return;
        }
        fprintf(stderr, "seq %ld: HTTP %ld\n", p.seq, http);
        e->no_enviar = time(NULL) + (http == 429 ? 1 : e->espera);
        return;
    }
}

static int cmd_enviar(sqlite3 *db) {
    char v[32];
    int intervalo = 5, latido = 10;
    if (cfg_leer(db, "intervalo_s", v, sizeof v) == 0 && atoi(v) > 0) intervalo = atoi(v);
    if (cfg_leer(db, "latido_s", v, sizeof v) == 0 && atoi(v) > 0)    latido = atoi(v);

    struct Envio e = { db, curl_easy_init(), 0, 1 };
    CURL *hm = curl_easy_init();
    if (!e.h || !hm) { fprintf(stderr, "curl_easy_init fallo\n"); return 1; }

    long seq = tel_siguiente_seq(db);
    if (seq < 1) return 1;
    time_t prox_lect = time(NULL), prox_lat = time(NULL);
    printf("Enviando desde seq=%ld cada %d s (Ctrl+C para salir)\n", seq, intervalo);

    while (!salir) {
        time_t ahora = time(NULL);

        if (ahora >= prox_lect) {
            double t, c;
            sensor_leer(&t, &c);
            if (tel_insertar(db, seq, (long)ahora, t, c) == 0) {
                printf("lectura seq=%ld temp=%.2f corriente=%.2f\n", seq, t, c);
                seq++;
            }
            prox_lect += intervalo;
        }
        if (ahora >= prox_lat) {                    /* latido: si falla se pierde, no se encola */
            char msg[96];
            snprintf(msg, sizeof msg, "LATIDO;%ld;%d;%ld", seq - 1, tel_pendientes(db), (long)ahora);
            CURLcode rc = mqtt_publicar(hm, &C, msg);
            if (rc != CURLE_OK) fprintf(stderr, "latido perdido: %s\n", curl_easy_strerror(rc));
            prox_lat += latido;
        }
        tick_cola(&e);
        pausa_ms(200);
    }
    curl_easy_cleanup(e.h);
    curl_easy_cleanup(hm);
    printf("Salida limpia. Pendientes: %d\n", tel_pendientes(db));
    return 0;
}

/* ================= Nivel 5 ================= */

struct Esc { sqlite3 *db; CURL *h_ack; };

static int codigo_valido(const char *s) {
    if (!*s) return 0;
    for (; *s; s++) if (!isalnum((unsigned char)*s)) return 0;
    return 1;
}

static void al_mensaje(const char *topico, size_t tlen, const char *texto, void *ud) {
    (void)topico; (void)tlen;
    struct Esc *e = ud;
    if (strncmp(texto, "ALARMA;", 7) != 0) return;      /* ignora NINGUNA;<ts> y lo demas */

    char copia[100];
    snprintf(copia, sizeof copia, "%s", texto + 7);
    char *cod = copia;
    char *maq = strchr(cod, ';');  if (!maq) return;  *maq++ = '\0';
    char *ts  = strchr(maq, ';');  if (!ts)  return;  *ts++ = '\0';
    if (!codigo_valido(cod)) return;

    if (alarma_insertar(e->db, cod, maq, atol(ts), (long)time(NULL)) < 0) return;
    if (alarma_ya_confirmada(e->db, cod)) return;       /* repeticion del mensaje retenido */

    char json[128];
    snprintf(json, sizeof json, "{\"codigo\":\"%s\"}", cod);
    struct Buf b = {0};
    long http, retry;
    CURLcode rc = red_post(e->h_ack, &C, "/api/v1/alarmas/ack", json, &b, &http, &retry);
    if (rc == CURLE_OK && (http == 200 || http == 409)) {
        alarma_marcar_ack(e->db, cod);
        printf("alarma %s (%s) confirmada: %s\n", cod, maq, b.d ? b.d : "");
    } else {
        fprintf(stderr, "ack %s fallo: %s HTTP %ld\n", cod, curl_easy_strerror(rc), http);
    }
    buf_liberar(&b);
}

static int cmd_escuchar(sqlite3 *db) {
    struct Esc e = { db, curl_easy_init() };            /* handle distinto al de la suscripcion */
    if (!e.h_ack) return 1;
    printf("Escuchando alarmas (Ctrl+C para salir)\n");
    red_escuchar(&C, al_mensaje, &e, &salir);
    curl_easy_cleanup(e.h_ack);
    return 0;
}

/* ================= main ================= */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s config|descargar|analizar|enviar|escuchar\n", argv[0]);
        return 1;
    }
    if (cargar_entorno() != 0) return 1;
    srand((unsigned)time(NULL));
    signal(SIGINT, al_sigint);

    if (red_iniciar() != 0) { fprintf(stderr, "curl_global_init fallo\n"); return 1; }
    sqlite3 *db = db_abrir(DB_RUTA);
    if (!db) { red_cerrar(); return 1; }

    int r = 1;
    if      (strcmp(argv[1], "config") == 0)    r = cmd_config(db);
    else if (strcmp(argv[1], "descargar") == 0) r = cmd_descargar(db);
    else if (strcmp(argv[1], "analizar") == 0)  r = cmd_analizar(db);
    else if (strcmp(argv[1], "enviar") == 0)    r = cmd_enviar(db);
    else if (strcmp(argv[1], "escuchar") == 0)  r = cmd_escuchar(db);
    else fprintf(stderr, "Subcomando desconocido: %s\n", argv[1]);

    sqlite3_close(db);
    red_cerrar();
    return r;
}