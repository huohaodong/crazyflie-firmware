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
// TODO: check
static int raftLogFindByIndex(Raft_Log_t *raftLog, uint16_t logIndex) {
  for (int i = raftLog->size - 1; i >= 0; i--) {
    if (raftLog->items[i].index == logIndex) {
      return i;
    }
  }
  return -1;
}
// TODO: check
static int raftLogFindMatched(Raft_Log_t *raftLog, uint16_t logIndex, uint16_t logTerm) {
  for (int i = raftLog->size - 1; i >= 0; i--) {
    if (raftLog->items[i].index == logIndex) {
      if (raftLog->items[i].term == logTerm) {
        return i;
      }
      break;
    }
  }
  return -1;
}
// TODO: check
static void raftLogCleanFrom(Raft_Log_t *raftLog, uint16_t itemStartIndex) {
  ASSERT(itemStartIndex >= 0);
  for (int i = itemStartIndex; i < raftLog->size; i++) {
    raftLog->items[i] = EMPTY_LOG_ITEM;
  }
  raftLog->size = itemStartIndex;
}

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
      xSemaphoreTake(raftNode.mu, portMAX_DELAY);
      UWB_Address_t peer = dataRxPacket.header.srcAddress;
      uint8_t type = dataRxPacket.payload[0];
      switch (type) {
        case RAFT_REQUEST_VOTE:
          raftProcessRequestVote(peer, (Raft_Request_Vote_Args_t *) dataRxPacket.payload);
        case RAFT_REQUEST_VOTE_REPLY:
          raftProcessRequestVoteReply(peer, (Raft_Request_Vote_Reply_t *) dataRxPacket.payload);
        case RAFT_APPEND_ENTRIES:
          raftProcessAppendEntries(peer, (Raft_Append_Entries_Args_t *) dataRxPacket.payload);
        case RAFT_APPEND_ENTRIES_REPLY:
          raftProcessAppendEntriesReply(peer, (Raft_Append_Entries_Reply_t *) dataRxPacket.payload);
        default:
          DEBUG_PRINT("raftRxTask: %u received unknown raft message type = %u.\n",
                            raftNode.me,
                            type);
      }
      xSemaphoreGive(raftNode.mu);
    }
    vTaskDelay(M2T(1));
  }
}

void raftInit() {
  xSemaphoreCreateMutex();
  rxQueue = xQueueCreate(RAFT_RX_QUEUE_SIZE, RAFT_RX_QUEUE_ITEM_SIZE);
  raftNodeInit(&raftNode);
  UWB_Data_Packet_Listener_t listener = {
      .type = UWB_DATA_MESSAGE_RAFT,
      .rxQueue = rxQueue
  };
  uwbRegisterDataPacketListener(&listener);

  xTaskCreate(raftRxTask, ADHOC_DECK_RAFT_RX_TASK_NAME, UWB_TASK_STACK_SIZE, NULL,
              ADHOC_DECK_TASK_PRI, &raftRxTaskHandle);
}

void raftNodeInit(Raft_Node_t *node) {
  node->mu = xSemaphoreCreateMutex();
  node->me = uwbGetAddress();
//  node.peerNodes = ? TODO: init peer
  for (int i = 0; i < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; i++) {
    node->peerVote[i] = false;
  }
  node->currentState = RAFT_STATE_FOLLOWER;
  node->currentTerm = 0;
  node->voteFor = RAFT_VOTE_FOR_NO_ONE;
  node->log.items[0] = EMPTY_LOG_ITEM;
  node->log.size = 1;
  node->commitIndex = 0;
  node->lastApplied = 0;
  for (int i = 0; i < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; i++) {
    node->nextIndex[i] = node->log.items[node->log.size - 1].index + 1;
    node->matchIndex[i] = 0;
  }
  raftNode.lastHeartbeatTime = xTaskGetTickCount();
//  TODO: election timer
//  TODO: heartbeat timer
//  TODO: log applier timer
}

