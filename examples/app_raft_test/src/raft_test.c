#include <string.h>
#include "FreeRTOS.h"
#include "debug.h"
#include "task.h"
#include "app.h"
#include "raft.h"

static TaskHandle_t raftTaskHandle;
static Raft_Node_t *raftNode;

static void testProposeEmptyLog() {
  bool success = true;
  uint16_t requestId = 0;
  while (1) {
    if (success) {
      requestId = raftProposeNew(RAFT_LOG_COMMAND_RESERVED, NULL, 0);
      success = false;
    } else {
      raftProposeRetry(requestId, RAFT_LOG_COMMAND_RESERVED, NULL, 0);
    }
    success = raftProposeCheck(requestId, 2000);
    DEBUG_PRINT("raftTask: proposed requestId = %u, success = %d.\n",
                requestId,
                success
    );
  }
}

static void testProposeMemeberAdd(UWB_Address_t newMember) {
  bool success = true;
  uint16_t requestId = 0;
  while (1) {
    if (success) {
      requestId = raftProposeNew(RAFT_LOG_COMMAND_CONFIG_ADD, (uint8_t * ) & newMember, 2);
      success = false;
    } else {
      raftProposeRetry(requestId, RAFT_LOG_COMMAND_CONFIG_ADD, (uint8_t * ) & newMember, 2);
    }
    success = raftProposeCheck(requestId, 2000);
    DEBUG_PRINT("raftTask: proposed requestId = %u, success = %d.\n",
                requestId,
                success
    );
    printRaftConfig(raftNode->config);
  }
}

static void testProposeMemeberRemove(UWB_Address_t newMember) {
  bool success = true;
  uint16_t requestId = 0;
  while (1) {
    if (success) {
      requestId = raftProposeNew(RAFT_LOG_COMMAND_CONFIG_REMOVE, (uint8_t * ) & newMember, 2);
      success = false;
    } else {
      raftProposeRetry(requestId, RAFT_LOG_COMMAND_CONFIG_REMOVE, (uint8_t * ) & newMember, 2);
    }
    success = raftProposeCheck(requestId, 2000);
    DEBUG_PRINT("raftTask: proposed requestId = %u, success = %d.\n",
                requestId,
                success
    );
    printRaftConfig(raftNode->config);
  }
}

static void raftTask() {
  // testProposeEmptyLog();
  // testProposeMemeberAdd(4);
  testProposeMemeberRemove(4);
}

void appMain() {
  raftNode = getGlobalRaftNode();
  xTaskCreate(raftTask, "RAFT_TEST", UWB_TASK_STACK_SIZE, NULL,
              ADHOC_DECK_TASK_PRI, &raftTaskHandle);
}
