#include <string.h>
#include "FreeRTOS.h"
#include "debug.h"
#include "task.h"
#include "app.h"
#include "raft.h"

static TaskHandle_t raftTaskHandle;

static void raftTask() {
  bool state = true;
  uint16_t requestId = 0;
  while (1) {
    if (state) {
      requestId = raftProposeNew(RAFT_LOG_COMMAND_RESERVED, NULL, 0);
      state = false;
    } else {
      raftProposeRetry(requestId, RAFT_LOG_COMMAND_RESERVED, NULL, 0);
    }
    state = raftProposeCheck(requestId, 2000);
    DEBUG_PRINT("raftTask: proposed requestId = %u, state = %d.\n",
                requestId,
                state
    );
  }
}

void appMain() {
  xTaskCreate(raftTask, "RAFT_TEST", UWB_TASK_STACK_SIZE, NULL,
              ADHOC_DECK_TASK_PRI, &raftTaskHandle);
}
