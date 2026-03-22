#ifndef UMESH_KEYS_H
#define UMESH_KEYS_H

#include "../common/defs.h"

/*
 * Derive ENC_KEY and AUTH_KEY from MASTER_KEY and net_id.
 * ENC_KEY  = AES(MASTER_KEY, 0x01 || net_id || padding)
 * AUTH_KEY = AES(MASTER_KEY, 0x02 || net_id || padding)
 */
void keys_derive(const uint8_t *master_key,
                 uint8_t        net_id,
                 uint8_t       *enc_key,
                 uint8_t       *auth_key);

#endif /* UMESH_KEYS_H */
