#pragma once
#include <string>
void T3_SetStatus(const std::string& s);
std::string T3_GetStatus();
void T3_InitOnce();
void T3_StartHeartbeat();
void T3_LoginAsync(const char* kami);
void T3_TryAutoLoginFromConfig();
void DrawT3AuthUI();
