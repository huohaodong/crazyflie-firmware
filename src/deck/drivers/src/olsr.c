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
  DEBUG_PRINT("computeMPR.\n");
  /* 1. Clear previous computed mpr set. */
  mprSetClear(&mprSet);
  Neighbor_Bit_Set_t coverSet;
  neighborBitSetInit(&coverSet);

  /* 2. Add all symmetric one-hop neighbors that provide reachability to symmetric two-hop neighbors that are not yet covered. */
  for (UWB_Address_t twoHopNeighbor = 0; twoHopNeighbor <= NEIGHBOR_ADDRESS_MAX; twoHopNeighbor++) {
    if (coverSet.size == neighborSet->twoHop.size) {
      break;
    }
    if (!neighborBitSetHas(&coverSet, twoHopNeighbor) && neighborSet->twoHopReachSets[twoHopNeighbor].size == 1) {
      UWB_Address_t onlyOneHopNeighbor = log2(neighborSet->twoHopReachSets[twoHopNeighbor].bits);
      mprSetAdd(&mprSet, onlyOneHopNeighbor);
      neighborBitSetAdd(&coverSet, twoHopNeighbor);
    }
  }

  /* 3. Add all symmetric one-hop neighbors that covers most uncovered two-hop neighbors. */
  for (UWB_Address_t round = 0; round <= NEIGHBOR_ADDRESS_MAX; round++) {
    if (coverSet.size == neighborSet->twoHop.size) {
      break;
    }
    /* 3.1 Collect reach counts of one-hop neighbors according to the number of uncovered two-hop neighbors. */
    uint8_t reachCount[NEIGHBOR_ADDRESS_MAX + 1] = {[0 ... NEIGHBOR_ADDRESS_MAX] = 0};
    for (UWB_Address_t twoHopNeighbor = 0; twoHopNeighbor <= NEIGHBOR_ADDRESS_MAX; twoHopNeighbor++) {
      if (!neighborBitSetHas(&coverSet, twoHopNeighbor)) {
        for (UWB_Address_t oneHopNeighbor = 0; oneHopNeighbor <= NEIGHBOR_ADDRESS_MAX; oneHopNeighbor++) {
          if (neighborSetHasRelation(neighborSet, oneHopNeighbor, twoHopNeighbor)) {
            reachCount[oneHopNeighbor]++;
          }
        }
      }
    }
    /* 3.2 Find the one-hop neighbor that covers most uncovered two-hop neighbors in this round. */
    UWB_Address_t mostOneHopNeighbor = 0;
    uint8_t mostCount = 0;
    for (UWB_Address_t oneHopNeighbor = 0; oneHopNeighbor <= NEIGHBOR_ADDRESS_MAX; oneHopNeighbor++) {
      if (reachCount[oneHopNeighbor] > mostCount) {
        mostOneHopNeighbor = oneHopNeighbor;
        mostCount = reachCount[oneHopNeighbor];
      }
    }
    /* 3.3 Add this one-hop neighbor to mpr set and then update cover set. */
    mprSetAdd(&mprSet, mostOneHopNeighbor);
    for (UWB_Address_t twoHopNeighbor = 0; twoHopNeighbor <= NEIGHBOR_ADDRESS_MAX; twoHopNeighbor++) {
      if (!neighborBitSetHas(&coverSet, twoHopNeighbor)
          && neighborSetHasRelation(neighborSet, mostOneHopNeighbor, twoHopNeighbor)) {
        neighborBitSetAdd(&coverSet, twoHopNeighbor);
      }
    }
  }

  if (coverSet.size == neighborSet->twoHop.size) {
    DEBUG_PRINT("computeMPR: covered all %u two-hop neighbors.\n", neighborSet->twoHop.size);
  } else {
    DEBUG_PRINT("computeMPR: cannot covered all %u two-hop neighbors, now covers %u.\n",
                neighborSet->twoHop.size,
                coverSet.size);
  }
  printMPRSet(&mprSet);
}

static void olsrTcTimerCallback(TimerHandle_t timer) {
  // TODO: send TC
}

// TODO: check invocation
void olsrNeighborTopologyChangeHook(UWB_Address_t neighborAddress) {
  xSemaphoreTake(olsrSetsMutex, portMAX_DELAY);
  computeMPR();
  xSemaphoreGive(olsrSetsMutex);
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