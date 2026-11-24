#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"


#define DB_FILE "estacion.db"


static int crear_tabla(sqlite3 *db) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS config ("
        "  clave  INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  valor  TEXT    NOT NULL"
        ");"
        
         "CREATE TABLE IF NOT EXISTS lecturas_planta ("
        "  id     INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  maquina TEXT    NOT NULL,"
        "  ts   INTEGER,"
        "  temp INTEGER, "
        "  corriente INTEGER,"
        "  vibracion INTEGER,"
        "  estado INTEGER"
        ");"

         "CREATE TABLE IF NOT EXISTS telemetria ("
        "  seq     INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ts   INTEGER CHECK (edad >= 0),"
        "  temp INTEGER, "
        "  corriente INTEGER,"
        "  enviado INTEGER CHECK(enviado == 0 || enviado == 1),"
        "  intentos INTEGER"
        ");"

         "CREATE TABLE IF NOT EXISTS alarmas ("
        "  codigo     INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  maquina  TEXT    NOT NULL,"
        "  ts_servidor   INTEGER CHECK (edad >= 0),"
        "  ts_recibida  TEXT    ,"
        "  ts_ack  TEXT    ,"

        ");"

        ;
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "Error creando tabla: %s\n", err);
        sqlite3_free(err);
        return 0;
    }
    return 1;
}