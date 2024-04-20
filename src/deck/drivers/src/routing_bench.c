#include "system.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "autoconf.h"
#include "debug.h"
#include "system.h"
#include "timers.h"
#include "routing_bench.h"
#include "swarm_ranging.h"

static TaskHandle_t benchTxTaskHandle;
static TaskHandle_t benchRxTaskHandle;
static QueueHandle_t rxQueue;
static uint32_t seqNumber = 1;

static void processMockDataRequest(UWB_Address_t neighborAddress, Mock_Data_Request_t *request) {
  // TODO
}

static void processMockDataReply(UWB_Address_t neighborAddress, Mock_Data_Reply_t *reply) {
  // TODO
}

static void sendMockDataRequest(UWB_Address_t neighborAddress) {
  UWB_Data_Packet_t dataTxPacket;
  dataTxPacket.header.type = UWB_DATA_MESSAGE_RESERVED;
  dataTxPacket.header.srcAddress = uwbGetAddress();
  dataTxPacket.header.destAddress = neighborAddress;
  dataTxPacket.header.ttl = 10;
  dataTxPacket.header.length = sizeof(UWB_Data_Packet_Header_t) + sizeof(Mock_Data_Request_t);
  Mock_Data_Request_t *request = (Mock_Data_Request_t *) &dataTxPacket.payload;
  request->header.type = MOCK_DATA_REQUEST;
  request->header.seqNumber = seqNumber++;
  // TODO
  uwbSendDataPacketBlock(&dataTxPacket);
}

static void sendMockDataReply(UWB_Address_t neighborAddress) {
  UWB_Data_Packet_t dataTxPacket;
  dataTxPacket.header.type = UWB_DATA_MESSAGE_RESERVED;
  dataTxPacket.header.srcAddress = uwbGetAddress();
  dataTxPacket.header.destAddress = neighborAddress;
  dataTxPacket.header.ttl = 10;
  dataTxPacket.header.length = sizeof(UWB_Data_Packet_Header_t) + sizeof(Mock_Data_Reply_t);
  Mock_Data_Request_t *reply = (Mock_Data_Request_t *) &dataTxPacket.payload;
  reply->header.type = MOCK_DATA_REPLY;
  reply->header.seqNumber = seqNumber++;
  // TODO
  uwbSendDataPacketBlock(&dataTxPacket);
}

static void benchTxTask(void *parameters) {
  systemWaitStart();
  srand(uwbGetAddress());
  
  while (true) {
    for (UWB_Address_t neighbor = 0; neighbor <= NEIGHBOR_ADDRESS_MAX; neighbor++) {
      if (neighbor == uwbGetAddress()) {
        continue;
      }
      sendMockDataRequest(neighbor);
      vTaskDelay(M2T(100 + random() % 15));
    }
    vTaskDelay(M2T(1));
  }

}

static void benchRxTask(void *parameters) {
  systemWaitStart();

  UWB_Data_Packet_t dataRxPacket;

  while (1) {
    if (uwbReceiveDataPacketBlock(UWB_DATA_MESSAGE_RESERVED, &dataRxPacket)) {
      MOCK_DATA_MESSAGE_TYPE msgType = ((Mock_Data_Header_t *) dataRxPacket.payload)->type;
      switch (msgType) {
        case MOCK_DATA_REQUEST:DEBUG_PRINT("%u received mock data request from %u.\n",
                                           uwbGetAddress(),
                                           dataRxPacket.header.srcAddress);
          processMockDataRequest(dataRxPacket.header.srcAddress,
                                 (Mock_Data_Request_t *) dataRxPacket.payload);
          break;
        case MOCK_DATA_REPLY:DEBUG_PRINT("%u received mock data reply from %u.\n",
                                         uwbGetAddress(),
                                         dataRxPacket.header.srcAddress);
          processMockDataReply(dataRxPacket.header.srcAddress,
                               (Mock_Data_Reply_t *) dataRxPacket.payload);
          break;
        default:DEBUG_PRINT("%u received unknown mock data type = %u from %u.\n",
                            uwbGetAddress(), msgType,
                            dataRxPacket.header.srcAddress);
      }
    }
    vTaskDelay(M2T(1));
  }

}

void routingBenchInit() {
  rxQueue = xQueueCreate(ROUTING_BENCH_RX_QUEUE_SIZE, ROUTING_BENCH_RX_QUEUE_ITEM_SIZE);
  UWB_Data_Packet_Listener_t listener = {
      .type = UWB_DATA_MESSAGE_RESERVED,
      .rxQueue = rxQueue
  };
  uwbRegisterDataPacketListener(&listener);

  xTaskCreate(benchTxTask, "ADHOC_ROUTING_BENCH_TX", UWB_TASK_STACK_SIZE, NULL,
              ADHOC_DECK_TASK_PRI, &benchTxTaskHandle);

  xTaskCreate(benchRxTask, "ADHOC_ROUTING_BENCH_RX", UWB_TASK_STACK_SIZE, NULL,
              ADHOC_DECK_TASK_PRI, &benchRxTaskHandle);
}