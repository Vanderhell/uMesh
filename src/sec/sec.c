#include "sec.h"

/* Stub — will be implemented in Steps 6 & 7 */
umesh_result_t sec_init(const uint8_t   *master_key,
                        uint8_t          net_id,
                        umesh_security_t level)
{
    UMESH_UNUSED(master_key);
    UMESH_UNUSED(net_id);
    UMESH_UNUSED(level);
    return UMESH_OK;
}

umesh_result_t sec_encrypt_frame(umesh_frame_t *frame)
{
    UMESH_UNUSED(frame);
    return UMESH_OK;
}

umesh_result_t sec_decrypt_frame(umesh_frame_t *frame)
{
    UMESH_UNUSED(frame);
    return UMESH_OK;
}

void sec_regenerate_salt(void)
{
    /* no-op stub */
}
