#ifndef __RAFT_H__
#define __RAFT_H__
#include <stdint.h>
#include <stdbool.h>
#include "semphr.h"
#include "routing.h"

#define RAFT_DEBUG_ENABLE

/* Queue Constants */
#define RAFT_RX_QUEUE_SIZE 5
#define RAFT_RX_QUEUE_ITEM_SIZE sizeof(UWB_Data_Packet_t)

/* Raft Constants */
#define RAFT_LOG_SIZE_MAX 100
#define RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX 5
#define RAFT_VOTE_FOR_NO_ONE UWB_DEST_EMPTY
#define RAFT_HEARTBEAT_INTERVAL 150 // default 150ms
#define RAFT_ELECTION_TIMEOUT (5 * RAFT_HEARTBEAT_INTERVAL)
#define RAFT_LOG_APPLY_INTERVAL 50 // default 50ms

typedef enum {
  RAFT_STATE_FOLLOWER,
  RAFT_STATE_CANDIDATE,
  RAFT_STATE_LEADER
} RAFT_STATE;

typedef struct {
  uint16_t term;
  uint16_t index;
  char *command; // TODO: COMMAND enum type definition
} Raft_Log_Item_t;

typedef struct {
  uint16_t size;
  Raft_Log_Item_t items[RAFT_LOG_SIZE_MAX];
} Raft_Log_t;

typedef struct {
  SemaphoreHandle_t mu;
  UWB_Address_t me;
  UWB_Address_t peerNodes[RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX]; /* peer nodes in current raft cluster configuration */
  bool peerVote[RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX]; /* granted vote count from peer nodes in current term */
  RAFT_STATE currentState;
  uint16_t currentTerm; /* latest term server has seen (initialized to 0 on first boot, increases monotonically) */
  UWB_Address_t voteFor; /* candidate that received vote in current term (or null if none), RAFT_VOTE_FOR_NO_ONE == null */
  Raft_Log_t log; /* log entries, each entry contains command for state machine, and term when entry was received by leader (first index is 1) */
  uint16_t commitIndex; /* index of highest log entry known to be committed (initialized to 0, increases monotonically) */
  uint16_t lastApplied; /* index of highest log entry known to be applied to state machine (initialized to 0, increases monotonically) */
  uint16_t nextIndex[RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX]; /* for each server, index of the next log entry to send to that server (initialized to leader last log index + 1) */
  uint16_t matchIndex[RAFT_CLUSTER_PEER_NODE_ADDRESS_MAX]; /* for each server, index of highest log entry known to be replicated on server (initialized to 0, increases monotonically) */
  Time_t lastHeartbeatTime; /* heartbeat used for trigger leader election */
} Raft_Node_t;

typedef enum {
  RAFT_REQUEST_VOTE,
  RAFT_REQUEST_VOTE_REPLY,
  RAFT_APPEND_ENTRIES,
  RAFT_APPEND_ENTRIES_REPLY
} RAFT_MESSAGE_TYPE;

typedef struct {
  RAFT_MESSAGE_TYPE type;
  uint16_t term; /* candidate's term */
  UWB_Address_t candidateId; /* candidate that requesting vote */
  uint16_t lastLogIndex; /* index of candidate's last log entry */
  uint16_t lastLogTerm; /* term of candidate's last log entry */
} __attribute__((packed)) Raft_Request_Vote_Args_t;

typedef struct {
  RAFT_MESSAGE_TYPE type;
  uint16_t term; /* currentTerm, for candidate to update itself */
  bool voteGranted; /* true means candidate received vote */
} __attribute__((packed)) Raft_Request_Vote_Reply_t;

#define RAFT_LOG_ENTRIES_SIZE_MAX ((ROUTING_DATA_PACKET_PAYLOAD_SIZE_MAX - 8) / sizeof (Raft_Log_t))

typedef struct {
  RAFT_MESSAGE_TYPE type;
  uint16_t term; /* leader's term */
  UWB_Address_t leaderId; /* so follower can redirect clients */
  uint16_t prevLogIndex; /* index of log entry immediately preceding new ones */
  uint16_t prevLogTerm; /* term of prevLogIndex entry */
  Raft_Log_Item_t entries[RAFT_LOG_ENTRIES_SIZE_MAX]; /* log entries to store (empty for heartbeat; may send more than one for efficiency) */
  uint16_t entryCount; /* log entries count */
  uint16_t leaderCommit; /* leader's commitIndex */
} __attribute__((packed)) Raft_Append_Entries_Args_t;

typedef struct {
  RAFT_MESSAGE_TYPE type;
  uint16_t term; /* currentTerm, for leader to update itself */
  bool success; /* true if follower contained entry matching prevLogIndex and prevLogTerm */
  /* Since we don't have rpc mechanism here, to help leader update nextIndex and matchIndex, follower should tell
   * leader it's replication progress. */
  uint16_t nextIndex; /* 0 represents null */
} __attribute__((packed)) Raft_Append_Entries_Reply_t;

void raftInit();
void raftSendRequestVote(UWB_Address_t peerAddress);
void raftProcessRequestVote(UWB_Address_t peerAddress, Raft_Request_Vote_Args_t *args);
void raftSendRequestVoteReply(UWB_Address_t peerAddress, uint16_t term, bool voteGranted);
void raftProcessRequestVoteReply(UWB_Address_t peerAddress, Raft_Request_Vote_Reply_t *reply);
void raftSendAppendEntries(UWB_Address_t peerAddress);
void raftProcessAppendEntries(UWB_Address_t peerAddress, Raft_Append_Entries_Args_t *args);
void raftSendAppendEntriesReply(UWB_Address_t peerAddress, uint16_t term, bool success, uint16_t nextIndex);
void raftProcessAppendEntriesReply(UWB_Address_t peerAddress, Raft_Append_Entries_Reply_t *reply);

#endif