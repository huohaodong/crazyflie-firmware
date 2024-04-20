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
static TimerHandle_t printTimer;
static uint32_t seqNumber = 1;

static uint32_t totalSendCount = 0;
static uint32_t totalRecvCount = 0;
static uint32_t recvCount[NEIGHBOR_ADDRESS_MAX + 1] = {[0 ... NEIGHBOR_ADDRESS_MAX] = 0};
static uint32_t sendCount[NEIGHBOR_ADDRESS_MAX + 1] = {[0 ... NEIGHBOR_ADDRESS_MAX] = 0};
static uint32_t RTT_COUNT[NEIGHBOR_ADDRESS_MAX + 1] = {[0 ... NEIGHBOR_ADDRESS_MAX] = 0};
static double AVG_RTT[NEIGHBOR_ADDRESS_MAX + 1] = {[0 ... NEIGHBOR_ADDRESS_MAX] = 0.0}; /* Average Round-trip time */
static double PDR[NEIGHBOR_ADDRESS_MAX + 1] = {[0 ... NEIGHBOR_ADDRESS_MAX] = 0.0}; /* Packet Delivery Rate */
static uint16_t lastRecvSeq[NEIGHBOR_ADDRESS_MAX + 1] = {[0 ... NEIGHBOR_ADDRESS_MAX] = 0};

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
  request->txTime = xTaskGetTickCount();
  request->sendCount = sendCount[neighborAddress];

  uwbSendDataPacketBlock(&dataTxPacket);
}

static void processMockDataRequest(UWB_Address_t neighborAddress, Mock_Data_Request_t *request) {
  UWB_Data_Packet_t dataTxPacket;
  dataTxPacket.header.type = UWB_DATA_MESSAGE_RESERVED;
  dataTxPacket.header.srcAddress = uwbGetAddress();
  dataTxPacket.header.destAddress = neighborAddress;
  dataTxPacket.header.ttl = 10;
  dataTxPacket.header.length = sizeof(UWB_Data_Packet_Header_t) + sizeof(Mock_Data_Reply_t);

  Mock_Data_Reply_t *reply = (Mock_Data_Reply_t *) &dataTxPacket.payload;
  reply->header.type = MOCK_DATA_REPLY;
  reply->header.seqNumber = seqNumber++;
  reply->txTime = request->txTime;
  reply->recvCount = recvCount[neighborAddress];
  reply->PDR = recvCount[neighborAddress] * 1.0 / sendCount[neighborAddress];
  uwbSendDataPacketBlock(&dataTxPacket);
}

static void processMockDataReply(UWB_Address_t neighborAddress, Mock_Data_Reply_t *reply) {
  Time_t curTime = xTaskGetTickCount();
  uint32_t rtt = T2M(curTime - reply->txTime);
  RTT_COUNT[neighborAddress]++;
  AVG_RTT[neighborAddress] = AVG_RTT[neighborAddress] + (rtt - AVG_RTT[neighborAddress]) * 1.0 / RTT_COUNT[neighborAddress];
  PDR[neighborAddress] = reply->PDR;
}

static void benchTxTask(void *parameters) {
  systemWaitStart();
  srand(uwbGetAddress());

  while (true) {
    for (UWB_Address_t neighbor = 0; neighbor <= NEIGHBOR_ADDRESS_MAX; neighbor++) {
      if (neighbor == uwbGetAddress()) {
        continue;
      }
      sendCount[neighbor]++;
      totalSendCount++;
      sendMockDataRequest(neighbor);
      vTaskDelay(M2T(5 + random() % 15));
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
      UWB_Address_t neighborAddress = dataRxPacket.header.srcAddress;
      totalRecvCount++;
      switch (msgType) {
        case MOCK_DATA_REQUEST:
//          DEBUG_PRINT("%u received mock data request from %u.\n",
//                                           uwbGetAddress(),
//                                           dataRxPacket.header.srcAddress);
          recvCount[neighborAddress]++;
          processMockDataRequest(dataRxPacket.header.srcAddress,
                                 (Mock_Data_Request_t *) dataRxPacket.payload);
          break;
        case MOCK_DATA_REPLY:
//          DEBUG_PRINT("%u received mock data reply from %u.\n",
//                                         uwbGetAddress(),
//                                         dataRxPacket.header.srcAddress);
          processMockDataReply(dataRxPacket.header.srcAddress,
                               (Mock_Data_Reply_t *) dataRxPacket.payload);
          break;
        default:
          DEBUG_PRINT("%u received unknown mock data type = %u from %u.\n",
                            uwbGetAddress(), msgType,
                            dataRxPacket.header.srcAddress);
      }
    }
    vTaskDelay(M2T(1));
  }

}

static void printTimerCallback(TimerHandle_t timer) {
  double totalRTT = 0.0;
  double totalPDR = 0.0;
  uint16_t count = 0;
  DEBUG_PRINT("neighbor\t AVG_RTT \t PDR\n");
  for (UWB_Address_t neighbor = 0; neighbor <= NEIGHBOR_ADDRESS_MAX; neighbor++) {
    if (neighbor == uwbGetAddress() || recvCount[neighbor] == 0) {
      continue;
    }
    DEBUG_PRINT("%u\t %.2f\t %.2f\t\n", neighbor, AVG_RTT[neighbor], PDR[neighbor]);
    totalRTT += AVG_RTT[neighbor];
    totalPDR += PDR[neighbor];
    count++;
  }
  DEBUG_PRINT("%u total send = %lu, total recv = %lu, total avg rtt = %.2f, total avg pdr = %.2f.\n\n",
              uwbGetAddress(), totalSendCount, totalRecvCount, totalRTT / count, totalPDR / count);
}

void routingBenchInit() {
  rxQueue = xQueueCreate(ROUTING_BENCH_RX_QUEUE_SIZE, ROUTING_BENCH_RX_QUEUE_ITEM_SIZE);
  UWB_Data_Packet_Listener_t listener = {
      .type = UWB_DATA_MESSAGE_RESERVED,
      .rxQueue = rxQueue
  };
  uwbRegisterDataPacketListener(&listener);

  printTimer = xTimerCreate("printTimer",
                            M2T(5000),
                            pdTRUE,
                            (void *) 0,
                            printTimerCallback);
  xTimerStart(printTimer, M2T(0));

  xTaskCreate(benchTxTask, "ADHOC_ROUTING_BENCH_TX", UWB_TASK_STACK_SIZE, NULL,
              ADHOC_DECK_TASK_PRI, &benchTxTaskHandle);

  xTaskCreate(benchRxTask, "ADHOC_ROUTING_BENCH_RX", UWB_TASK_STACK_SIZE, NULL,
              ADHOC_DECK_TASK_PRI, &benchRxTaskHandle);
}