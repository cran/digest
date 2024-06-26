/*

  digest -- hash digest functions for R

  Copyright (C) 2003 - 2024  Dirk Eddelbuettel <edd@debian.org>

  This file is part of digest.

  digest is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  digest is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with digest.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdint.h> // for uint32_t
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <R.h>
#include <Rdefines.h>
#include <Rinternals.h>
#include <Rversion.h>
#include <inttypes.h>
#include "sha1.h"
#include "sha2.h"
#include "sha256.h"
#include "md5.h"
#include "zlib.h"
#include "xxhash.h"
#include "pmurhash.h"
#include "blake3.h"
#include "crc32c.h"

#ifdef _WIN32
#include <Windows.h>
#endif

unsigned long ZEXPORT digest_crc32(unsigned long crc,
                                   const unsigned char FAR *buf,
                                   unsigned len);

static const char *sha2_hex_digits = "0123456789abcdef";

FILE* open_with_widechar_on_windows(const char* txt) {
    FILE* out;
#ifdef _WIN32
    wchar_t* buf;
    size_t len = MultiByteToWideChar(CP_UTF8, 0, txt, -1, NULL, 0);
    if (len <= 0) {
        error("Cannot convert file to Unicode: %s", txt);
    }
    buf = (wchar_t*) R_alloc(len, sizeof(wchar_t));
    if (buf == NULL) {
        error("Could not allocate buffer of size: %llu", len);
    }

    MultiByteToWideChar(CP_UTF8, 0, txt, -1, buf, len);
    out = _wfopen(buf, L"rb");
#else
    out = fopen(txt, "rb");
#endif

    return out;
}

SEXP digest(SEXP Txt, SEXP Algo, SEXP Length, SEXP Skip, SEXP Leave_raw, SEXP Seed) {
    size_t BUF_SIZE = 1024;
    FILE *fp=0;
    char *txt;
    int algo = INTEGER_VALUE(Algo);
    int  length = INTEGER_VALUE(Length);
    int skip = INTEGER_VALUE(Skip);
    int seed = INTEGER_VALUE(Seed);
    int leaveRaw = INTEGER_VALUE(Leave_raw);
    SEXP result = R_NilValue;
    char output[128+1], *outputp = output;    /* 33 for md5, 41 for sha1, 65 for sha256, 128 for sha512; plus trailing NULL */
    R_xlen_t nChar;
    int output_length = -1;
    if (IS_RAW(Txt)) { /* Txt is either RAW */
        txt = (char*) RAW(Txt);
#if defined(R_VERSION) && R_VERSION >= R_Version(3,0,0)
        nChar = XLENGTH(Txt);
#else
        nChar = LENGTH(Txt);
#endif
    } else { /* or a string */
        txt = (char*) STRING_VALUE(Txt);
        nChar = strlen(txt);

        if (algo >= 100) {
            fp = open_with_widechar_on_windows(txt);
            if (!fp)  {
              error("Cannot open input file: %s", txt);  /* #nocov */
            }
        }
    }
    if (skip > 0 && algo < 100) {
        if (skip>=nChar) {
            nChar=0;                                                            /* #nocov */
        } else {
            nChar -= skip;
            txt += skip;
        }
    }
    if (length>=0 && length<nChar) nChar = length;

    switch (algo) {
    case 1: {     /* md5 case */
        md5_context ctx;
        output_length = 16;
        unsigned char md5sum[16];
        int j;
        md5_starts( &ctx );
        md5_update( &ctx, (uint8 *) txt, nChar);
        md5_finish( &ctx, md5sum );
        memcpy(output, md5sum, 16);

        if (!leaveRaw)
            for (j = 0; j < 16; j++)
                snprintf(output + j * 2, 3, "%02x", md5sum[j]);

        break;
    }
    case 2: {     /* sha1 case */
        int j;
        sha1_context ctx;
        output_length = 20;
        unsigned char sha1sum[20];

        sha1_starts( &ctx );
        sha1_update( &ctx, (uint8 *) txt, nChar);
        sha1_finish( &ctx, sha1sum );
        memcpy(output, sha1sum, 20);

        if (!leaveRaw)
            for ( j = 0; j < 20; j++ )
                snprintf( output + j * 2, 3, "%02x", sha1sum[j] );

        break;
    }
    case 3: {     /* crc32 case */
        unsigned long val, l;
        l = nChar;

        val  = digest_crc32(0L, 0, 0);
        val  = digest_crc32(val, (unsigned char*) txt, (unsigned) l);

        snprintf(output, 128, "%08x", (unsigned int) val);
        break;
    }
    case 4: {     /* sha256 case */
        int j;
        sha256_context ctx;
        output_length = 32;
        unsigned char sha256sum[32];

        sha256_starts( &ctx );
        sha256_update( &ctx, (uint8 *) txt, nChar);
        sha256_finish( &ctx, sha256sum );
        memcpy(output, sha256sum, 32);

        if (!leaveRaw)
            for ( j = 0; j < 32; j++ )
                snprintf( output + j * 2, 3, "%02x", sha256sum[j] );

        break;
    }
    case 5: {     /* sha2-512 case */
        int j;
        SHA512_CTX ctx;
        output_length = SHA512_DIGEST_LENGTH;
        uint8_t sha512sum[output_length], *d = sha512sum;

        SHA512_Init(&ctx);
        SHA512_Update(&ctx, (uint8 *) txt, nChar);
        /* Calling SHA512_Final, because SHA512_End will already
           convert the hash to a string, and we also want RAW */
        SHA512_Final(sha512sum, &ctx);
        memcpy(output, sha512sum, output_length);

        /* adapted from SHA512_End */
        if (!leaveRaw) {
            for (j = 0; j < output_length; j++) {
                *outputp++ = sha2_hex_digits[(*d & 0xf0) >> 4];
                *outputp++ = sha2_hex_digits[*d & 0x0f];
                d++;
            }
            *outputp = (char)0;
        }
        break;
    }
    case 6: {     /* xxhash32 case */
        XXH32_hash_t val =  XXH32(txt, nChar, seed);
        snprintf(output, 128, "%08x", val);
        break;
    }
    case 7: {     /* xxhash64 case */
        XXH64_hash_t val =  XXH64(txt, nChar, seed);
        snprintf(output, 128, "%016" PRIx64, val);
        break;
    }
    case 8: {     /* MurmurHash3 32 */
        unsigned int val = PMurHash32(seed, txt, nChar);
        snprintf(output, 128, "%08x", val);
        break;
    }
    case 10: {     /* blake3 */
        output_length = BLAKE3_OUT_LEN;
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, txt, nChar);
        uint8_t val[BLAKE3_OUT_LEN];
        blake3_hasher_finalize(&hasher, val, BLAKE3_OUT_LEN);
        if (leaveRaw) {
            memcpy(output, val, BLAKE3_OUT_LEN);
        } else {
            for (size_t i = 0; i < BLAKE3_OUT_LEN; i++) {
                snprintf(output + i * 2, 3, "%02x", val[i]);
            }
        }
        break;
    }
    case 11: {		/* crc32c */
        uint32_t crc = 0;       /* initial value, can be zero */
        crc = crc32c_extend(crc, (const uint8_t*) txt, (size_t) nChar);
        snprintf(output, 128, "%08x", crc);
        break;
    }
    case 12: {		/* xxh3_64bits */
        XXH64_hash_t val =  XXH3_64bits_withSeed(txt, nChar, seed);
        snprintf(output, 128, "%016" PRIx64, val);
        break;
    }
    case 13: {		/* xxh3_128bits */
        XXH128_hash_t val =  XXH3_128bits_withSeed(txt, nChar, seed);
        snprintf(output, 128, "%016" PRIx64 "%016" PRIx64, val.high64, val.low64);
        break;
    }
    case 101: {     /* md5 file case */
        int j;
        md5_context ctx;
        output_length = 16;
        unsigned char buf[BUF_SIZE];
        unsigned char md5sum[16];

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        md5_starts( &ctx );
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0
                   && length>0) {
                if (nChar>length) nChar=length;
                md5_update( &ctx, buf, nChar );
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0)
                md5_update( &ctx, buf, nChar );
        }
        md5_finish( &ctx, md5sum );
        memcpy(output, md5sum, 16);
        if (!leaveRaw)
            for (j = 0; j < 16; j++)
                snprintf(output + j * 2, 3, "%02x", md5sum[j]);
        break;
    }
    case 102: {     /* sha1 file case */
        int j;
        sha1_context ctx;
        output_length = 20;
        unsigned char buf[BUF_SIZE];
        unsigned char sha1sum[20];

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        sha1_starts ( &ctx );
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0 && length>0) {
                if (nChar>length) nChar=length;
                sha1_update( &ctx, buf, nChar );
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0)
                sha1_update( &ctx, buf, nChar );
        }
        sha1_finish ( &ctx, sha1sum );
        memcpy(output, sha1sum, 20);
        if (!leaveRaw)
            for ( j = 0; j < 20; j++ )
                snprintf( output + j * 2, 3, "%02x", sha1sum[j] );
        break;
    }
    case 103: {     /* crc32 file case */
        unsigned char buf[BUF_SIZE];
        unsigned long val;

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        val  = digest_crc32(0L, 0, 0);
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0 && length>0) {
                if (nChar>length) nChar=length;
                val  = digest_crc32(val , buf, (unsigned) nChar);
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0)
                val  = digest_crc32(val , buf, (unsigned) nChar);
        }
        snprintf(output, 128, "%08x", (unsigned int) val);
        break;
    }
    case 104: {     /* sha256 file case */
        int j;
        sha256_context ctx;
        output_length = 32;
        unsigned char buf[BUF_SIZE];
        unsigned char sha256sum[32];

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        sha256_starts ( &ctx );
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0 && length>0) {
                if (nChar>length) nChar=length;
                sha256_update( &ctx, buf, nChar );
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0)
                sha256_update( &ctx, buf, nChar );
        }
        sha256_finish ( &ctx, sha256sum );
        memcpy(output, sha256sum, 32);
        if (!leaveRaw)
            for ( j = 0; j < 32; j++ )
                snprintf( output + j * 2, 3, "%02x", sha256sum[j] );
        break;
    }
    case 105: {     /* sha2-512 file case */
        int j;
        SHA512_CTX ctx;
        output_length = SHA512_DIGEST_LENGTH;
        uint8_t sha512sum[output_length], *d = sha512sum;

        unsigned char buf[BUF_SIZE];

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        SHA512_Init(&ctx);
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0 && length>0) {
                if (nChar>length) nChar=length;
                SHA512_Update( &ctx, buf, nChar );
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0)
                SHA512_Update( &ctx, buf, nChar );
        }

		/* Calling SHA512_Final, because SHA512_End will already
		   convert the hash to a string, and we also want RAW */
		SHA512_Final(sha512sum, &ctx);
		memcpy(output, sha512sum, output_length);

		/* adapted from SHA512_End */
		if (!leaveRaw) {
            for (j = 0; j < output_length; j++) {
                *outputp++ = sha2_hex_digits[(*d & 0xf0) >> 4];
                *outputp++ = sha2_hex_digits[*d & 0x0f];
                d++;
            }
            *outputp = (char)0;

		}
        break;
    }
    case 106: {     /* xxhash32 */
        unsigned char buf[BUF_SIZE];
        XXH32_state_t* const state = XXH32_createState();

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        XXH_errorcode const resetResult = XXH32_reset(state, seed);
        if (resetResult == XXH_ERROR) {
          error("Error in `XXH32_reset()`"); 				/* #nocov */
        }
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0 && length>0) {
                if (nChar>length) nChar=length;
                XXH_errorcode const updateResult = XXH32_update(state, buf, nChar);
                if (updateResult == XXH_ERROR) {
                  error("Error in `XXH32_update()`"); 		/* #nocov */
                }
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0) {
              XXH_errorcode const updateResult = XXH32_update(state, buf, nChar);
              if (updateResult == XXH_ERROR) {
                error("Error in `XXH32_update()`");	 		/* #nocov */
              }
            }
        }
        XXH32_hash_t val =  XXH32_digest(state);
        XXH32_freeState(state);

        snprintf(output, 128, "%08x", val);
        break;
    }
    case 107: {     /* xxhash64 */
        unsigned char buf[BUF_SIZE];
        XXH64_state_t* const state = XXH64_createState();

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        XXH_errorcode const resetResult = XXH64_reset(state, seed);
        if (resetResult == XXH_ERROR) {
          error("Error in `XXH64_reset()`"); 				/* #nocov */
        }
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0 && length>0) {
                if (nChar>length) nChar=length;
                XXH_errorcode const updateResult = XXH64_update(state, buf, nChar);
                if (updateResult == XXH_ERROR) {
                  error("Error in `XXH64_update()`"); 		/* #nocov */
                }
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0) {
                XXH_errorcode const updateResult = XXH64_update(state, buf, nChar);
              if (updateResult == XXH_ERROR) {
                error("Error in `XXH64_update()`"); 		/* #nocov */
              }
            }
        }
        XXH64_hash_t val =  XXH64_digest(state);
        XXH64_freeState(state);
        snprintf(output, 128, "%016" PRIx64, val);
        break;
    }
    case 108: {     /* murmur32 */
        unsigned int h1=seed, carry=0;
        unsigned char buf[BUF_SIZE];
        size_t total_length = 0;

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        if (length>=0) {
            while( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0
                   && length>0) {
                if (nChar>length) nChar=length;
                PMurHash32_Process(&h1, &carry, buf, nChar);
                length -= nChar;
                total_length += nChar;
            }
        } else {
            while( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0) {
                PMurHash32_Process(&h1, &carry, buf, nChar);
                total_length += nChar;
            }
        }
        unsigned int val = PMurHash32_Result(h1, carry, total_length);

        snprintf(output, 128, "%08x", val);
        break;
    }
    case 110: {     /* blake3 file case */
        output_length = BLAKE3_OUT_LEN;
        unsigned char buf[BUF_SIZE];
        uint8_t val[BLAKE3_OUT_LEN];
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0 && length>0) {
                if (nChar>length) nChar=length;
                blake3_hasher_update( &hasher, buf, nChar );
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0)
                blake3_hasher_update( &hasher, buf, nChar );
        }
        blake3_hasher_finalize(&hasher, val, BLAKE3_OUT_LEN);
        if (leaveRaw) {
            memcpy(output, val, BLAKE3_OUT_LEN);
        } else {
            for (size_t i = 0; i < BLAKE3_OUT_LEN; i++) {
                snprintf(output + i * 2, 3, "%02x", val[i]);
            }
        }
        break;
    }
    case 111: {		/* crc32c */
        unsigned char buf[BUF_SIZE];
        uint32_t crc = 0;

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0 && length>0) {
                if (nChar>length) nChar=length;
                crc = crc32c_extend(crc, (const uint8_t*) buf, (size_t) nChar);
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0)
                crc = crc32c_extend(crc, (const uint8_t*) buf, (size_t) nChar);
        }
        snprintf(output, 128, "%08x", (unsigned int) crc);
        break;
    }
    case 112: {     /* xxh3_64 */
        unsigned char buf[BUF_SIZE];
        XXH3_state_t* const state = XXH3_createState();

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        XXH_errorcode const resetResult = XXH3_64bits_reset(state);
        if (resetResult == XXH_ERROR) {
            error("Error in `XXH3_reset()`"); 				/* #nocov */
        }
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0 && length>0) {
                if (nChar>length) nChar=length;
                XXH_errorcode const updateResult = XXH3_64bits_update(state, buf, nChar);
                if (updateResult == XXH_ERROR) {
                    error("Error in `XXH3_64bits_update()`"); 		/* #nocov */
                }
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0) {
                XXH_errorcode const updateResult = XXH3_64bits_update(state, buf, nChar);
                if (updateResult == XXH_ERROR) {
                    error("Error in `XXH3_64bit_update()`"); 		/* #nocov */
                }
            }
        }
        XXH64_hash_t val =  XXH3_64bits_digest(state);
        XXH3_freeState(state);
        snprintf(output, 128, "%016" PRIx64, val);
        break;
    }
    case 113: {     /* xxh3_128 */
        unsigned char buf[BUF_SIZE];
        XXH3_state_t* const state = XXH3_createState();

        if (skip > 0) fseek(fp, skip, SEEK_SET);
        XXH_errorcode const resetResult = XXH3_128bits_reset(state);
        if (resetResult == XXH_ERROR) {
            error("Error in `XXH3_reset()`"); 				/* #nocov */
        }
        if (length>=0) {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0 && length>0) {
                if (nChar>length) nChar=length;
                XXH_errorcode const updateResult = XXH3_128bits_update(state, buf, nChar);
                if (updateResult == XXH_ERROR) {
                    error("Error in `XXH3_128bits_update()`"); 		/* #nocov */
                }
                length -= nChar;
            }
        } else {
            while ( ( nChar = fread( buf, 1, sizeof( buf ), fp ) ) > 0) {
                XXH_errorcode const updateResult = XXH3_128bits_update(state, buf, nChar);
                if (updateResult == XXH_ERROR) {
                    error("Error in `XXH3_128bit_update()`"); 		/* #nocov */
                }
            }
        }
        XXH128_hash_t val =  XXH3_128bits_digest(state);
        XXH3_freeState(state);
        snprintf(output, 128, "%016" PRIx64 "%016" PRIx64, val.high64, val.low64);
        break;
    }

    default: {
        error("Unsupported algorithm code"); /* should not be reached due to test in R */ /* #nocov */
    }
    } /* end switch */

    if (algo >= 100 && fp) {
        fclose(fp);
    }

    if (leaveRaw && output_length > 0) {
        PROTECT(result=allocVector(RAWSXP, output_length));
        memcpy(RAW(result), output, output_length);
    } else {
        PROTECT(result=allocVector(STRSXP, 1));
        SET_STRING_ELT(result, 0, mkChar(output));
    }
    UNPROTECT(1);

  return result;
}


