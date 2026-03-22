#include "frame.h"

/* Stub — will be implemented in Step 5 */
umesh_result_t frame_serialize(const umesh_frame_t *frame,
                               uint8_t *buf, uint8_t buf_size,
                               uint8_t *out_len)
{
    UMESH_UNUSED(frame);
    UMESH_UNUSED(buf);
    UMESH_UNUSED(buf_size);
    UMESH_UNUSED(out_len);
    return UMESH_ERR_NOT_INIT;
}

umesh_result_t frame_deserialize(const uint8_t *buf, uint8_t len,
                                 umesh_frame_t *frame)
{
    UMESH_UNUSED(buf);
    UMESH_UNUSED(len);
    UMESH_UNUSED(frame);
    return UMESH_ERR_NOT_INIT;
}
