/**********************************************************/
/*           Programación para mecatrónicos               */
/*  Nombre:    Layla Garcia                               */
/*  Matricula: 2026-0288                                  */
/*  Seccion:   Miercoles                                  */
/*  Practica:  Practica 1                                */
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

#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include "datos.h"
#include <time.h>


#define DB_FILE "estacion.db"

static int ejecutar(sqlite3 *db, const char *sql) {   /* solo SQL fijo, sin datos variables */
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "SQL: %s\n", err ? err : "error");
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

static sqlite3_stmt *preparar(sqlite3 *db, const char *sql) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "prepare: %s\n", sqlite3_errmsg(db));
        return NULL;
    }
    return st;
}

sqlite3 *db_abrir(const char *ruta) {
    sqlite3 *db = NULL;
    if (sqlite3_open(ruta, &db) != SQLITE_OK) {
        fprintf(stderr, "No se pudo abrir %s: %s\n", ruta, sqlite3_errmsg(db));
        sqlite3_close(db);
        return NULL;
    }
    sqlite3_busy_timeout(db, 2000);
    if (ejecutar(db, "PRAGMA journal_mode=WAL;") != 0 ||
        ejecutar(db,
            "CREATE TABLE IF NOT EXISTS config(clave TEXT PRIMARY KEY, valor TEXT);"
            "CREATE TABLE IF NOT EXISTS lecturas_planta(id INTEGER PRIMARY KEY,"
            " maquina TEXT, ts INTEGER, temp REAL, corriente REAL, vibracion REAL, estado TEXT);"
            "CREATE TABLE IF NOT EXISTS telemetria(seq INTEGER PRIMARY KEY, ts INTEGER,"
            " temp REAL, corriente REAL, enviado INTEGER NOT NULL DEFAULT 0,"
            " intentos INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS alarmas(codigo TEXT PRIMARY KEY, maquina TEXT,"
            " ts_servidor INTEGER, ts_recibida INTEGER, ts_ack INTEGER);") != 0) {
        sqlite3_close(db);
        return NULL;
    }
    return db;
}

int db_begin(sqlite3 *db)    { return ejecutar(db, "BEGIN;"); }
int db_commit(sqlite3 *db)   { return ejecutar(db, "COMMIT;"); }
int db_rollback(sqlite3 *db) { return ejecutar(db, "ROLLBACK;"); }

