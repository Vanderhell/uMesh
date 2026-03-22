#ifndef UMESH_DISCOVERY_H
#define UMESH_DISCOVERY_H

#include "../common/defs.h"

umesh_result_t discovery_init(uint8_t net_id, uint8_t node_id,
                               umesh_role_t role);
umesh_result_t discovery_join(void);
void           discovery_leave(void);
void           discovery_on_frame(const umesh_frame_t *frame,
                                  int8_t rssi);

#endif /* UMESH_DISCOVERY_H */
