#ifndef __AODV_H__
#define __AODV_H__

#define OLSR_DEBUG_ENABLE

/* Queue Constants */
#define OLSR_RX_QUEUE_SIZE 5
#define OLSR_RX_QUEUE_ITEM_SIZE sizeof (UWB_Packet_t)

/* OLSR Message Constants */
#define OLSR_TC_INTERVAL 500

void olsrInit();
#endif
