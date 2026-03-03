#include "DumpHandler.h"
#include "../business/DumpManager.h"
#include "../core/MethodDispatcher.h"
#include "../core/PermissionChecker.h"
#include "../core/Exceptions.h"
#include "../core/Logger.h"
#include "../utils/StringUtils.h"
#include <_scriptapi_module.h>
#include <set>

namespace MCP {

void DumpHandler::RegisterMethods() {
    auto& dispatcher = MethodDispatcher::Instance();
    
    dispatcher.RegisterMethod("dump.module", DumpModule);
    dispatcher.RegisterMethod("dump.memory_region", DumpMemoryRegion);
    dispatcher.RegisterMethod("dump.auto_unpack", AutoUnpackAndDump);
    dispatcher.RegisterMethod("dump.analyze_module", AnalyzeModule);
    dispatcher.RegisterMethod("dump.detect_oep", DetectOEP);
    dispatcher.RegisterMethod("dump.get_dumpable_regions", GetDumpableRegions);
    dispatcher.RegisterMethod("dump.fix_imports", FixImports);
    dispatcher.RegisterMethod("dump.rebuild_pe", RebuildPE);
}

nlohmann::json DumpHandler::DumpModule(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsMemoryWriteAllowed()) {
        throw PermissionDeniedException("Dumping module requires write permission");
    }
    
    // 楠岃瘉鍙傛暟
    if (!params.contains("module")) {
        throw InvalidParamsException("Missing required parameter: module");
    }
    
    if (!params.contains("output_path")) {
        throw InvalidParamsException("Missing required parameter: output_path");
    }
    
    std::string module = params["module"].get<std::string>();
    std::string outputPath = params["output_path"].get<std::string>();
    
    // 瑙ｆ瀽閫夐」
    DumpOptions options;
    if (params.contains("options")) {
        const auto& opts = params["options"];
        options.fixImports = opts.value("fix_imports", true);
        options.fixRelocations = opts.value("fix_relocations", false);
        options.fixOEP = opts.value("fix_oep", true);
        options.removeIntegrityCheck = opts.value("remove_integrity_check", true);
        options.rebuildPE = opts.value("rebuild_pe", true);
        options.autoDetectOEP = opts.value("auto_detect_oep", false);
        options.dumpFullImage = opts.value("dump_full_image", false);
    }
    
    auto& manager = DumpManager::Instance();
    
    // 鎵цdump
    auto result = manager.DumpModule(module, outputPath, options, nullptr);
    
    return DumpResultToJson(result);
}

nlohmann::json DumpHandler::DumpMemoryRegion(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsMemoryWriteAllowed()) {
        throw PermissionDeniedException("Dumping memory requires write permission");
    }
    
    // 楠岃瘉鍙傛暟
    if (!params.contains("address")) {
        throw InvalidParamsException("Missing required parameter: address");
    }
    
    if (!params.contains("size")) {
        throw InvalidParamsException("Missing required parameter: size");
    }
    
    if (!params.contains("output_path")) {
        throw InvalidParamsException("Missing required parameter: output_path");
    }
    
    std::string addressStr = params["address"].get<std::string>();
    uint64_t address = StringUtils::ParseAddress(addressStr);
    size_t size = params["size"].get<size_t>();
    std::string outputPath = params["output_path"].get<std::string>();
    bool asRawBinary = params.value("as_raw_binary", false);
    
    auto& manager = DumpManager::Instance();
    auto result = manager.DumpMemoryRegion(address, size, outputPath, asRawBinary);
    
    return DumpResultToJson(result);
}

nlohmann::json DumpHandler::AutoUnpackAndDump(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsMemoryWriteAllowed()) {
        throw PermissionDeniedException("Auto-unpacking requires write permission");
    }
    
    // 楠岃瘉鍙傛暟
    if (!params.contains("module")) {
        throw InvalidParamsException("Missing required parameter: module");
    }
    
    if (!params.contains("output_path")) {
        throw InvalidParamsException("Missing required parameter: output_path");
    }
    
    std::string module = params["module"].get<std::string>();
    std::string outputPath = params["output_path"].get<std::string>();
    int maxIterations = params.value("max_iterations", 3);
    std::string strategy = params.value("strategy", "entropy");

    static const std::set<std::string> validStrategies = {
        "entropy", "code_analysis", "api_calls", "tls", "entrypoint"
    };
    if (validStrategies.find(strategy) == validStrategies.end()) {
        throw InvalidParamsException(
            "Invalid strategy '" + strategy + "'. Valid strategies: entropy, code_analysis, api_calls, tls, entrypoint"
        );
    }
    
    auto& manager = DumpManager::Instance();
    auto result = manager.AutoUnpackAndDump(module, outputPath, maxIterations, strategy, nullptr);
    
    nlohmann::json response = DumpResultToJson(result);
    
    // 娣诲姞棰濆鐨勮嚜鍔ㄨ劚澹充俊鎭?
    if (result.success && result.newEP != 0) {
        response["detected_oep"] = StringUtils::FormatAddress(result.newEP);
    }
    
    return response;
}

