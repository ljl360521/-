#ifndef SERVER_SWITCH_H
#define SERVER_SWITCH_H

#include "il2cpp.h"
#include <string>

class ServerSwitcher {
public:
    enum Server { OFFICIAL = 0, ALPHA, BETA };

    bool initialize();

    std::string getCurrentServer();
    Server detectCurrentServer();
    const char* getServerName(Server s);

    bool switchServer(Server server);
    bool executeConsole(const std::string& command);
    bool showFpsTool();
    bool relogin();

    const std::string& getStatus() const { return m_status; }
    bool isInitialized() const { return m_initialized; }

private:
    bool setPlayerPref(const std::string& key, const std::string& value);
    bool setRuntimeServerUrl(const std::string& url);

    bool m_initialized = false;
    std::string m_status;
    std::string m_currentServerUrl;

    Il2CppClass_API* m_FpsTool = nullptr;
    Il2CppClass_API* m_GINIEFLJLDO = nullptr;
    Il2CppClass_API* m_PPMHHDJLCKJ = nullptr;
    Il2CppClass_API* m_NKLBOIBNINH = nullptr;
    Il2CppClass_API* m_PlayerPrefsDefine = nullptr;

    const MethodInfo* m_ConsoleCheck = nullptr;
    const MethodInfo* m_ShowFpsTool = nullptr;
    const MethodInfo* m_getInstance = nullptr;
    const MethodInfo* m_GJPFEOIEMGA = nullptr;
};

extern ServerSwitcher g_serverSwitcher;

#endif