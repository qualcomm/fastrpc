// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "adspmsgd_apps.h"
#include "remote.h"

#include <stdio.h>

#define LOG_NODE_SIZE 256
#define LOG_FILENAME_SIZE 30
#define LOG_MSG_SIZE                                                           \
  LOG_NODE_SIZE - LOG_FILENAME_SIZE - sizeof(enum adspmsgd_apps_Level) -       \
      (2 * sizeof(unsigned short))

typedef struct __attribute__((packed)) {
  enum adspmsgd_apps_Level level;
  unsigned short line;
  unsigned short thread_id;
  char str[LOG_MSG_SIZE];
  char file[LOG_FILENAME_SIZE];
} LogNode;

int adspmsgd_apps_log(const unsigned char *log_message_buffer,
                      int log_message_bufferLen) {
  LogNode *logMessage = (LogNode *)log_message_buffer;
  while ((log_message_bufferLen > 0) && (logMessage != NULL)) {
    printf("adsprpc: %s:%d:0x%x:%s", logMessage->file, logMessage->line,
           logMessage->thread_id, logMessage->str);
    logMessage++;
    log_message_bufferLen -= sizeof(LogNode);
  };

  return 0;
}
void adspmsgd_log_message(char *format, char *msg) {
  printf("adsprpc:dsp: %s\n", msg);
}
