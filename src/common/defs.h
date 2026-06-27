#ifndef UMESH_DEFS_H
#define UMESH_DEFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../include/umesh.h"

#define UMESH_ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#define UMESH_UNUSED(x)      (void)(x)
#define UMESH_MIN(a,b)       ((a) < (b) ? (a) : (b))
#define UMESH_MAX(a,b)       ((a) > (b) ? (a) : (b))

#define UMESH_HOP_FROM_SEQ(s)  (((s) >> 12) & 0x0F)
#define UMESH_SEQ_FROM_SEQ(s)  ((s) & 0x0FFF)

#ifndef UMESH_LOG_DEBUG
#define UMESH_LOG_DEBUG(fmt, ...) (void)0
#endif
#ifndef UMESH_LOG_WARN
#define UMESH_LOG_WARN(fmt, ...)  (void)0
#endif
#ifndef UMESH_LOG_ERROR
#define UMESH_LOG_ERROR(fmt, ...) (void)0
#endif

#endif /* UMESH_DEFS_H */
