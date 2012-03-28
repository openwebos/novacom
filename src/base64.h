/* LibTomCrypt, modular cryptographic library -- Tom St Denis
 *
 * LibTomCrypt is a library that provides various cryptographic
 * algorithms in a highly modular and flexible manner.
 *
 * The library is free for all purposes without any express
 * guarantee it works.
 *
 * Tom St Denis, tomstdenis@gmail.com, http://libtom.org
 */
#ifndef __BASE64_H
#define __BASE64_H

int base64_encode(const unsigned char *in,  unsigned long inlen, 
                        unsigned char *out, unsigned long *outlen);

int base64_decode(const unsigned char *in,  unsigned long inlen, 
                        unsigned char *out, unsigned long *outlen);

#endif

