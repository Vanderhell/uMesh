#ifndef UMESH_CONTEXT_H
#define UMESH_CONTEXT_H

#include "../include/umesh.h"

void umesh_bind_ctx(umesh_ctx_t *ctx);
umesh_ctx_t *umesh_current_ctx(void);

#endif /* UMESH_CONTEXT_H */
