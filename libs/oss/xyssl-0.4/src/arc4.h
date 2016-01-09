/**
 * \file arc4.h
 */
#ifndef _ARC4_H
#define _ARC4_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _STD_TYPES
#define _STD_TYPES

#define uchar   unsigned char
#define uint    unsigned int
#define ulong   unsigned long int

#endif

/**
 * \brief          ARC4 context structure
 */
typedef struct
{
    int m[256];         /*!< permutation table */
    int x;              /*!< permutation index */
    int y;              /*!< permutation index */
}
arc4_context;

/**
 * \brief          ARC4 key schedule
 *
 * \param ctx      ARC4 context to be initialized
 * \param key      the secret key
 * \param keylen   length of the key
 */
void arc4_setup( arc4_context *ctx, uchar *key, int keylen );

/**
 * \brief          ARC4 cipher function
 *
 * \param ctx      ARC4 context
 * \param buf      buffer to be processed
 * \param buflen   amount of data in buf
 */
void arc4_crypt( arc4_context *ctx, uchar *buf, int buflen );

/*
 * \brief          Checkup routine
 *
 * \return         0 if successful, or 1 if the test failed
 */
int arc4_self_test( void );

// 2007.02.27
#define RC4_CTX arc4_context
#define RC4_setup(a, b, c) arc4_setup(a, b, c)
#define RC4_crypt(a, b, c, d) arc4_crypt(a, b, d)

#ifdef __cplusplus
}
#endif

#endif /* arc4.h */
