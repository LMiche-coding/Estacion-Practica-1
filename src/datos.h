#ifndef DATOS_H
#define DATOS_H 
#include <stddef.h>
#include "sqlite3.h"

struct Pendiente 
{
    long seq; 
    long ts; 
    double temp; 
    double corriente; 
}; 

sqlite3 *db_abrir(const char *ruta); 
int db_begin(sqlite3 *db); 
int db_commit(sqlite3 *db); 
int db_rollback(sqlite3 *db); 

int cfg_guardar(sqlite3 *db, const char *clave, const char *valor); 
int cfg_leer(sqlite3 *db, const char *clave, char *out, size_t n); 

sqlite3_stmt *lectura_preparar(sqlite3 *db);
long lectura_insertar_csv(sqlite3_stmt *st, char *linea);  
int descarga_totales(sqlite3 *db, long *filas, long *suma);
int analisis_json(sqlite3 *db, double umbral, char *out, size_t n);

long tel_siguiente_seq(sqlite3 *db);
int tel_insertar(sqlite3 *db, long seq, long ts, double temp, double corr);
int tel_pendientes(sqlite3 *db);
int tel_obtener_pendiente(sqlite3 *db, struct Pendiente *p);
int tel_marcar_enviado(sqlite3 *db, long seq);
int tel_sumar_intento(sqlite3 *db, long seq);

int alarma_insertar(sqlite3 *db, const char *cod, const char *maq, long ts_srv, long ts_rec);
int alarma_ya_confirmada(sqlite3 *db, const char *cod);
int alarma_marcar_ack(sqlite3 *db, const char *cod);

#endif