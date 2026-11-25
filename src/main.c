/**********************************************************/
/*           Programación para mecatrónicos               */
/*  Nombre:    Layla Garcia                               */
/*  Matricula: 2026-0288                                  */
/*  Seccion:   Miercoles                                  */
/*  Practica:  Practica 1                                */
/*  Fecha:     25/11/2026                                 */                       
/* Link Practica: https://github.com/LMiche-coding/parcial-c-20260288 */
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
#include "sqlite3.h"


#define DB_FILE "estacion.db"

static void leer_linea(const char *msg, char *buf, size_t n)
{
    printf("%s", msg); 
    fflush(stdout); 
     if (!fgets(buf, (int)n, stdin)) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\r\n")] = '\0';           /* quitar salto de línea */
}

static int leer_entero(const char *msg) {
    char buf[32];
    leer_linea(msg, buf, sizeof buf);
    return atoi(buf);
}

/* ---------- 0. Crear la tabla ---------- */




int main()
{
    sqlite3 *db; 
    sqlite3_open("estacion.db", &db);
    
    sqlite3_close(db); 
    return 0; 
}