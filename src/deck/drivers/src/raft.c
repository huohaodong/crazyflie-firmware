#include "FreeRTOS.h"
#include "raft.h"
#include "debug.h"

#ifndef RAFT_DEBUG_ENABLE
  #undef DEBUG_PRINT
  #define DEBUG_PRINT
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static QueueHandle_t rxQueue;
static TaskHandle_t raftRxTaskHandle;
static Raft_Node_t raftNode;
static Raft_Log_Item_t EMPTY_LOG_ITEM = {
    .term = 0,
    .index = 0,
    .command = "NULL"
};

static void raftRxTask() {
  UWB_Data_Packet_t dataRxPacket;
  while (1) {
    // TODO: handlers
    if (uwbReceiveDataPacketBlock(UWB_DATA_MESSAGE_RAFT, &dataRxPacket)) {
      uint8_t type = dataRxPacket.payload[0];
      switch (type) {
        case RAFT_REQUEST_VOTE:
        case RAFT_REQUEST_VOTE_REPLY:
        case RAFT_APPEND_ENTRIES:
        case RAFT_APPEND_ENTRIES_REPLY:
        default:DEBUG_PRINT("raftRxTask: %u received unknown raft message type = %u.\n",
                            uwbGetAddress(),
                            type);
      }
    }
    vTaskDelay(M2T(1));
  }
}

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
  UWB_Data_Packet_Listener_t listener = {
      .type = UWB_DATA_MESSAGE_RAFT,
      .rxQueue = rxQueue
  };
  uwbRegisterDataPacketListener(&listener);

  xTaskCreate(raftRxTask, ADHOC_DECK_RAFT_RX_TASK_NAME, UWB_TASK_STACK_SIZE, NULL,
              ADHOC_DECK_TASK_PRI, &raftRxTaskHandle);
}

void raftSendRequestVote(UWB_Address_t address, Raft_Request_Vote_Args_t *args) {
//  TODO
}

void raftProcessRequestVote(Raft_Request_Vote_Args_t *args) {
//  TODO
}

void raftProcessRequestVoteReply(Raft_Request_Vote_Reply_t *reply) {
//  TODO
}

void raftSendAppendEntries(UWB_Address_t address, Raft_Append_Entries_Args_t *args) {
//  TODO
}

void raftProcessAppendEntries(Raft_Append_Entries_Reply_t *args) {
//  TODO
}

void raftProcessAppendEntriesReply(Raft_Append_Entries_Reply_t *reply) {
//  TODO
}
