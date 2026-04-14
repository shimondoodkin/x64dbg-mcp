#include "PermissionChecker.h"
#include "ConfigManager.h"
#include "Logger.h"

namespace MCP {

PermissionChecker& PermissionChecker::Instance() {
    static PermissionChecker instance;
    return instance;
}

void PermissionChecker::Initialize() {
    // Method-level allowlist was removed; this hook now just confirms the
    // boolean permission flags were consulted at least once. Write-gates
    // (memory/register/script/breakpoint) are still enforced per-handler.
    std::lock_guard<std::mutex> lock(m_mutex);
    Logger::Info("PermissionChecker initialized (method allowlist disabled)");
}

bool PermissionChecker::IsMemoryWriteAllowed() const {
    return ConfigManager::Instance().IsMemoryWriteAllowed();
}

bool PermissionChecker::IsRegisterWriteAllowed() const {
    return ConfigManager::Instance().IsRegisterWriteAllowed();
}

bool PermissionChecker::IsScriptExecutionAllowed() const {
    return ConfigManager::Instance().IsScriptExecutionAllowed();
}

bool PermissionChecker::IsBreakpointModificationAllowed() const {
    return ConfigManager::Instance().Get<bool>("permissions.allow_breakpoint_modification", true);
}

bool PermissionChecker::CanWrite() const {
    // Keep backward compatibility with "permissions.allow_write" while
    // also treating any specific write permission as sufficient.
    if (ConfigManager::Instance().Get<bool>("permissions.allow_write", false)) {
        return true;
    }

    return IsMemoryWriteAllowed() ||
           IsRegisterWriteAllowed() ||
           IsScriptExecutionAllowed() ||
           IsBreakpointModificationAllowed();
}

} // namespace MCP
