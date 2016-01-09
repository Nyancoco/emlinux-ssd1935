/*
 *  Self-test demonstration program
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

#include "md2.h"
#include "md4.h"
#include "md5.h"
#include "sha1.h"
#include "sha2.h"
#include "arc4.h"
#include "des.h"
#include "aes.h"
#include "bignum.h"
#include "base64.h"
#include "rsa.h"
#include "x509.h"

/*
 * dummy DHM parameters (not used)
 */
char dhm_ext_modulus[]   = "";
char dhm_ext_generator[] = "";

int main( void )
{
    int ret;

    printf( "\n" );

    if( ( ret =    md2_self_test() ) == 0 &&
        ( ret =    md4_self_test() ) == 0 &&
        ( ret =    md5_self_test() ) == 0 &&
        ( ret =   sha1_self_test() ) == 0 &&
        ( ret =   sha2_self_test() ) == 0 &&
        ( ret =   arc4_self_test() ) == 0 &&
        ( ret =    des_self_test() ) == 0 &&
        ( ret =    aes_self_test() ) == 0 &&
        ( ret =    mpi_self_test() ) == 0 &&
        ( ret = base64_self_test() ) == 0 &&
        ( ret =    rsa_self_test() ) == 0 &&
        ( ret =   x509_self_test() ) == 0 )
        printf( "  [ All tests passed ]\n" );

    printf( "\n" );

#ifdef WIN32
    printf( "  Press Enter to exit this program.\n" );
    fflush( stdout ); getchar();
#endif

    return( ret );
}
