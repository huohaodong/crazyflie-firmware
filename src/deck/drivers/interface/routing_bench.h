#ifndef __ROUTING_BENCH_H__
#define __ROUTING_BENCH_H__

#include <stdint.h>
#include "adhocdeck.h"
#include "routing.h"

#define ROUTING_BENCH_RX_QUEUE_SIZE 15
#define ROUTING_BENCH_RX_QUEUE_ITEM_SIZE sizeof(UWB_Data_Packet_t)

typedef enum {
  MOCK_DATA_REQUEST,
  MOCK_DATA_REPLY
} MOCK_DATA_MESSAGE_TYPE;

typedef struct {
  MOCK_DATA_MESSAGE_TYPE type;
  uint32_t seqNumber;
} __attribute__((packed)) Mock_Data_Header_t;

typedef struct {
  Mock_Data_Header_t header;
  Time_t txTime;
  uint32_t sendCount; /* number of request send to corresponding neighbor */
} __attribute__((packed)) Mock_Data_Request_t;

typedef struct {
  Mock_Data_Header_t header;
  Time_t txTime; /* tx time of corresponding request */
  uint32_t recvCount; /* number of request received from corresponding neighbor */
  double PDR; /* Packet Delivery Rate of corresponding neighbor to me */
} __attribute__((packed)) Mock_Data_Reply_t;

void routingBenchInit();
#endif
