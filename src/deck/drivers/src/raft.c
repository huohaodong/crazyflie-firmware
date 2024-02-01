#include <stdlib.h>
#include "FreeRTOS.h"
#include "timers.h"
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
static TimerHandle_t raftElectionTimer;
static TimerHandle_t raftHeartbeatTimer;
static TimerHandle_t raftLogApplyTimer;
static Raft_Log_Item_t EMPTY_LOG_ITEM = {
    .term = 0,
    .index = 0,
    .command = {.type = RAFT_LOG_COMMAND_RESERVED, .clientId = UWB_DEST_EMPTY, .requestId = 0}
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
  /* Binary Search */
  int left = -1, right = raftLog->size;
  while (left + 1 != right) {
    int mid = left + (right - left) / 2;
    if (raftLog->items[mid].index == logIndex) {
      if (raftLog->items[mid].term == logTerm) {
        return mid;
      }
      break;
    } else if (raftLog->items[mid].index > logIndex) {
      right = mid;
    } else {
      left = mid;
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
// TODO: check
static int raftLogGetLastLogItemByTerm(Raft_Log_t *raftLog, uint16_t logTerm) {
  /* Binary search, i.e. find last 6 in [1,2,3,3,6,6,6,6,7,8] */
  int left = -1, right = raftLog->size;
  while (left + 1 != right) {
    int mid = left + (right - left) / 2;
    if (raftLog->items[mid].term <= logTerm) {
      left = mid;
    } else if (raftLog->items[mid].term > logTerm) {
      right = mid;
    }
  }
  return left;
}
// TODO: check
static void raftLogApply(Raft_Log_t *raftLog, uint16_t logItemIndex) {
  // TODO
  DEBUG_PRINT("raftLogApply: Apply log index = %u.\n", raftLog->items[logItemIndex].index);
}
// TODO: check
static void raftLogAppend(Raft_Log_t *raftLog, uint16_t logTerm, Raft_Log_Command_t command) {
  if (raftLog->size >= RAFT_LOG_SIZE_MAX * 0.75) {
    // TODO: snapshot
  }
  int index = raftLog->size;
  raftLog->items[index].term = logTerm;
  raftLog->items[index].index = raftLog->items[index - 1].index + 1;
  raftLog->items[index].command = command;
  raftLog->size++;
  DEBUG_PRINT("raftLogAdd: Add log index = %u, term = %u.\n", raftLog->items[index].index, raftLog->items[index].term);
}
// TODO: check
static void raftUpdateCommitIndex(Raft_Node_t *node) {
  // TODO: Compare count with actual node configuration
  for (int peer = 0; peer < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; peer++) {
    uint16_t candidateCommitIndex = node->matchIndex[peer];
    if (candidateCommitIndex > node->commitIndex) {
      uint8_t count = 0;
      for (int i = 0; i < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; i++) {
        if (i != node->me && node->matchIndex[i] >= candidateCommitIndex) {
          count++;
        }
      }
      if (count >= RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX / 2) {
        DEBUG_PRINT("raftUpdateCommitIndex: %u update commit index from %u to %u.\n",
                    node->me,
                    node->commitIndex,
                    MAX(node->commitIndex, candidateCommitIndex));
        node->commitIndex = MAX(node->commitIndex, candidateCommitIndex);
      }
    }
  }
}

static void convertToFollower(Raft_Node_t *node) {
  node->currentState = RAFT_STATE_FOLLOWER;
  node->currentLeader = UWB_DEST_EMPTY;
  node->voteFor = RAFT_VOTE_FOR_NO_ONE;
  for (int i = 0; i < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; i++) {
    node->peerVote[i] = false;
  }
  node->lastHeartbeatTime = xTaskGetTickCount();
}

static void convertToLeader(Raft_Node_t *node) {
  node->currentState = RAFT_STATE_LEADER;
  node->currentLeader = node->me;
}

static void convertToCandidate(Raft_Node_t *node) {
  node->currentTerm++;
  node->currentState = RAFT_STATE_CANDIDATE;
  node->currentLeader = UWB_DEST_EMPTY;
  for (int peer = 0; peer < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; peer++) {
    node->peerVote[peer] = false;
  }
  node->voteFor = raftNode.me;
  node->lastHeartbeatTime = xTaskGetTickCount();
}

// TODO: check
static void raftHeartbeatTimerCallback(TimerHandle_t timer) {
//  DEBUG_PRINT("raftHeartbeatTimerCallback: %u trigger heartbeat timer at %lu.\n", raftNode.me, xTaskGetTickCount());
  xSemaphoreTake(raftNode.mu, portMAX_DELAY);
  if (raftNode.currentState == RAFT_STATE_LEADER) {
    // TODO: use actual node configuration
    for (int peer = 0; peer < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; peer++) {
      if (peer != raftNode.me) {
        raftSendAppendEntries(peer);
        vTaskDelay(M2T(5));
      }
    }
  }
  xSemaphoreGive(raftNode.mu);
}
// TODO: check
static void raftElectionTimerCallback(TimerHandle_t timer) {
//  DEBUG_PRINT("raftElectionTimerCallback: %u trigger election timer at %lu, lastHeartbeat = %lu.\n",
//              raftNode.me,
//              xTaskGetTickCount(),
//              raftNode.lastHeartbeatTime);
  xSemaphoreTake(raftNode.mu, portMAX_DELAY);
  Time_t curTime = xTaskGetTickCount();
  if (raftNode.currentState != RAFT_STATE_LEADER) {
    if ((curTime - raftNode.lastHeartbeatTime) > RAFT_ELECTION_TIMEOUT) {
      DEBUG_PRINT("raftLogApplyTimerCallback: %u timeout in term %u, commitIndex = %u.\n",
                  raftNode.me,
                  raftNode.currentTerm,
                  raftNode.commitIndex);
      convertToCandidate(&raftNode);
      for (int peer = 0; peer < RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX; peer++) {
        if (peer != raftNode.me) {
          raftSendRequestVote(peer);
          vTaskDelay(M2T(5));
        }
      }
    }
  } else {
    raftNode.lastHeartbeatTime = xTaskGetTickCount();
  }
  xSemaphoreGive(raftNode.mu);
}
// TODO: check
static void raftLogApplyTimerCallback(TimerHandle_t timer) {
//  DEBUG_PRINT("raftLogApplyTimerCallback: %u trigger log apply timer at %lu.\n", raftNode.me, xTaskGetTickCount());
  xSemaphoreTake(raftNode.mu, portMAX_DELAY);
  int startIndex = raftNode.lastApplied + 1;
  for (int i = startIndex; i <= raftNode.commitIndex; i++) {
    raftNode.lastApplied++;
    raftLogApply(&raftNode.log, i);
  }
  xSemaphoreGive(raftNode.mu);
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
          break;
        case RAFT_REQUEST_VOTE_REPLY:
          raftProcessRequestVoteReply(peer, (Raft_Request_Vote_Reply_t *) dataRxPacket.payload);
          break;
        case RAFT_APPEND_ENTRIES:
          raftProcessAppendEntries(peer, (Raft_Append_Entries_Args_t *) dataRxPacket.payload);
          break;
        case RAFT_APPEND_ENTRIES_REPLY:
          raftProcessAppendEntriesReply(peer, (Raft_Append_Entries_Reply_t *) dataRxPacket.payload);
          break;
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
  UWB_Data_Packet_Listener_t listener = {
      .type = UWB_DATA_MESSAGE_RAFT,
      .rxQueue = rxQueue
  };
  uwbRegisterDataPacketListener(&listener);

  raftNode.mu = xSemaphoreCreateMutex();
  raftNode.currentLeader = UWB_DEST_EMPTY;
  raftNode.me = uwbGetAddress();
//  node.peerNodes = ? TODO: init peer
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
  raftHeartbeatTimer = xTimerCreate("raftHeartbeatTimer",
                                   M2T(RAFT_HEARTBEAT_INTERVAL),
                                   pdTRUE,
                                   (void *) 0,
                                    raftHeartbeatTimerCallback);
  xTimerStart(raftHeartbeatTimer, M2T(0));
  raftElectionTimer = xTimerCreate("raftElectionTimer",
                                   M2T((rand() % 50 + RAFT_ELECTION_TIMEOUT) / 2),
                                   pdTRUE,
                                   (void *) 0,
                                   raftElectionTimerCallback);
  xTimerStart(raftElectionTimer, M2T(0));
  raftLogApplyTimer = xTimerCreate("raftLogApplyTimer",
                                   M2T(RAFT_LOG_APPLY_INTERVAL),
                                   pdTRUE,
                                   (void *) 0,
                                   raftLogApplyTimerCallback);
  xTimerStart(raftLogApplyTimer, M2T(0));

  xTaskCreate(raftRxTask, ADHOC_DECK_RAFT_RX_TASK_NAME, UWB_TASK_STACK_SIZE, NULL,
              ADHOC_DECK_TASK_PRI, &raftRxTaskHandle);
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
  DEBUG_PRINT("raftProcessRequestVoteReply: %u received vote request reply from %u, grant = %d.\n",
              raftNode.me,
              peerAddress,
              reply->voteGranted);
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
      if (i != raftNode.me && raftNode.peerVote[i]) {
        voteCount++;
      }
    }
    DEBUG_PRINT("raftProcessRequestVoteReply: voteCount = %u.\n", voteCount);
    // TODO: Compare vote count with actual node configuration
    if (voteCount >= RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX / 2) {
      DEBUG_PRINT("raftProcessRequestVoteReply: %u elected as leader.\n", raftNode.me);
      convertToLeader(&raftNode);
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
  if (preLogItemIndex < 0) {
    DEBUG_PRINT("raftSendAppendEntries: %u has preLogItemIndex < 0 for peer %u, ignore.\n", raftNode.me, peerAddress);
    return;
  }
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
    raftSendAppendEntriesReply(peerAddress, raftNode.currentTerm, false, 0);
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
  /* Candidate: If AppendEntries RPC received from new leader, convert to follower. */
  if (raftNode.currentState == RAFT_STATE_CANDIDATE) {
    // TODO: check
    DEBUG_PRINT(
        "raftProcessAppendEntries: Candidate %u received append entries request from new leader %u, convert to follower.\n",
        raftNode.me,
        peerAddress);
    convertToFollower(&raftNode);
  }
  raftNode.currentLeader = peerAddress;
  raftNode.lastHeartbeatTime = xTaskGetTickCount();
  /* Reply false if log doesn't contain an entry at prevLogIndex whose term matches prevLogTerm. */
  // TODO: check snapshot
  int matchedItemIndex = raftLogFindMatched(&raftNode.log, args->prevLogIndex, args->prevLogTerm);
  if (matchedItemIndex == -1) {
    DEBUG_PRINT(
        "raftProcessAppendEntries: %u log doesn't contain an entry at prevLogIndex = %u whose term matches prevLogTerm = %u.\n",
        raftNode.me,
        args->prevLogIndex,
        args->prevLogTerm);
    raftSendAppendEntriesReply(peerAddress, raftNode.currentTerm, false, 0);
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
  raftSendAppendEntriesReply(peerAddress, raftNode.currentTerm, true, raftNode.log.items[raftNode.log.size - 1].index + 1);
}
// TODO: check
void raftSendAppendEntriesReply(UWB_Address_t peerAddress, uint16_t term, bool success, uint16_t nextIndex) {
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
  reply->nextIndex = nextIndex;
  DEBUG_PRINT("raftSendAppendEntriesReply: %u send append entries reply to %u, term = %u, success = %d, nextIndex = %u.\n",
              raftNode.me,
              peerAddress,
              term,
              success,
              nextIndex);
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
  if (raftNode.currentState != RAFT_STATE_LEADER) {
    DEBUG_PRINT("raftProcessAppendEntriesReply: %u is not a leader now, ignore.\n", raftNode.me);
    return;
  }
  if (reply->success) {
    // TODO: check
    /* If successful: update nextIndex and matchIndex for follower. */
    raftNode.nextIndex[peerAddress] = MAX(raftNode.nextIndex[peerAddress], reply->nextIndex);
    raftNode.matchIndex[peerAddress] = raftNode.nextIndex[peerAddress] - 1;
    /* If there exists an N such that N > commitIndex, a majority of matchIndex[i] ≥ N,
     * and log[N].term == currentTerm: set commitIndex = N.
     */
    raftUpdateCommitIndex(&raftNode);
  } else {
    /* If AppendEntries fails because of log inconsistency: decrement nextIndex and retry.
     * Here we decrement nextIndex from nextIndex to matchIndex in a term by term way for efficiency.
     */
    // TODO: check
    int matchedIndex = raftLogFindByIndex(&raftNode.log, raftNode.matchIndex[peerAddress]);
    if (matchedIndex == -1) {
      DEBUG_PRINT("raftProcessAppendEntriesReply: %u cannot find log with matchIndex = %u.\n",
                  raftNode.me,
                  raftNode.matchIndex[peerAddress]
      );
    } else {
      int itemIndex = raftLogGetLastLogItemByTerm(&raftNode.log, raftNode.log.items[matchedIndex].term - 1);
      if (itemIndex == -1) {
        DEBUG_PRINT("raftProcessAppendEntriesReply: %u cannot find log with term = %u.\n",
                    raftNode.me,
                    raftNode.log.items[matchedIndex].term - 1
        );
      } else {
        raftNode.nextIndex[peerAddress] = MAX(raftNode.matchIndex[peerAddress] + 1, raftNode.log.items[itemIndex].index);
        raftSendAppendEntries(peerAddress);
      }
    }
  }
}
