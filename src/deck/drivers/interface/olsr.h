#ifndef __AODV_H__
#define __AODV_H__

/* Queue Constants */
#define OLSR_RX_QUEUE_SIZE 5
#define OLSR_RX_QUEUE_ITEM_SIZE sizeof (UWB_Packet_t)

void olsrInit();
#endif
