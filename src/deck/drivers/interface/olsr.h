#ifndef __AODV_H__
#define __AODV_H__

#include "swarm_ranging.h"

#define OLSR_DEBUG_ENABLE

/* Queue Constants */
#define OLSR_RX_QUEUE_SIZE 5
#define OLSR_RX_QUEUE_ITEM_SIZE sizeof (UWB_Packet_t)

/* OLSR Message Constants */
#define OLSR_PACKET_SIZE_MAX UWB_PAYLOAD_SIZE_MAX
#define OLSR_PACKET_PAYLOAD_SIZE_MAX (OLSR_PACKET_SIZE_MAX - sizeof(OLSR_Packet_Header_t))
#define OLSR_TC_MAX_BODY_UNIT ((OLSR_PACKET_PAYLOAD_SIZE_MAX - sizeof(OLSR_Message_Header_t)) / sizeof(OLSR_TC_Body_Unit_t))
#define OLSR_TC_INTERVAL 500

/* MPR Selector Set */
#define OLSR_MPR_SELECTOR_SET_HOLD_TIME (6 * OLSR_TC_INTERVAL)

typedef enum {
  OLSR_HELLO_MESSAGE = 1, /* Use Ranging instead of HELLO here, see swarm_ranging.h */
  OLSR_TC_MESSAGE = 2,
} OLSR_MESSAGE_TYPE;

typedef struct {
  uint16_t seqNumber;
  uint16_t length;
} __attribute__((packed)) OLSR_Packet_Header_t;

typedef struct {
  OLSR_Packet_Header_t header;
  uint8_t payload[OLSR_PACKET_PAYLOAD_SIZE_MAX];
} __attribute__((packed)) OLSR_Packet_t;

typedef struct {
  OLSR_MESSAGE_TYPE type;
  uint16_t srcAddress;
  uint16_t msgSequence;
  uint16_t msgLength;
  uint8_t ttl;
  uint8_t hopCount;
} __attribute__((packed)) OLSR_Message_Header_t;

typedef struct {
  UWB_Address_t mprSelector;
  // TODO: add link state weight
} __attribute__((packed)) OLSR_TC_Body_Unit_t;

typedef struct {
  OLSR_Message_Header_t header;
  OLSR_TC_Body_Unit_t bodyUnits[OLSR_TC_MAX_BODY_UNIT];
} __attribute__((packed)) OLSR_TC_Message_t;

typedef Neighbor_Bit_Set_t MPR_Set_t;

typedef struct {
  Neighbor_Bit_Set_t mprSelectors;
  Time_t expirationTime[NEIGHBOR_ADDRESS_MAX + 1];
} MPR_Selector_Set_t;

/* MPR Set Operations */
MPR_Set_t *getGlobalMPRSet();
void mprSetInit(MPR_Set_t *set);
void mprSetAdd(MPR_Set_t *set, UWB_Address_t neighborAddress);
void mprSetRemove(MPR_Set_t *set, UWB_Address_t neighborAddress);
bool mprSetHas(MPR_Set_t *set, UWB_Address_t neighborAddress);
void mprSetClear(MPR_Set_t *set);

/* MPR Selector Set Operations */
MPR_Selector_Set_t *getGlobalMPRSelectorSet();
void mprSelectorSetInit(MPR_Selector_Set_t *set);
void mprSelectorSetAdd(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress);
void mprSelectorSetRemove(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress);
bool mprSelectorSetHas(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress);
void mprSelectorSetUpdateExpirationTime(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress);
int mprSelectorSetClearExpire(MPR_Selector_Set_t *set);

void olsrInit();

/* Debug Operations */
void printMPRSet(MPR_Set_t *set);
void printMPRSelectorSet(MPR_Selector_Set_t *set);

#endif
