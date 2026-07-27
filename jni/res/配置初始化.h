#pragma once
#include <string>
void XmlConfigCompileProbe();
void InitAppConfigOnce();
void SaveAppConfigNow();
bool GetCurrentAppExternalFilesDir(std::string& outDir);
