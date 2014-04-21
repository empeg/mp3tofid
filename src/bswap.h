#if HAVE_BYTESWAP_H
#include <byteswap.h>
#elif defined(USE_SYS_ENDIAN_H)
#include <sys/endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define bswap_16 OSSwapInt16
#define bswap_32 OSSwapInt32
#define bswap_64 OSSwapInt64
#else
#define bswap_16(value)  \
  ((((value) & 0xff) << 8) | ((value) >> 8))

#define bswap_32(value)\
  (((uint32_t)bswap_16((uint16_t)((value) & 0xffff)) << 16) | \
   (uint32_t)bswap_16((uint16_t)((value) >> 16)))
 
#define bswap_64(value)\
  (((uint64_t)bswap_32((uint32_t)((value) & 0xffffffff)) \
    << 32) | \
   (uint64_t)bswap_32((uint32_t)((value) >> 32)))
#endif
#include "config.h"

/*
 * be2me ... BigEndian to MachineEndian
 * le2me ... LittleEndian to MachineEndian
 */

#ifdef WORDS_BIGENDIAN
#define be2me_16(x) /**/
#define be2me_32(x) /**/
#define be2me_64(x) /**/
#define le2me_16(x) bswap_16(x)
#define le2me_32(x) bswap_32(x)
#define le2me_64(x) bswap_64(x)
#else
#define be2me_16(x) bswap_16(x)
#define be2me_32(x) bswap_32(x)
#define be2me_64(x) bswap_64(x)
#define le2me_16(x) /**/
#define le2me_32(x) /**/
#define le2me_64(x) /**/
#endif
