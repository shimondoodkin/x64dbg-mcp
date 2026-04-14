#include "DebugHandler.h"
#include "../business/DebugController.h"
#include "../core/MethodDispatcher.h"
#include "../core/RequestValidator.h"
#include "../core/Logger.h"
#include "../utils/StringUtils.h"
#include <windows.h>

namespace MCP {

void DebugHandler::RegisterMethods() {
    auto& dispatcher = MethodDispatcher::Instance();
    
    dispatcher.RegisterMethod("debug.get_state", GetState);
    dispatcher.RegisterMethod("debug.run", Run);
    dispatcher.RegisterMethod("debug.run_pass_exception", RunPassException);
    dispatcher.RegisterMethod("debug.run_swallow_exception", RunSwallowException);
    dispatcher.RegisterMethod("debug.step_into_pass_exception", StepIntoPassException);
    dispatcher.RegisterMethod("debug.step_into_swallow_exception", StepIntoSwallowException);
    dispatcher.RegisterMethod("debug.step_over_pass_exception", StepOverPassException);
    dispatcher.RegisterMethod("debug.step_over_swallow_exception", StepOverSwallowException);
    dispatcher.RegisterMethod("debug.step_out_pass_exception", StepOutPassException);
    dispatcher.RegisterMethod("debug.step_out_swallow_exception", StepOutSwallowException);
    dispatcher.RegisterMethod("debug.skip_instruction", SkipInstruction);
    dispatcher.RegisterMethod("debug.pause", Pause);
    dispatcher.RegisterMethod("debug.step_into", StepInto);
    dispatcher.RegisterMethod("debug.step_over", StepOver);
    dispatcher.RegisterMethod("debug.step_out", StepOut);
    dispatcher.RegisterMethod("debug.run_to", RunTo);
    dispatcher.RegisterMethod("debug.restart", Restart);
    dispatcher.RegisterMethod("debug.stop", Stop);
    
    Logger::Info("Registered debug.* methods");
}

json DebugHandler::GetState(const json& params) {
    auto& controller = DebugController::Instance();
    
    DebugState state = controller.GetState();
    std::string stateStr = StateToString(state);
    
    json result = {
        {"state", stateStr}
    };
    
    if (controller.IsDebugging()) {
        try {
            uint64_t rip = controller.GetInstructionPointer();
            result["rip"] = StringUtils::FormatAddress(rip);
        } catch (...) {
            // 如果无法获取 RIP，忽略错误
        }
    }
    
    return result;
}

json DebugHandler::Run(const json& params) {
    auto& controller = DebugController::Instance();
    
    bool success = controller.Run();
    
    json result = {
        {"success", success}
    };
    
    // 等待一小段时间让调试器状态稳定
    Sleep(50);
    
    // 获取当前状态
    DebugState state = controller.GetState();
    result["state"] = StateToString(state);
    
    // 如果暂停了，返回停止原因
    if (controller.IsPaused()) {
        try {
            uint64_t rip = controller.GetInstructionPointer();
            result["rip"] = StringUtils::FormatAddress(rip);
            result["stop_reason"] = "breakpoint_or_exception";
        } catch (...) {
            // 忽略
        }
    }
    
    return result;
}

json DebugHandler::RunPassException(const json& params) {
    (void)params;
    auto& controller = DebugController::Instance();

    bool success = controller.RunPassException();

    json result = {
        {"success", success}
    };

    // Give the debugger a moment to settle, then report state.
    Sleep(50);

    DebugState state = controller.GetState();
    result["state"] = StateToString(state);

    if (controller.IsPaused()) {
        try {
            uint64_t rip = controller.GetInstructionPointer();
            result["rip"] = StringUtils::FormatAddress(rip);
            result["stop_reason"] = "breakpoint_or_exception";
        } catch (...) {
        }
    }

    return result;
}

json DebugHandler::RunSwallowException(const json& params) {
    (void)params;
    auto& controller = DebugController::Instance();
    bool success = controller.RunSwallowException();
    json result = { {"success", success} };
    Sleep(50);
    DebugState state = controller.GetState();
    result["state"] = StateToString(state);
    if (controller.IsPaused()) {
        try { result["rip"] = StringUtils::FormatAddress(controller.GetInstructionPointer()); }
        catch (...) {}
    }
    return result;
}

json DebugHandler::StepIntoPassException(const json& params) {
    (void)params;
    uint64_t rip = DebugController::Instance().StepIntoPassException();
    return { {"rip", StringUtils::FormatAddress(rip)} };
}

json DebugHandler::StepIntoSwallowException(const json& params) {
    (void)params;
    uint64_t rip = DebugController::Instance().StepIntoSwallowException();
    return { {"rip", StringUtils::FormatAddress(rip)} };
}

json DebugHandler::StepOverPassException(const json& params) {
    (void)params;
    uint64_t rip = DebugController::Instance().StepOverPassException();
    return { {"rip", StringUtils::FormatAddress(rip)} };
}

json DebugHandler::StepOverSwallowException(const json& params) {
    (void)params;
    uint64_t rip = DebugController::Instance().StepOverSwallowException();
    return { {"rip", StringUtils::FormatAddress(rip)} };
}

json DebugHandler::StepOutPassException(const json& params) {
    (void)params;
    uint64_t rip = DebugController::Instance().StepOutPassException();
    return { {"rip", StringUtils::FormatAddress(rip)} };
}

json DebugHandler::StepOutSwallowException(const json& params) {
    (void)params;
    uint64_t rip = DebugController::Instance().StepOutSwallowException();
    return { {"rip", StringUtils::FormatAddress(rip)} };
}

json DebugHandler::SkipInstruction(const json& params) {
    (void)params;
    auto& controller = DebugController::Instance();
    bool success = controller.SkipInstruction();
    json result = { {"success", success} };
    if (controller.IsPaused()) {
        try { result["rip"] = StringUtils::FormatAddress(controller.GetInstructionPointer()); }
        catch (...) {}
    }
    return result;
}

json DebugHandler::Pause(const json& params) {
    auto& controller = DebugController::Instance();
    
    bool success = controller.Pause();
    uint64_t rip = 0;
    
    if (success && controller.IsPaused()) {
        try {
            rip = controller.GetInstructionPointer();
        } catch (...) {
            // 忽略
        }
    }
    
    json result = {
        {"success", success}
    };
    
    if (rip != 0) {
        result["rip"] = StringUtils::FormatAddress(rip);
    }
    
    return result;
}

json DebugHandler::StepInto(const json& params) {
    auto& controller = DebugController::Instance();
    
    uint64_t rip = controller.StepInto();
    
    return {
        {"rip", StringUtils::FormatAddress(rip)}
    };
}

json DebugHandler::StepOver(const json& params) {
    auto& controller = DebugController::Instance();
    
    uint64_t rip = controller.StepOver();
    
    return {
        {"rip", StringUtils::FormatAddress(rip)}
    };
}

json DebugHandler::StepOut(const json& params) {
    auto& controller = DebugController::Instance();
    
    uint64_t rip = controller.StepOut();
    
    return {
        {"rip", StringUtils::FormatAddress(rip)}
    };
}

json DebugHandler::RunTo(const json& params) {
    RequestValidator::RequireString(params, "address");
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = RequestValidator::ValidateAddress(addressStr);
    
    auto& controller = DebugController::Instance();
    bool success = controller.RunToAddress(address);
    
    return {
        {"success", success},
        {"address", StringUtils::FormatAddress(address)}
    };
}

json DebugHandler::Restart(const json& params) {
    auto& controller = DebugController::Instance();
    
    bool success = controller.Restart();
    
    return {
        {"success", success}
    };
}

json DebugHandler::Stop(const json& params) {
    auto& controller = DebugController::Instance();
    
    bool success = controller.Stop();
    
    return {
        {"success", success}
    };
}

std::string DebugHandler::StateToString(DebugState state) {
    switch (state) {
        case DebugState::Stopped:
            return "stopped";
        case DebugState::Running:
            return "running";
        case DebugState::Paused:
            return "paused";
        default:
            return "unknown";
    }
}

} // namespace MCP