nlohmann::json DumpHandler::AnalyzeModule(const nlohmann::json& params) {
    std::string module;
    
    // 濡傛灉娌℃湁鎻愪緵 module 鍙傛暟,浣跨敤涓绘ā鍧?
    if (!params.contains("module") || params["module"].is_null()) {
        Script::Module::ModuleInfo info;
        if (Script::Module::GetMainModuleInfo(&info)) {
            module = info.name;
            Logger::Info("No module specified, using main module: {}", module);
        } else {
            throw MCPException("No module specified and failed to get main module", -32000);
        }
    } else {
        module = params["module"].get<std::string>();
    }
    
    auto& manager = DumpManager::Instance();
    auto info = manager.AnalyzeModule(module);
    
    return ModuleDumpInfoToJson(info);
}

nlohmann::json DumpHandler::DetectOEP(const nlohmann::json& params) {
    // 楠岃瘉鍙傛暟
    if (!params.contains("module")) {
        throw InvalidParamsException("Missing required parameter: module");
    }
    
    std::string moduleStr = params["module"].get<std::string>();
    std::string strategy = params.value("strategy", "entropy");
    
    // 楠岃瘉绛栫暐鍙傛暟
    static const std::set<std::string> validStrategies = {
        "entropy", "code_analysis", "api_calls", "tls", "entrypoint"
    };
    
    if (validStrategies.find(strategy) == validStrategies.end()) {
        throw InvalidParamsException(
            "Invalid strategy '" + strategy + "'. Valid strategies: entropy, code_analysis, api_calls, tls, entrypoint"
        );
    }
    
    auto& manager = DumpManager::Instance();
    
    // 瑙ｆ瀽妯″潡鍚嶆垨鍦板潃
    auto moduleBaseOpt = manager.ParseModuleOrAddress(moduleStr);
    if (!moduleBaseOpt.has_value()) {
        throw InvalidParamsException("Invalid module name or address: " + moduleStr);
    }
    
    uint64_t moduleBase = moduleBaseOpt.value();
    auto oepOpt = manager.DetectOEP(moduleBase, strategy);
    
    nlohmann::json result;
    result["detected"] = oepOpt.has_value();
    result["strategy"] = strategy;
    
    if (oepOpt.has_value()) {
        uint64_t oep = oepOpt.value();
        result["oep"] = StringUtils::FormatAddress(oep);
        result["rva"] = StringUtils::FormatAddress(oep - moduleBase);
    } else {
        result["oep"] = nullptr;
        result["rva"] = nullptr;
    }
    
    return result;
}

nlohmann::json DumpHandler::GetDumpableRegions(const nlohmann::json& params) {
    uint64_t moduleBase = 0;
    
    if (params.contains("module_base")) {
        std::string baseStr = params["module_base"].get<std::string>();
        moduleBase = StringUtils::ParseAddress(baseStr);
    }
    
    auto& manager = DumpManager::Instance();
    auto regions = manager.GetDumpableRegions(moduleBase);
    
    nlohmann::json regionArray = nlohmann::json::array();
    for (const auto& region : regions) {
        regionArray.push_back(MemoryRegionDumpToJson(region));
    }
    
    nlohmann::json result;
    result["regions"] = regionArray;
    result["count"] = regions.size();
    
    return result;
}

