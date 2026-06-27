#include "context.h"

static umesh_ctx_t s_default_ctx;
static umesh_ctx_t *s_current_ctx = &s_default_ctx;

void umesh_bind_ctx(umesh_ctx_t *ctx)
{
    s_current_ctx = ctx ? ctx : &s_default_ctx;
}

umesh_ctx_t *umesh_current_ctx(void)
{
    if (!s_current_ctx) {
        s_current_ctx = &s_default_ctx;
    }
    return s_current_ctx;
}
