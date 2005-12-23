#include <byteswap.h>
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