nlohmann::json DumpHandler::FixImports(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsMemoryWriteAllowed()) {
        throw PermissionDeniedException("Fixing imports requires write permission");
    }
    
    // 楠岃瘉鍙傛暟
    if (!params.contains("module_base")) {
        throw InvalidParamsException("Missing required parameter: module_base");
    }
    
    if (!params.contains("buffer")) {
        throw InvalidParamsException("Missing required parameter: buffer");
    }
    
    std::string baseStr = params["module_base"].get<std::string>();
    uint64_t moduleBase = StringUtils::ParseAddress(baseStr);
    
    // 浠嶫SON鏁扮粍杞崲涓哄瓧鑺傚悜閲?
    std::vector<uint8_t> buffer;
    for (const auto& byte : params["buffer"]) {
        buffer.push_back(byte.get<uint8_t>());
    }
    
    bool useScylla = params.value("use_scylla", false);
    
    auto& manager = DumpManager::Instance();
    bool success;
    
    if (useScylla) {
        success = manager.ScyllaRebuildImports(moduleBase, buffer);
    } else {
        success = manager.FixImportTable(moduleBase, buffer);
    }
    
    nlohmann::json result;
    result["success"] = success;
    
    if (success) {
        // 杞崲鍥濲SON鏁扮粍
        nlohmann::json bufferArray = nlohmann::json::array();
        for (uint8_t byte : buffer) {
            bufferArray.push_back(byte);
        }
        result["fixed_buffer"] = bufferArray;
    }
    
    return result;
}

nlohmann::json DumpHandler::RebuildPE(const nlohmann::json& params) {
    // 妫€鏌ュ啓鏉冮檺
    if (!PermissionChecker::Instance().IsMemoryWriteAllowed()) {
        throw PermissionDeniedException("Rebuilding PE requires write permission");
    }
    
    // 楠岃瘉鍙傛暟
    if (!params.contains("module_base")) {
        throw InvalidParamsException("Missing required parameter: module_base");
    }
    
    if (!params.contains("buffer")) {
        throw InvalidParamsException("Missing required parameter: buffer");
    }
    
    std::string baseStr = params["module_base"].get<std::string>();
    uint64_t moduleBase = StringUtils::ParseAddress(baseStr);
    
    // 浠嶫SON鏁扮粍杞崲涓哄瓧鑺傚悜閲?
    std::vector<uint8_t> buffer;
    for (const auto& byte : params["buffer"]) {
        buffer.push_back(byte.get<uint8_t>());
    }
    
    std::optional<uint32_t> newEP;
    if (params.contains("new_ep")) {
        std::string epStr = params["new_ep"].get<std::string>();
        newEP = static_cast<uint32_t>(StringUtils::ParseAddress(epStr));
    }
    
    auto& manager = DumpManager::Instance();
    bool success = manager.RebuildPEHeaders(moduleBase, buffer, newEP);
    
    nlohmann::json result;
    result["success"] = success;
    
    if (success) {
        // 杞崲鍥濲SON鏁扮粍
        nlohmann::json bufferArray = nlohmann::json::array();
        for (uint8_t byte : buffer) {
            bufferArray.push_back(byte);
        }
        result["fixed_buffer"] = bufferArray;
    }
    
    return result;
}

// ========== 杈呭姪鏂规硶 ==========

nlohmann::json DumpHandler::DumpResultToJson(const DumpResult& result) {
    nlohmann::json json;
    
    json["success"] = result.success;
    
    if (result.success) {
        json["file_path"] = result.filePath;
        json["dumped_size"] = result.dumpedSize;
        
        if (result.originalEP != 0) {
            json["original_ep"] = StringUtils::FormatAddress(result.originalEP);
        }
        
        if (result.newEP != 0) {
            json["new_ep"] = StringUtils::FormatAddress(result.newEP);
        }
        
        // 杩涘害淇℃伅
        json["stage"] = static_cast<int>(result.finalProgress.stage);
        json["progress"] = result.finalProgress.progress;
        json["message"] = result.finalProgress.message;
    } else {
        json["error"] = result.error;
        json["stage"] = static_cast<int>(result.finalProgress.stage);
        json["message"] = result.finalProgress.message;
    }
    
    return json;
}

nlohmann::json DumpHandler::ModuleDumpInfoToJson(const ModuleDumpInfo& info) {
    nlohmann::json json;
    
    json["name"] = info.name;
    json["path"] = info.path;
    json["base_address"] = StringUtils::FormatAddress(info.baseAddress);
    json["size"] = info.size;
    json["entry_point"] = StringUtils::FormatAddress(info.entryPoint);
    json["is_packed"] = info.isPacked;
    json["packer_id"] = info.packerId;
    
    return json;
}

nlohmann::json DumpHandler::MemoryRegionDumpToJson(const MemoryRegionDump& region) {
    nlohmann::json json;
    
    json["address"] = StringUtils::FormatAddress(region.address);
    json["size"] = region.size;
    json["protection"] = region.protection;
    json["type"] = region.type;
    json["name"] = region.name;
    
    return json;
}

} // namespace MCP