int cfg_guardar(sqlite3 *db, const char *clave, const char *valor) {
    sqlite3_stmt *st = preparar(db, "INSERT OR REPLACE INTO config(clave, valor) VALUES(?, ?)");
    if (!st) return -1;
    sqlite3_bind_text(st, 1, clave, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, valor, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int cfg_leer(sqlite3 *db, const char *clave, char *out, size_t n) {
    sqlite3_stmt *st = preparar(db, "SELECT valor FROM config WHERE clave = ?");
    if (!st) return -1;
    sqlite3_bind_text(st, 1, clave, -1, SQLITE_STATIC);
    int r = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *v = sqlite3_column_text(st, 0);
        snprintf(out, n, "%s", v ? (const char *)v : "");
        r = 0;
    }
    sqlite3_finalize(st);
    return r;
}

/* ---------- nivel 1: descarga ---------- */

sqlite3_stmt *lectura_preparar(sqlite3 *db) {
    return preparar(db,
        "INSERT OR IGNORE INTO lecturas_planta"
        "(id, maquina, ts, temp, corriente, vibracion, estado) VALUES(?,?,?,?,?,?,?)");
}

/* Parte la linea por comas SIN saltarse los campos vacios (strtok si lo haria). */
static int partir(char *linea, char *campos[], int max) {
    int n = 0;
    char *p = linea;
    while (n < max) {
        campos[n++] = p;
        char *c = strchr(p, ',');
        if (!c) break;
        *c = '\0';
        p = c + 1;
    }
    return n;
}

static void bind_texto(sqlite3_stmt *st, int i, const char *s) {
    if (*s) sqlite3_bind_text(st, i, s, -1, SQLITE_TRANSIENT);
    else    sqlite3_bind_null(st, i);
}

static void bind_entero(sqlite3_stmt *st, int i, const char *s) {
    char *fin;
    long long v = strtoll(s, &fin, 10);
    if (*s && *fin == '\0') sqlite3_bind_int64(st, i, v);
    else                    sqlite3_bind_null(st, i);
}

static void bind_real(sqlite3_stmt *st, int i, const char *s) {
    char *fin;
    double v = strtod(s, &fin);
    if (*s && *fin == '\0') sqlite3_bind_double(st, i, v);
    else                    sqlite3_bind_null(st, i);
}

long lectura_insertar_csv(sqlite3_stmt *st, char *linea) {
    size_t k = strlen(linea);
    while (k && (linea[k - 1] == '\n' || linea[k - 1] == '\r')) linea[--k] = '\0';

    char *c[7];
    if (partir(linea, c, 7) != 7) return -1;

    char *fin;
    long id = strtol(c[0], &fin, 10);
    if (fin == c[0] || *fin != '\0') return -1;          /* cabecera o basura */

    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    sqlite3_bind_int64(st, 1, id);
    bind_texto(st, 2, c[1]);
    bind_entero(st, 3, c[2]);
    bind_real(st, 4, c[3]);
    bind_real(st, 5, c[4]);
    bind_real(st, 6, c[5]);
    bind_texto(st, 7, c[6]);

    if (sqlite3_step(st) != SQLITE_DONE) {
        fprintf(stderr, "insert lectura %ld: %s\n", id, sqlite3_errmsg(sqlite3_db_handle(st)));
        return -2;
    }
    return id;
}

int descarga_totales(sqlite3 *db, long *filas, long *suma) {
    sqlite3_stmt *st = preparar(db, "SELECT COUNT(*), COALESCE(SUM(id),0) FROM lecturas_planta");
    if (!st) return -1;
    int r = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        *filas = (long)sqlite3_column_int64(st, 0);
        *suma  = (long)sqlite3_column_int64(st, 1);
        r = 0;
    }
    sqlite3_finalize(st);
    return r;
}

/* ---------- nivel 2: analisis en SQL ---------- */

int analisis_json(sqlite3 *db, double umbral, char *out, size_t n) {
    static const char *sql =
        "SELECT maquina, SUM(v), SUM(1-v),"
        " MIN(CASE WHEN v=1 THEN temp END),"
        " MAX(CASE WHEN v=1 THEN temp END),"
        " ROUND(AVG(CASE WHEN v=1 THEN temp END), 2),"
        " SUM(CASE WHEN v=1 AND temp > ? THEN 1 ELSE 0 END) "
        "FROM (SELECT maquina, temp,"
        "  COALESCE(estado = 'OK' AND temp IS NOT NULL AND temp <> -999.0"
        "   AND ts IS NOT NULL AND corriente IS NOT NULL"
        "   AND vibracion IS NOT NULL AND maquina IS NOT NULL, 0) AS v"
        " FROM lecturas_planta WHERE maquina IS NOT NULL) "
        "GROUP BY maquina ORDER BY maquina";

    sqlite3_stmt *st = preparar(db, sql);
    if (!st) return -1;
    sqlite3_bind_double(st, 1, umbral);

    int w = snprintf(out, n, "{\"maquinas\":[");
    if (w < 0 || (size_t)w >= n) { sqlite3_finalize(st); return -1; }
    size_t off = (size_t)w;
    int primero = 1, rc;

    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        w = snprintf(out + off, n - off,
            "%s{\"maquina\":\"%s\",\"validas\":%d,\"descartadas\":%d,"
            "\"temp_min\":%.2f,\"temp_max\":%.2f,\"temp_prom\":%.2f,\"sobre_umbral\":%d}",
            primero ? "" : ",",
            (const char *)sqlite3_column_text(st, 0),
            sqlite3_column_int(st, 1), sqlite3_column_int(st, 2),
            sqlite3_column_double(st, 3), sqlite3_column_double(st, 4),
            sqlite3_column_double(st, 5), sqlite3_column_int(st, 6));
        if (w < 0 || (size_t)w >= n - off) { sqlite3_finalize(st); return -1; }
        off += (size_t)w;
        primero = 0;
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return -1;

    w = snprintf(out + off, n - off, "]}");
    return (w < 0 || (size_t)w >= n - off) ? -1 : 0;
}

