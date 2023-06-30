/*-------------------------------------------------------------------------
   stack_probe_err.c

   Copyright (C) 2023, Oleg Endo

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this library; see the file COPYING. If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.

   As a special exception, if you link this library with other files,
   some of which are compiled with SDCC, to produce an executable,
   this library does not by itself cause the resulting executable to
   be covered by the GNU General Public License. This exception does
   not however invalidate any other reasons why the executable file
   might be covered by the GNU General Public License.
-------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>

/* The '_stack_probe_err' function is called by the stack probing code in the
   function prologue via when it detects a potential stack pointer over/underflow.
   The default implementation does nothing special.
   It can/should be overridden by the user.  The function is expected to
   be __nonbanked on mcs51.  */

#if defined(__SDCC_mcs51)
_Noreturn void _stack_probe_err (void) __nonbanked
#else
_Noreturn void _stack_probe_err (void)
#endif
{
  for(;;);
}
