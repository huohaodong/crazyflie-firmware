#ifndef __AODV_H__
#define __AODV_H__

#include "swarm_ranging.h"

#define OLSR_DEBUG_ENABLE

/* Queue Constants */
#define OLSR_RX_QUEUE_SIZE 5
#define OLSR_RX_QUEUE_ITEM_SIZE sizeof (UWB_Packet_t)

/* OLSR Message Constants */
#define OLSR_TC_INTERVAL 500

typedef Neighbor_Bit_Set_t MPR_Set_t;
void mprSetInit(MPR_Set_t *set);
void mprSetAdd(MPR_Set_t *set, UWB_Address_t neighborAddress);
void mprSetRemove(MPR_Set_t *set, UWB_Address_t neighborAddress);
bool mprSetHas(MPR_Set_t *set, UWB_Address_t neighborAddress);
void mprSetClear(MPR_Set_t *set);

//void mprSelectorSetAdd(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress);
//void mprSelectorSetRemove(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress);
//void mprSelectorSetClear(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress);
//bool mprSelectorSetHas(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress);

void olsrInit();

#endif
