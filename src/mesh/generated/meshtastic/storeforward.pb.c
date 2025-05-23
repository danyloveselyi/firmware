/* Automatically generated nanopb constant definitions */
#include "meshtastic/storeforward.pb.h"

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* Define missing MSGTYPE macros */
#define meshtastic_StoreAndForward_variant_heartbeat_MSGTYPE meshtastic_StoreAndForward_HeartbeatPayload
#define meshtastic_StoreAndForward_variant_stats_MSGTYPE meshtastic_StoreAndForward_StatsPayload
#define meshtastic_StoreAndForward_variant_history_MSGTYPE meshtastic_StoreAndForward_HistoryPayload
#define meshtastic_StoreAndForward_variant_text_MSGTYPE meshtastic_StoreAndForward_TextPayload

/* Binding for message structs */
PB_BIND(meshtastic_StoreAndForward_HeartbeatPayload, meshtastic_StoreAndForward_HeartbeatPayload, AUTO)
PB_BIND(meshtastic_StoreAndForward_StatsPayload, meshtastic_StoreAndForward_StatsPayload, AUTO)
PB_BIND(meshtastic_StoreAndForward_HistoryPayload, meshtastic_StoreAndForward_HistoryPayload, AUTO)
PB_BIND(meshtastic_StoreAndForward_TextPayload, meshtastic_StoreAndForward_TextPayload, AUTO)
PB_BIND(meshtastic_StoreAndForward, meshtastic_StoreAndForward, AUTO)