/* ---------- nivel 3: telemetria ---------- */

long tel_siguiente_seq(sqlite3 *db) {
    sqlite3_stmt *st = preparar(db, "SELECT COALESCE(MAX(seq),0)+1 FROM telemetria");
    if (!st) return -1;
    long r = -1;
    if (sqlite3_step(st) == SQLITE_ROW) r = (long)sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return r;
}

int tel_insertar(sqlite3 *db, long seq, long ts, double temp, double corr) {
    sqlite3_stmt *st = preparar(db,
        "INSERT INTO telemetria(seq, ts, temp, corriente, enviado, intentos) VALUES(?,?,?,?,0,0)");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, seq);
    sqlite3_bind_int64(st, 2, ts);
    sqlite3_bind_double(st, 3, temp);
    sqlite3_bind_double(st, 4, corr);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) fprintf(stderr, "insert telemetria: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int tel_pendientes(sqlite3 *db) {
    sqlite3_stmt *st = preparar(db, "SELECT COUNT(*) FROM telemetria WHERE enviado = 0");
    if (!st) return -1;
    int r = -1;
    if (sqlite3_step(st) == SQLITE_ROW) r = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return r;
}

int tel_obtener_pendiente(sqlite3 *db, struct Pendiente *p) {
    sqlite3_stmt *st = preparar(db,
        "SELECT seq, ts, temp, corriente FROM telemetria WHERE enviado = 0 ORDER BY seq LIMIT 1");
    if (!st) return -1;
    int r = 0, rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        p->seq = (long)sqlite3_column_int64(st, 0);
        p->ts = (long)sqlite3_column_int64(st, 1);
        p->temp = sqlite3_column_double(st, 2);
        p->corriente = sqlite3_column_double(st, 3);
        r = 1;
    } else if (rc != SQLITE_DONE) r = -1;
    sqlite3_finalize(st);
    return r;
}

int tel_marcar_enviado(sqlite3 *db, long seq) {
    sqlite3_stmt *st = preparar(db, "UPDATE telemetria SET enviado = 1 WHERE seq = ?");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, seq);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

int tel_sumar_intento(sqlite3 *db, long seq) {
    sqlite3_stmt *st = preparar(db, "UPDATE telemetria SET intentos = intentos + 1 WHERE seq = ?");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, seq);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

/* ---------- nivel 5: alarmas ---------- */

int alarma_insertar(sqlite3 *db, const char *cod, const char *maq, long ts_srv, long ts_rec) {
    sqlite3_stmt *st = preparar(db,
        "INSERT OR IGNORE INTO alarmas(codigo, maquina, ts_servidor, ts_recibida) VALUES(?,?,?,?)");
    if (!st) return -1;
    sqlite3_bind_text(st, 1, cod, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, maq, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, ts_srv);
    sqlite3_bind_int64(st, 4, ts_rec);
    int rc = sqlite3_step(st);
    int nueva = sqlite3_changes(db);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? nueva : -1;
}

int alarma_ya_confirmada(sqlite3 *db, const char *cod) {
    sqlite3_stmt *st = preparar(db, "SELECT ts_ack IS NOT NULL FROM alarmas WHERE codigo = ?");
    if (!st) return 0;
    sqlite3_bind_text(st, 1, cod, -1, SQLITE_STATIC);
    int r = 0;
    if (sqlite3_step(st) == SQLITE_ROW) r = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return r;
}

int alarma_marcar_ack(sqlite3 *db, const char *cod) {
    sqlite3_stmt *st = preparar(db, "UPDATE alarmas SET ts_ack = ? WHERE codigo = ?");
    if (!st) return -1;
    sqlite3_bind_int64(st, 1, (sqlite3_int64)time(NULL));
    sqlite3_bind_text(st, 2, cod, -1, SQLITE_STATIC);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}