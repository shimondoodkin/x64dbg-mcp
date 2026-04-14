#include "HeartbeatMonitor.h"
#include "../core/Logger.h"
#include "../core/ResponseBuilder.h"

namespace MCP {

HeartbeatMonitor::HeartbeatMonitor(ConnectionManager& connectionManager)
    : m_connectionManager(connectionManager)
    , m_running(false)
    , m_intervalSeconds(30)
{
}

HeartbeatMonitor::~HeartbeatMonitor() {
    Stop();
}

void HeartbeatMonitor::Start(uint32_t intervalSeconds) {
    if (m_running) {
        Logger::Warning("HeartbeatMonitor is already running");
        return;
    }
    
    m_intervalSeconds = intervalSeconds;
    m_running = true;
    m_thread = std::thread(&HeartbeatMonitor::MonitorThread, this);
    
    Logger::Info("HeartbeatMonitor started with interval {} seconds", intervalSeconds);
}

void HeartbeatMonitor::Stop() {
    if (!m_running) {
        return;
    }
    
    Logger::Info("Stopping HeartbeatMonitor...");

    m_running = false;
    m_wakeCv.notify_all();

    if (m_thread.joinable()) {
        m_thread.join();
    }
    
    Logger::Info("HeartbeatMonitor stopped");
}

void HeartbeatMonitor::MonitorThread() {
    Logger::Debug("Heartbeat monitor thread started");
    
    while (m_running) {
        {
            std::unique_lock<std::mutex> lock(m_wakeMutex);
            m_wakeCv.wait_for(lock, std::chrono::seconds(m_intervalSeconds),
                              [this] { return !m_running.load(); });
        }

        if (!m_running) {
            break;
        }

        SendHeartbeat();
    }
    
    Logger::Debug("Heartbeat monitor thread stopped");
}

void HeartbeatMonitor::SendHeartbeat() {
    if (m_connectionManager.GetClientCount() == 0) {
        return;
    }
    
    // 创建心跳通知消息
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    json params = {
        {"timestamp", timestamp},
        {"type", "ping"}
    };
    
    std::string notification = ResponseBuilder::CreateNotification("system.heartbeat", params);
    
    Logger::Trace("Sending heartbeat to {} clients", m_connectionManager.GetClientCount());
    m_connectionManager.BroadcastMessage(notification);
}

} // namespace MCP
