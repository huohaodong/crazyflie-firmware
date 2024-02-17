#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "autoconf.h"
#include "debug.h"
#include "system.h"
#include "timers.h"
#include "olsr.h"
#include "routing.h"

#ifndef OLSR_DEBUG_ENABLE
#undef DEBUG_PRINT
#define DEBUG_PRINT
#endif

static TaskHandle_t olsrRxTaskHandle;
static QueueHandle_t rxQueue;
static SemaphoreHandle_t olsrSetsMutex;
static Routing_Table_t *routingTable;
static Neighbor_Set_t *neighborSet;
static MPR_Set_t mprSet;
static TimerHandle_t olsrTcTimer;

static void olsrTcTimerCallback(TimerHandle_t timer) {
  Time_t curTime = xTaskGetTickCount();
//  DEBUG_PRINT("olsrHelloTimerCallback: send tc at %lu.\n", curTime);
  // TODO: send TC
}

void mprSetInit(MPR_Set_t *set) {
  neighborBitSetInit(set);
}

void mprSetAdd(MPR_Set_t *set, UWB_Address_t neighborAddress) {
  neighborBitSetAdd(set, neighborAddress);
}

void mprSetRemove(MPR_Set_t *set, UWB_Address_t neighborAddress) {
  neighborBitSetRemove(set, neighborAddress);
}

bool mprSetHas(MPR_Set_t *set, UWB_Address_t neighborAddress) {
  return neighborBitSetHas(set, neighborAddress);
}

void mprSetClear(MPR_Set_t *set) {
  neighborBitSetClear(set);
}

static void computeMPR(Neighbor_Set_t *set) {
  // TODO
}

void olsrRxCallback(void *parameters) {
//  DEBUG_PRINT("olsrRxCallback\n");
}

void olsrTxCallback(void *parameters) {
//  DEBUG_PRINT("olsrTxCallback\n");
}

static void olsrRxTask(void *parameters) {
  systemWaitStart();

  UWB_Packet_t rxPacketCache;

  while (true) {
    if (uwbReceivePacketBlock(UWB_OLSR_MESSAGE, &rxPacketCache)) {
      xSemaphoreTake(olsrSetsMutex, portMAX_DELAY);
      xSemaphoreTake(routingTable->mu, portMAX_DELAY);

      // TODO: process TC

      xSemaphoreGive(routingTable->mu);
      xSemaphoreGive(olsrSetsMutex);
    }
    vTaskDelay(M2T(1));
  }

}

void olsrInit() {
  rxQueue = xQueueCreate(OLSR_RX_QUEUE_SIZE, OLSR_RX_QUEUE_ITEM_SIZE);
  olsrSetsMutex = xSemaphoreCreateMutex();
  routingTable = getGlobalRoutingTable();
  neighborSet = getGlobalNeighborSet();
  mprSetInit(&mprSet);

  olsrTcTimer = xTimerCreate("olsrTcTimer",
                             M2T(OLSR_TC_INTERVAL),
                             pdTRUE,
                             (void *) 0,
                             olsrTcTimerCallback);
  xTimerStart(olsrTcTimer, M2T(0));

  UWB_Message_Listener_t listener;
  listener.type = UWB_OLSR_MESSAGE;
  listener.rxQueue = rxQueue;
  listener.rxCb = olsrRxCallback;
  listener.txCb = olsrTxCallback;
  uwbRegisterListener(&listener);

  xTaskCreate(olsrRxTask,
              ADHOC_DECK_OLSR_RX_TASK_NAME,
              UWB_TASK_STACK_SIZE,
              NULL,
              ADHOC_DECK_TASK_PRI,
              &olsrRxTaskHandle);
}