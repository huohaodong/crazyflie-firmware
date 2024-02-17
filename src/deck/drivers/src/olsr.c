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
static Routing_Table_t *routingTable;
static TimerHandle_t olsrTcTimer;

static void olsrTcTimerCallback(TimerHandle_t timer) {
  Time_t curTime = xTaskGetTickCount();
  DEBUG_PRINT("olsrHelloTimerCallback: send tc at %lu.\n", curTime);
  // TODO: send TC
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
      xSemaphoreTake(routingTable->mu, portMAX_DELAY);
      // TODO: process TC
      xSemaphoreGive(routingTable->mu);
    }
    vTaskDelay(M2T(1));
  }

}

void olsrInit() {
  rxQueue = xQueueCreate(OLSR_RX_QUEUE_SIZE, OLSR_RX_QUEUE_ITEM_SIZE);
  routingTable = getGlobalRoutingTable();
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