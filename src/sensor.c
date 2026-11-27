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

#include "sensor.h"
#include <math.h>
#include <stdlib.h> 
#include <time.h>

static double ruido(void)
{
    return((double)rand() / RAND_MAX * 2.0 - 1.0); 
}

static double limitar(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v); 
}

void sensor_leer(double *temp, double *corriente)
{
    double t = (double)time(NULL);
    *temp = limitar(35.0 + 8.0 * sin(t/120.0) + 1.5 * ruido(), 25.0, 45.0); 
    *corriente = limitar(3.5 + 1.2 * sin(t/45.0) + 0.3 * ruido(), 2.0, 5.0);
}
