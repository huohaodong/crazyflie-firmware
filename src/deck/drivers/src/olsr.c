#include <math.h>
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

static void computeMPR() {
  /* 1. Clear previous computed mpr set. */
  mprSetClear(&mprSet);
  Neighbor_Bit_Set_t coverSet;
  neighborBitSetInit(&coverSet);

  /* 2. Add all symmetric one-hop neighbors that provide reachability to symmetric two-hop neighbors that are not yet covered. */
  for (UWB_Address_t twoHopNeighbor = 0; twoHopNeighbor <= NEIGHBOR_ADDRESS_MAX; twoHopNeighbor++) {
    if (!neighborBitSetHas(&coverSet, twoHopNeighbor) && neighborSet->twoHopReachSets[twoHopNeighbor].size == 1) {
      UWB_Address_t oneHopNeighbor = log2(neighborSet->twoHopReachSets[twoHopNeighbor].bits);
      mprSetAdd(&mprSet, oneHopNeighbor);
      neighborBitSetAdd(&coverSet, twoHopNeighbor);
    }
    if (coverSet.size == neighborSet->twoHop.size) {
      break;
    }
  }

  /* 3.


  // TODO: compute
}

static void olsrTcTimerCallback(TimerHandle_t timer) {
  // TODO: send TC
}

// TODO: check invocation
void olsrNeighborTopologyChangeHook(UWB_Address_t neighborAddress) {
  if (mprSetHas(&mprSet, neighborAddress)) {
    xSemaphoreTake(olsrSetsMutex, portMAX_DELAY);
    xSemaphoreTake(neighborSet->mu, portMAX_DELAY);
    computeMPR();
    printMPRSet(&mprSet);
    xSemaphoreGive(neighborSet->mu);
    xSemaphoreGive(olsrSetsMutex);
  }
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

void printMPRSet(MPR_Set_t *set) {
  DEBUG_PRINT("%u has %u mpr neighbors = ", uwbGetAddress(), set->size);
  for (int neighborAddress = 0; neighborAddress <= NEIGHBOR_ADDRESS_MAX; neighborAddress++) {
    if (mprSetHas(set, neighborAddress)) {
      DEBUG_PRINT("%u ", neighborAddress);
    }
  }
  DEBUG_PRINT("\n");
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
      xSemaphoreTake(neighborSet->mu, portMAX_DELAY);
      xSemaphoreTake(routingTable->mu, portMAX_DELAY);

      // TODO: process TC

      xSemaphoreGive(routingTable->mu);
      xSemaphoreGive(neighborSet->mu);
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
  neighborSetRegisterTopologyChangeHook(neighborSet, olsrNeighborTopologyChangeHook);
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