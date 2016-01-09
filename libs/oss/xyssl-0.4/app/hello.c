/*
 *  "Hello, world!" demonstration program
 *
 *  Copyright (C) 2006  Christophe Devine
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License, version 2.1 as published by the Free Software Foundation.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

#ifndef _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_DEPRECATE 1
#endif

#include <stdio.h>

#include "md5.h"

/*
 * dummy DHM parameters (not used)
 */
char dhm_ext_modulus[]   = "";
char dhm_ext_generator[] = "";

int main( void )
{
    int i;
    uchar digest[16];
    uchar str[] = "Hello, world!";

    printf( "\n  MD5('%s') = ", str );

    md5_csum( str, 13, digest );

    for( i = 0; i < 16; i++ )
        printf( "%02x", digest[i] );

    printf( "\n\n" );

#ifdef WIN32
    printf( "  Press Enter to exit this program.\n" );
    fflush( stdout ); getchar();
#endif

    return( 0 );
}