SEXP vdigest(SEXP Txt, SEXP Algo, SEXP Length, SEXP Skip, SEXP Leave_raw, SEXP Seed){
    R_xlen_t n = length(Txt);
    if (TYPEOF(Txt) == RAWSXP || n == 0)
        return(digest(Txt, Algo, Length, Skip, Leave_raw, Seed));
    SEXP ans = PROTECT(allocVector(STRSXP, n));
    SEXP d = R_NilValue;
    if (TYPEOF(Txt) == VECSXP){
        for (R_xlen_t i = 0; i < n; i++){
            d = digest(VECTOR_ELT(Txt, i), Algo, Length, Skip, Leave_raw, Seed);
            SET_STRING_ELT(ans, i, STRING_ELT(d, 0));
        }
    } else {
        for (R_xlen_t i = 0; i < n; i++){
            d = digest(STRING_ELT(Txt, i), Algo, Length, Skip, Leave_raw, Seed);
            SET_STRING_ELT(ans, i, STRING_ELT(d, 0));
        }
    }
    UNPROTECT(1);
    return ans;
}


// Also already used in sha2.h
//
// We can rely on WORDS_BIGENDIAN only be defined on big endian systems thanks to Rconfig.
//
// A number of other #define based tests are in other source files here for different hash
// algorithm implementations notably crc32c, pmurhash, sha2 and xxhash
//
// A small and elegant test is also in package qs based on https://stackoverflow.com/a/1001373

// edd 02 Dec 2013  use Rconfig.h to define BYTE_ORDER, unless already defined
#ifndef BYTE_ORDER
    // see sha2.c comments, and on the internet at large
    #define LITTLE_ENDIAN 1234
    #define BIG_ENDIAN    4321
#ifdef WORDS_BIGENDIAN
    #define BYTE_ORDER  BIG_ENDIAN
#else
    #define BYTE_ORDER  LITTLE_ENDIAN
#endif
#endif

SEXP is_big_endian(void) {
    return Rf_ScalarLogical(BYTE_ORDER == BIG_ENDIAN);
}
SEXP is_little_endian(void) {
    return Rf_ScalarLogical(BYTE_ORDER == LITTLE_ENDIAN);
}
