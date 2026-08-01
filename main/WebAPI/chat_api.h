#pragma once

#include "esp_system.h"

// 获取回答数据
// prompt  : 用户输入文本
// api_key : Agent API 密钥（从 NVS 配置读取，如 "Bearer sk-xxx"）
char *mfg_agent_chat(char *prompt, const char *api_key);