// TODO: leader broadcasting, follower uni-casting
void raftSendRequestVote(UWB_Address_t peerAddress) {
  UWB_Data_Packet_t dataTxPacket;
  dataTxPacket.header.type = UWB_DATA_MESSAGE_RAFT;
  dataTxPacket.header.srcAddress = raftNode.me;
  dataTxPacket.header.destAddress = peerAddress;
  dataTxPacket.header.ttl = 10;
  dataTxPacket.header.length = sizeof(UWB_Data_Packet_Header_t) + sizeof(Raft_Request_Vote_Args_t);
  Raft_Request_Vote_Args_t *args = (Raft_Request_Vote_Args_t *) &dataTxPacket.payload;
  args->type = RAFT_REQUEST_VOTE;
  args->term = raftNode.currentTerm;
  args->candidateId = raftNode.me;
  args->lastLogIndex = raftNode.log.items[raftNode.log.size - 1].index;
  args->lastLogTerm = raftNode.log.items[raftNode.log.size - 1].term;
  DEBUG_PRINT("raftSendRequestVote: %u send request vote to %u.\n", raftNode.me, peerAddress);
  uwbSendDataPacketBlock(&dataTxPacket);
}
// TODO: check
void raftProcessRequestVote(UWB_Address_t peerAddress, Raft_Request_Vote_Args_t *args) {
  DEBUG_PRINT("raftProcessRequestVote: %u received vote request from %u.\n", raftNode.me, peerAddress);
  if (args->term < raftNode.currentTerm) {
    DEBUG_PRINT("raftProcessRequestVote: Candidate term = %u < my term = %u, ignore.\n",
                args->term,
                raftNode.currentTerm);
    raftSendRequestVoteReply(args->candidateId, raftNode.currentTerm, false);
    return;
  }
  /* If RPC request or response contains term T > currentTerm, set currentTerm = T, convert to follower. */
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
// TODO: check
void raftSendRequestVoteReply(UWB_Address_t peerAddress, uint16_t term, bool voteGranted) {
  UWB_Data_Packet_t dataTxPacket;
  dataTxPacket.header.type = UWB_DATA_MESSAGE_RAFT;
  dataTxPacket.header.srcAddress = raftNode.me;
  dataTxPacket.header.destAddress = peerAddress;
  dataTxPacket.header.ttl = 10;
  dataTxPacket.header.length = sizeof(UWB_Data_Packet_Header_t) + sizeof(Raft_Request_Vote_Reply_t);
  Raft_Request_Vote_Reply_t *reply = (Raft_Request_Vote_Reply_t *) &dataTxPacket.payload;
  reply->type = RAFT_REQUEST_VOTE_REPLY;
  reply->term = term;
  reply->voteGranted = voteGranted;
  DEBUG_PRINT("raftSendRequestVoteReply: %u send vote reply to %u, term = %u, granted = %d.\n",
              raftNode.me,
              peerAddress,
              term,
              voteGranted);
  uwbSendDataPacketBlock(&dataTxPacket);
}
// TODO: check
void raftProcessRequestVoteReply(UWB_Address_t peerAddress, Raft_Request_Vote_Reply_t *reply) {
  DEBUG_PRINT("raftProcessRequestVoteReply: %u received vote request reply from %u.\n", raftNode.me, peerAddress);
  if (reply->term < raftNode.currentTerm) {
    DEBUG_PRINT("raftProcessRequestVoteReply: Peer term = %u < my term = %u, ignore.\n",
                reply->term,
                raftNode.currentTerm);
    return;
  }
  /* If RPC request or response contains term T > currentTerm, set currentTerm = T, convert to follower. */
  if (reply->term > raftNode.currentTerm) {
    DEBUG_PRINT("raftProcessRequestVoteReply: Peer term = %u > my term = %u, convert to follower.\n",
                reply->term,
                raftNode.currentTerm
    );
    raftNode.currentTerm = reply->term;
    convertToFollower(&raftNode);
  }
  if (reply->voteGranted && raftNode.currentState == RAFT_STATE_CANDIDATE) {
    raftNode.peerVote[peerAddress] = true;
    uint8_t voteCount = 0;
    for (int i = 0; i < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; i++) {
      if (raftNode.peerNodes[i]) {
        voteCount++;
      }
    }
    // TODO: Compare vote count with actual node configuration
    if (voteCount >= RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX / 2) {
      DEBUG_PRINT("raftProcessRequestVoteReply: %u elected as leader.\n", raftNode.me);
      raftNode.currentState = RAFT_STATE_LEADER;
      /* Upon election: send initial empty AppendEntries RPCs (heartbeat) to each server, repeat during idle periods to
       * prevent election timeouts.
       */
      for (int peerNode = 0; peerNode < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; peerNode++) {
        if (peerNode != raftNode.me) {
          raftNode.nextIndex[peerNode] = raftNode.log.size;
          raftNode.matchIndex[peerNode] = 0;
          raftSendAppendEntries(peerNode);
        }
      }
    }
  }
}
// TODO: check
void raftSendAppendEntries(UWB_Address_t peerAddress) {
  UWB_Data_Packet_t dataTxPacket;
  dataTxPacket.header.type = UWB_DATA_MESSAGE_RAFT;
  dataTxPacket.header.srcAddress = raftNode.me;
  dataTxPacket.header.destAddress = peerAddress;
  dataTxPacket.header.ttl = 10;
  dataTxPacket.header.length = sizeof(UWB_Data_Packet_Header_t) + sizeof(Raft_Append_Entries_Args_t);
  Raft_Append_Entries_Args_t *args = (Raft_Append_Entries_Args_t *) &dataTxPacket.payload;
  args->type = RAFT_APPEND_ENTRIES;
  args->term = raftNode.currentTerm;
  args->leaderId = raftNode.me;
  // TODO: check snapshot
  int preLogItemIndex = raftLogFindByIndex(&raftNode.log, raftNode.nextIndex[peerAddress] - 1);
  ASSERT(preLogItemIndex >= 0);
  args->prevLogIndex = raftNode.log.items[preLogItemIndex].index;
  args->prevLogTerm = raftNode.log.items[preLogItemIndex].term;
  // TODO: check
  int nextItemIndex = preLogItemIndex + 1;
  int startItemIndex = nextItemIndex;
  int endItemIndex = MIN(RAFT_LOG_SIZE_MAX - 1, startItemIndex + RAFT_LOG_ENTRIES_SIZE_MAX - 1);
  /* Include log items with item index in [startItemIndex, endItemIndex] */
  args->entryCount = 0;
  for (int i = startItemIndex; i <= endItemIndex; i++) {
    args->entries[args->entryCount] = raftNode.log.items[i];
    args->entryCount++;
  }
  args->leaderCommit = raftNode.commitIndex;
  DEBUG_PRINT("raftSendAppendEntries: %u send append entries to %u.\n", raftNode.me, peerAddress);
  uwbSendDataPacketBlock(&dataTxPacket);
}
// TODO: check
void raftProcessAppendEntries(UWB_Address_t peerAddress, Raft_Append_Entries_Args_t *args) {
  DEBUG_PRINT("raftProcessAppendEntries: %u received append entries request from %u.\n", raftNode.me, peerAddress);
  if (args->term < raftNode.currentTerm) {
    DEBUG_PRINT("raftProcessAppendEntries: Peer term = %u < my term = %u, ignore.\n",
                args->term,
                raftNode.currentTerm);
    raftSendAppendEntriesReply(peerAddress, raftNode.currentTerm, false);
    return;
  }
  /* If RPC request or response contains term T > currentTerm, set currentTerm = T, convert to follower. */
  if (args->term > raftNode.currentTerm) {
    DEBUG_PRINT("raftProcessAppendEntries: Peer term = %u > my term = %u, convert to follower.\n",
                args->term,
                raftNode.currentTerm
    );
    raftNode.currentTerm = args->term;
    convertToFollower(&raftNode);
  }
  raftNode.lastHeartbeatTime = xTaskGetTickCount();
  /* Candidate: If AppendEntries RPC received from new leader, convert to follower. */
  if (raftNode.currentState == RAFT_STATE_CANDIDATE) {
    // TODO: check
    DEBUG_PRINT(
        "raftProcessAppendEntries: Candidate %u received append entries request from new leader %u, convert to follower.\n",
        raftNode.me,
        peerAddress);
    convertToFollower(&raftNode);
  }
  /* Reply false if log doesn't contain an entry at prevLogIndex whose term matches prevLogTerm. */
  // TODO: check snapshot
  int matchedItemIndex = raftLogFindMatched(&raftNode.log, args->prevLogIndex, args->prevLogTerm);
  if (matchedItemIndex == -1) {
    DEBUG_PRINT(
        "raftProcessAppendEntries: %u log doesn't contain an entry at prevLogIndex = %u whose term matches prevLogTerm = %u.\n",
        raftNode.me,
        args->prevLogIndex,
        args->prevLogTerm);
    raftSendAppendEntriesReply(peerAddress, raftNode.currentTerm, false);
    return;
  }
  /* If an existing entry conflicts with a new one (same index but different terms), delete the existing entry and all
   * that follow it. Here we just clean all entries follow and overwrite them with entries from request args.
   */
  // TODO: check snapshot
  int startItemIndex = matchedItemIndex + 1;
  raftLogCleanFrom(&raftNode.log, startItemIndex);
  /* Append any new entries not already in the log. */
  // TODO: check
  for (int i = 0; i < args->entryCount; i++) {
    raftNode.log.items[startItemIndex + i] = args->entries[i];
    raftNode.log.size++;
  }
  /* If leaderCommit > commitIndex, set commitIndex = minInt(leaderCommit, index of last new entry) */
  if (args->leaderCommit > raftNode.commitIndex) {
    DEBUG_PRINT("raftProcessAppendEntries: Leader commit = %u > commitIndex = %u, update.\n",
                args->leaderCommit,
                raftNode.commitIndex);
    raftNode.commitIndex = MIN(args->leaderCommit, raftNode.log.items[raftNode.log.size - 1].index);
  }
  raftNode.lastHeartbeatTime = xTaskGetTickCount();
  raftSendAppendEntriesReply(peerAddress, raftNode.currentTerm, true);
}
// TODO: check
void raftSendAppendEntriesReply(UWB_Address_t peerAddress, uint16_t term, bool success) {
  UWB_Data_Packet_t dataTxPacket;
  dataTxPacket.header.type = UWB_DATA_MESSAGE_RAFT;
  dataTxPacket.header.srcAddress = raftNode.me;
  dataTxPacket.header.destAddress = peerAddress;
  dataTxPacket.header.ttl = 10;
  dataTxPacket.header.length = sizeof(UWB_Data_Packet_Header_t) + sizeof(Raft_Append_Entries_Reply_t);
  Raft_Append_Entries_Reply_t *reply = (Raft_Append_Entries_Reply_t *) &dataTxPacket.payload;
  reply->type = RAFT_APPEND_ENTRIES_REPLY;
  reply->term = term;
  reply->success = success;
  DEBUG_PRINT("raftSendAppendEntriesReply: %u send vote reply to %u, term = %u, success = %d.\n",
              raftNode.me,
              peerAddress,
              term,
              success);
  uwbSendDataPacketBlock(&dataTxPacket);
}
// TODO: check
void raftProcessAppendEntriesReply(UWB_Address_t peerAddress, Raft_Append_Entries_Reply_t *reply) {
  DEBUG_PRINT("raftProcessAppendEntriesReply: %u received append entries reply from %u.\n", raftNode.me, peerAddress);
  if (reply->term < raftNode.currentTerm) {
    DEBUG_PRINT("raftProcessAppendEntriesReply: Peer term = %u < my term = %u, ignore.\n",
                reply->term,
                raftNode.currentTerm);

    return;
  }
  /* If RPC request or response contains term T > currentTerm, set currentTerm = T, convert to follower. */
  if (reply->term > raftNode.currentTerm) {
    DEBUG_PRINT("raftProcessAppendEntriesReply: Peer term = %u > my term = %u, convert to follower.\n",
                reply->term,
                raftNode.currentTerm
    );
    raftNode.currentTerm = reply->term;
    convertToFollower(&raftNode);
  }
  if (reply->success) {
    // TODO: add info in messages to update match index and next index.
//    raftNode.matchIndex[peerAddress] = ?
//    raftNode.nextIndex[peerAddress] = ?
    /* If there exists an N such that N > commitIndex, a majority of matchIndex[i] ≥ N,
     * and log[N].term == currentTerm: set commitIndex = N
     */
    // TODO: update commit index and apply index.
  } else {
    /* If AppendEntries fails because of log inconsistency: decrement nextIndex and retry. */
    // TODO: sendAppendEntries && backward log index term by term
  }
}
