/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 *
 * Adapted for `long double' by Ulrich Drepper <drepper@cygnus.com>.
 */

#include <math_private.h>

long double
__atanl (long double x)
{
  long double res;

  __asm__ ("fld1\n"
       "fpatan"
       : "=t" (res) : "0" (x));

  return res;
}

weak_alias (__atanl, atanl)
