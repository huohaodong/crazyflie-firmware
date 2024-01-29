#include "FreeRTOS.h"
#include "raft.h"

#ifndef RAFT_DEBUG_ENABLE
  #undef DEBUG_PRINT
  #define DEBUG_PRINT
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static QueueHandle_t rxQueue;
static Raft_Node_t raftNode;
static Raft_Log_Item_t EMPTY_LOG_ITEM = {
    .term = 0,
    .index = 0,
    .command = "NULL"
};

void raftInit() {
  xSemaphoreCreateMutex();
  rxQueue = xQueueCreate(RAFT_RX_QUEUE_SIZE, RAFT_RX_QUEUE_ITEM_SIZE);
  raftNode.mu = xSemaphoreCreateMutex();
//  raftNode.peerNodes = ? TODO: init peer
  raftNode.voteCount = 0;
  raftNode.currentState = RAFT_STATE_FOLLOWER;
  raftNode.currentTerm = 0;
  raftNode.voteFor = RAFT_VOTE_FOR_NO_ONE;
  raftNode.log.items[0] = EMPTY_LOG_ITEM;
  raftNode.log.size = 1;
  raftNode.commitIndex = 0;
  raftNode.lastApplied = 0;
  for (int i = 0; i < RAFT_CLUSTER_PEER_NODE_COUNT_MAX; i++) {
    raftNode.nextIndex[i] = raftNode.log.items[raftNode.log.size - 1].index + 1;
    raftNode.matchIndex[i] = 0;
  }
  raftNode.lastHeartbeatTime = xTaskGetTickCount();
//  TODO: election timer
//  TODO: heartbeat timer
//  TODO: log applier timer
}
