#ifndef __AODV_H__
#define __AODV_H__

#include "swarm_ranging.h"

#define OLSR_DEBUG_ENABLE

/* Queue Constants */
#define OLSR_RX_QUEUE_SIZE 5
#define OLSR_RX_QUEUE_ITEM_SIZE sizeof (UWB_Packet_t)

/* OLSR Message Constants */
#define OLSR_TC_INTERVAL 500
#define OLSR_MPR_SELECTOR_SET_HOLD_TIME (6 * OLSR_TC_INTERVAL)

typedef Neighbor_Bit_Set_t MPR_Set_t;

typedef struct {
  Neighbor_Bit_Set_t mprSelectors;
  Time_t expirationTime[NEIGHBOR_ADDRESS_MAX + 1];
} MPR_Selector_Set_t;

MPR_Set_t *getGlobalMPRSet();
void mprSetInit(MPR_Set_t *set);
void mprSetAdd(MPR_Set_t *set, UWB_Address_t neighborAddress);
void mprSetRemove(MPR_Set_t *set, UWB_Address_t neighborAddress);
bool mprSetHas(MPR_Set_t *set, UWB_Address_t neighborAddress);
void mprSetClear(MPR_Set_t *set);

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
