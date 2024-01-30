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

static void convertToFollower(Raft_Node_t *node) {
  node->currentState = RAFT_STATE_FOLLOWER;
  node->voteFor = RAFT_VOTE_FOR_NO_ONE;
  for (int i = 0; i < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; i++) {
    node->peerVote[i] = false;
  }
  node->lastHeartbeatTime = xTaskGetTickCount();
}

static void raftRxTask() {
  UWB_Data_Packet_t dataRxPacket;
  while (1) {
    // TODO: handlers
    if (uwbReceiveDataPacketBlock(UWB_DATA_MESSAGE_RAFT, &dataRxPacket)) {
      uint8_t type = dataRxPacket.payload[0];
      switch (type) {
        case RAFT_REQUEST_VOTE:raftProcessRequestVote((Raft_Request_Vote_Args_t *) dataRxPacket.payload);
        case RAFT_REQUEST_VOTE_REPLY: raftProcessRequestVoteReply((Raft_Request_Vote_Reply_t *) dataRxPacket.payload);
        case RAFT_APPEND_ENTRIES: raftProcessAppendEntries((Raft_Append_Entries_Args_t *) dataRxPacket.payload);
        case RAFT_APPEND_ENTRIES_REPLY:raftProcessAppendEntriesReply((Raft_Append_Entries_Reply_t *) dataRxPacket.payload);
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
  for (int i = 0; i < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; i++) {
    raftNode.peerVote[i] = false;
  }
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
  DEBUG_PRINT("raftSendRequestVote: %u send request vote to %u.\n", raftNode.me, address);
  uwbSendDataPacketBlock(&dataTxPacket);
}

// TODO: check
void raftProcessRequestVote(Raft_Request_Vote_Args_t *args) {
  DEBUG_PRINT("raftProcessRequestVote: %u received vote request from %u.\n", raftNode.me, args->candidateId);
  if (args->term < raftNode.currentTerm) {
    DEBUG_PRINT("raftProcessRequestVote: Candidate term = %u < my term = %u, ignore.\n",
                args->term,
                raftNode.currentTerm);
    raftSendRequestVoteReply(args->candidateId, raftNode.currentTerm, false);
    return;
  }
  if (args->term > raftNode.currentTerm) {
    DEBUG_PRINT("raftProcessRequestVote: Candidate term = %u > my term = %u, convert to follower.\n",
                args->term,
                raftNode.currentTerm
    );
    raftNode.currentTerm = args->term;
    convertToFollower(&raftNode);
  }
  if (raftNode.voteFor != RAFT_VOTE_FOR_NO_ONE && raftNode.voteFor != args->candidateId) {
    DEBUG_PRINT("raftProcessRequestVote: I %u have already granted vote to %u this term, don't grant vote.\n",
                raftNode.me,
                raftNode.voteFor);
    raftSendRequestVoteReply(args->candidateId, raftNode.currentTerm, false);
    return;
  }
  Raft_Log_Item_t lastLog = raftNode.log.items[raftNode.log.size - 1];
  if (args->lastLogTerm < lastLog.term || (args->lastLogTerm == lastLog.term && args->lastLogIndex < lastLog.index)) {
    DEBUG_PRINT("raftProcessRequestVote: My %u local log entries are more up-to-date than %u, don't grant vote.\n",
                raftNode.me,
                args->candidateId);
    raftSendRequestVoteReply(args->candidateId, raftNode.currentTerm, false);
    return;
  }
  raftNode.voteFor = args->candidateId;
  raftNode.lastHeartbeatTime = xTaskGetTickCount();
  DEBUG_PRINT("raftProcessRequestVote: %u grant vote to %u.\n", raftNode.me, args->candidateId);
  raftSendRequestVoteReply(args->candidateId, raftNode.currentTerm, true);
}

void raftSendRequestVoteReply(UWB_Address_t address, uint16_t term, bool voteGranted) {
  UWB_Data_Packet_t dataTxPacket;
  dataTxPacket.header.type = UWB_DATA_MESSAGE_RAFT;
  dataTxPacket.header.srcAddress = raftNode.me;
  dataTxPacket.header.destAddress = address;
  dataTxPacket.header.ttl = 10;
  dataTxPacket.header.length = sizeof(UWB_Data_Packet_Header_t) + sizeof(Raft_Request_Vote_Reply_t);
  Raft_Request_Vote_Reply_t *reply = (Raft_Request_Vote_Reply_t *) &dataTxPacket.payload;
  reply->type = RAFT_REQUEST_VOTE_REPLY;
  reply->term = term;
  reply->voteGranted = voteGranted;
  DEBUG_PRINT("raftSendRequestVoteReply: %u send vote reply to %u, term = %u, granted = %d.\n",
              raftNode.me,
              address,
              term,
              voteGranted);
  uwbSendDataPacketBlock(&dataTxPacket);
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
  DEBUG_PRINT("raftSendAppendEntries: %u send append entries to %u.\n", raftNode.me, address);
  uwbSendDataPacketBlock(&dataTxPacket);
}

void raftProcessAppendEntries(Raft_Append_Entries_Args_t *args) {
//  TODO
}

void raftSendAppendEntriesReply(UWB_Address_t address, uint16_t term, bool success) {
//  TODO
}

void raftProcessAppendEntriesReply(Raft_Append_Entries_Reply_t *reply) {
//  TODO
}
