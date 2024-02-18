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
static MPR_Selector_Set_t mprSelectorSet;
static TimerHandle_t mprSelectorSetEvictionTimer;
static TimerHandle_t olsrTcTimer;

static void computeMPR() {
  /* 1. Clear previous computed mpr set. */
  mprSetClear(&mprSet);
  Neighbor_Bit_Set_t coverSet;
  neighborBitSetInit(&coverSet);

  /* 2. Add all symmetric one-hop neighbors that provide reachability to symmetric two-hop neighbors that are not yet covered. */
  for (UWB_Address_t twoHopNeighbor = 0; twoHopNeighbor <= NEIGHBOR_ADDRESS_MAX; twoHopNeighbor++) {
    if (coverSet.size == neighborSet->twoHop.size) {
      break;
    }
    if (!neighborSetHasTwoHop(neighborSet, twoHopNeighbor)) {
      continue;
    }
    if (!neighborBitSetHas(&coverSet, twoHopNeighbor) && neighborSet->twoHopReachSets[twoHopNeighbor].size == 1) {
      UWB_Address_t onlyOneHopNeighbor = (UWB_Address_t) log2(neighborSet->twoHopReachSets[twoHopNeighbor].bits);
      DEBUG_PRINT("computeMPR: onlyOneHopNeighbor to %u = %u.\n", twoHopNeighbor, onlyOneHopNeighbor);
      mprSetAdd(&mprSet, onlyOneHopNeighbor);
      neighborBitSetAdd(&coverSet, twoHopNeighbor);
    }
  }
  uint8_t remainUncoveredCount = neighborSet->twoHop.size - coverSet.size;
  /* 3. Add all symmetric one-hop neighbors that covers most uncovered two-hop neighbors. */
  for (UWB_Address_t round = 0; round <= remainUncoveredCount; round++) {
    if (coverSet.size == neighborSet->twoHop.size) {
      break;
    }
    /* 3.1 Collect reach counts of one-hop neighbors according to the number of uncovered two-hop neighbors. */
    uint8_t reachCount[NEIGHBOR_ADDRESS_MAX + 1] = {[0 ... NEIGHBOR_ADDRESS_MAX] = 0};
    for (UWB_Address_t twoHopNeighbor = 0; twoHopNeighbor <= NEIGHBOR_ADDRESS_MAX; twoHopNeighbor++) {
      if (!neighborSetHasTwoHop(neighborSet, twoHopNeighbor)) {
        continue;
      }
      if (!neighborBitSetHas(&coverSet, twoHopNeighbor)) {
        for (UWB_Address_t oneHopNeighbor = 0; oneHopNeighbor <= NEIGHBOR_ADDRESS_MAX; oneHopNeighbor++) {
          if (!neighborSetHasOneHop(neighborSet, oneHopNeighbor)) {
            continue;
          }
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
      if (!neighborSetHasOneHop(neighborSet, oneHopNeighbor)) {
        continue;
      }
      if (reachCount[oneHopNeighbor] > mostCount) {
        mostOneHopNeighbor = oneHopNeighbor;
        mostCount = reachCount[oneHopNeighbor];
      }
    }
    /* 3.3 Add this one-hop neighbor to mpr set and then update cover set. */
    mprSetAdd(&mprSet, mostOneHopNeighbor);
    for (UWB_Address_t twoHopNeighbor = 0; twoHopNeighbor <= NEIGHBOR_ADDRESS_MAX; twoHopNeighbor++) {
      if (!neighborSetHasTwoHop(neighborSet, twoHopNeighbor)) {
        continue;
      }
      if (!neighborBitSetHas(&coverSet, twoHopNeighbor)
          && neighborSetHasRelation(neighborSet, mostOneHopNeighbor, twoHopNeighbor)) {
        neighborBitSetAdd(&coverSet, twoHopNeighbor);
      }
    }
  }

  if (coverSet.size == neighborSet->twoHop.size && coverSet.bits == neighborSet->twoHop.bits) {
    DEBUG_PRINT("computeMPR: covered all %u two-hop neighbors.\n", neighborSet->twoHop.size);
    printNeighborBitSet(&coverSet);
  } else {
    DEBUG_PRINT("computeMPR: cannot covered all %u two-hop neighbors, now covers %u.\n",
                neighborSet->twoHop.size,
                coverSet.size);
    printNeighborBitSet(&coverSet);
  }
}

static void olsrTcTimerCallback(TimerHandle_t timer) {
  // TODO: send TC
  xSemaphoreTake(olsrSetsMutex, portMAX_DELAY);
  xSemaphoreTake(neighborSet->mu, portMAX_DELAY);
  printNeighborSet(neighborSet);
  printMPRSet(&mprSet);
  xSemaphoreGive(neighborSet->mu);
  xSemaphoreGive(olsrSetsMutex);
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

void mprSelectorSetInit(MPR_Selector_Set_t *set) {
  neighborBitSetInit(&set->mprSelectors);
  for (UWB_Address_t neighborAddress = 0; neighborAddress <= NEIGHBOR_ADDRESS_MAX; neighborAddress++) {
    set->expirationTime[neighborAddress] = 0;
  }
}

void mprSelectorSetAdd(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress) {
  if (!neighborBitSetHas(&set->mprSelectors, neighborAddress)) {
    neighborBitSetAdd(&set->mprSelectors, neighborAddress);
  }
}

void mprSelectorSetRemove(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress) {
  if (neighborBitSetHas(&set->mprSelectors, neighborAddress)) {
    neighborBitSetRemove(&set->mprSelectors, neighborAddress);
  }
}

bool mprSelectorSetHas(MPR_Selector_Set_t *set, UWB_Address_t neighborAddress) {
  return neighborBitSetHas(&set->mprSelectors, neighborAddress);
}

int mprSelectorSetClearExpire(MPR_Selector_Set_t *set) {
  Time_t curTime = xTaskGetTickCount();
  int evictionCount = 0;
  for (UWB_Address_t neighborAddress = 0; neighborAddress <= NEIGHBOR_ADDRESS_MAX; neighborAddress++) {
    if (mprSelectorSetHas(set, neighborAddress) && set->expirationTime[neighborAddress] <= curTime) {
      evictionCount++;
      mprSelectorSetRemove(set, neighborAddress);
      DEBUG_PRINT("mprSelectorSetClearExpire: mpr selector %u expire at %lu.\n", neighborAddress, curTime);
    }
  }
  return evictionCount;
}

static void mprSelectorSetClearExpireTimerCallback(TimerHandle_t timer) {
  xSemaphoreTake(olsrSetsMutex, portMAX_DELAY);

  Time_t curTime = xTaskGetTickCount();
  DEBUG_PRINT("mprSelectorSetClearExpireTimerCallback: Trigger expiration timer at %lu.\n", curTime);

  int evictionCount = mprSelectorSetClearExpire(&mprSelectorSet);
  if (evictionCount > 0) {
    DEBUG_PRINT("mprSelectorSetClearExpireTimerCallback: Evict total %d mpr selectors.\n", evictionCount);
  } else {
    DEBUG_PRINT("mprSelectorSetClearExpireTimerCallback: Evict none.\n");
  }

  xSemaphoreGive(olsrSetsMutex);
}

void printMPRSet(MPR_Set_t *set) {
  DEBUG_PRINT("%u has %u mpr neighbors = ", uwbGetAddress(), set->size);
  for (UWB_Address_t neighborAddress = 0; neighborAddress <= NEIGHBOR_ADDRESS_MAX; neighborAddress++) {
    if (mprSetHas(set, neighborAddress)) {
      DEBUG_PRINT("%u ", neighborAddress);
    }
  }
  DEBUG_PRINT("\n");
}

void printMPRSelectorSet(MPR_Selector_Set_t *set) {
  DEBUG_PRINT("%u has %u mpr selectors = ", uwbGetAddress(), set->mprSelectors.size);
  for (UWB_Address_t neighborAddress = 0; neighborAddress <= NEIGHBOR_ADDRESS_MAX; neighborAddress++) {
    if (mprSelectorSetHas(set, neighborAddress)) {
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
  mprSelectorSetInit(&mprSelectorSet);
  mprSelectorSetEvictionTimer = xTimerCreate("mprSelectorSetEvictionTimer",
                                             M2T(OLSR_MPR_SELECTOR_SET_HOLD_TIME / 2),
                                             pdTRUE,
                                             (void *) 0,
                                             mprSelectorSetClearExpireTimerCallback);
  xTimerStart(mprSelectorSetEvictionTimer, M2T(0));
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