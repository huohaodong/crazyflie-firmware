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
                            raftNode.me,
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
  raftNode.me = uwbGetAddress();
//  raftNode.peerNodes = ? TODO: init peer
  raftNode.voteCount = 0;
  raftNode.currentState = RAFT_STATE_FOLLOWER;
  raftNode.currentTerm = 0;
  raftNode.voteFor = RAFT_VOTE_FOR_NO_ONE;
  raftNode.log.items[0] = EMPTY_LOG_ITEM;
  raftNode.log.size = 1;
  raftNode.commitIndex = 0;
  raftNode.lastApplied = 0;
  for (int i = 0; i < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; i++) {
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

// TODO: leader broadcasting, follower uni-casting
void raftSendRequestVote(UWB_Address_t address) {
  UWB_Data_Packet_t dataTxPacket;
  dataTxPacket.header.type = UWB_DATA_MESSAGE_RAFT;
  dataTxPacket.header.srcAddress = raftNode.me;
  dataTxPacket.header.destAddress = address;
  dataTxPacket.header.ttl = 10;
  dataTxPacket.header.length = sizeof(UWB_Data_Packet_Header_t) + sizeof(Raft_Request_Vote_Args_t);
  Raft_Request_Vote_Args_t *args = (Raft_Request_Vote_Args_t *) &dataTxPacket.payload;
  args->type = RAFT_REQUEST_VOTE;
  args->term = raftNode.currentTerm;
  args->candidateId = raftNode.me;
  args->lastLogIndex = raftNode.log.items[raftNode.log.size - 1].index;
  args->lastLogTerm = raftNode.log.items[raftNode.log.size - 1].term;
  uwbSendDataPacketBlock(&dataTxPacket);
}

void raftProcessRequestVote(Raft_Request_Vote_Args_t *args) {
//  TODO
}

void raftProcessRequestVoteReply(Raft_Request_Vote_Reply_t *reply) {
//  TODO
}

void raftSendAppendEntries(UWB_Address_t address) {
  UWB_Data_Packet_t dataTxPacket;
  dataTxPacket.header.type = UWB_DATA_MESSAGE_RAFT;
  dataTxPacket.header.srcAddress = raftNode.me;
  dataTxPacket.header.destAddress = address;
  dataTxPacket.header.ttl = 10;
  dataTxPacket.header.length = sizeof(UWB_Data_Packet_Header_t) + sizeof(Raft_Append_Entries_Args_t);
  Raft_Append_Entries_Args_t *args = (Raft_Append_Entries_Args_t *) &dataTxPacket.payload;
  args->type = RAFT_APPEND_ENTRIES;
  args->term = raftNode.currentTerm;
  args->leaderId = raftNode.me;
  args->prevLogIndex = raftNode.log.items[raftNode.nextIndex[address] - 1].index;
  args->prevLogTerm = raftNode.log.items[raftNode.nextIndex[address] - 1].term;
  args->entryCount = 0;
  // TODO: check
  int startIndex = MAX(0, raftNode.nextIndex[address] - RAFT_LOG_ENTRIES_SIZE_MAX);
  for (int i = startIndex; i < raftNode.nextIndex[address]; i++) {
    args->entries[args->entryCount] = raftNode.log.items[i];
    args->entryCount++;
  }
  args->leaderCommit = raftNode.commitIndex;
  uwbSendDataPacketBlock(&dataTxPacket);
}

void raftProcessAppendEntries(Raft_Append_Entries_Reply_t *args) {
//  TODO
}

void raftProcessAppendEntriesReply(Raft_Append_Entries_Reply_t *reply) {
//  TODO
}
