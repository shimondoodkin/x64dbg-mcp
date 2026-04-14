#pragma once
#include <string>
#include <mutex>

namespace MCP {

/**
 * @brief 权限检查器
 */
class PermissionChecker {
public:
    /**
     * @brief 获取单例实例
     */
    static PermissionChecker& Instance();
    
    /**
     * @brief 初始化权限配置
     */
    void Initialize();
    
    /**
     * @brief 检查是否允许内存写入
     */
    bool IsMemoryWriteAllowed() const;
    
    /**
     * @brief 检查是否允许寄存器写入
     */
    bool IsRegisterWriteAllowed() const;
    
    /**
     * @brief 检查是否允许脚本执行
     */
    bool IsScriptExecutionAllowed() const;
    
    /**
     * @brief 检查是否允许断点修改
     */
    bool IsBreakpointModificationAllowed() const;
    
    /**
     * @brief 检查是否允许写入操作 (通用方法)
     * @return 是否允许写入
     */
    bool CanWrite() const;
    

private:
    PermissionChecker() = default;
    ~PermissionChecker() = default;
    PermissionChecker(const PermissionChecker&) = delete;
    PermissionChecker& operator=(const PermissionChecker&) = delete;
    
    mutable std::mutex m_mutex;
};

} // namespace MCP
