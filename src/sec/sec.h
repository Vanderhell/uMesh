#ifndef UMESH_SEC_H
#define UMESH_SEC_H

#include "../common/defs.h"
#include "keys.h"

umesh_result_t sec_init(const uint8_t   *master_key,
                        uint8_t          net_id,
                        umesh_security_t level);

umesh_result_t sec_encrypt_frame(umesh_frame_t *frame);
umesh_result_t sec_decrypt_frame(umesh_frame_t *frame);

void sec_regenerate_salt(void);

#endif /* UMESH_SEC_H */
