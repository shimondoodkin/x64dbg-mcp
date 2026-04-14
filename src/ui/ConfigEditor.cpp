#include "ConfigEditor.h"
#include "resource.h"
#include "../core/Logger.h"
#include "../core/ConfigManager.h"
#include <commctrl.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <filesystem>
#include <unordered_map>

namespace {
// Scroll-state kept in file scope because the dialog is modal and single-instance.
std::unordered_map<HWND, int> g_originalY;   // original client-Y of each child control
int g_scrollPos = 0;                          // current scroll offset (pixels)
int g_scrollMax = 0;                          // max scroll offset (pixels)
int g_visibleHeight = 0;                      // client height we fit the dialog into

BOOL CALLBACK RecordChildY(HWND hChild, LPARAM lParam) {
    HWND hDlg = reinterpret_cast<HWND>(lParam);
    // Only record direct children of the dialog, not internals of composite
    // controls like the listbox's own scrollbars.
    if (GetParent(hChild) != hDlg) return TRUE;
    RECT rc;
    GetWindowRect(hChild, &rc);
    POINT tl{ rc.left, rc.top };
    ScreenToClient(hDlg, &tl);
    g_originalY[hChild] = tl.y;
    return TRUE;
}

struct ApplyScrollCtx { HWND hDlg; int delta; };

BOOL CALLBACK ApplyScrollToChild(HWND hChild, LPARAM lParam) {
    auto* ctx = reinterpret_cast<ApplyScrollCtx*>(lParam);
    auto it = g_originalY.find(hChild);
    if (it == g_originalY.end()) return TRUE;
    RECT rc;
    GetWindowRect(hChild, &rc);
    POINT tl{ rc.left, rc.top };
    ScreenToClient(ctx->hDlg, &tl);
    const int newY = it->second - g_scrollPos;
    (void)ctx->delta;
    SetWindowPos(hChild, nullptr, tl.x, newY, 0, 0,
                 SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    return TRUE;
}

void UpdateScroll(HWND hDlg, int newPos) {
    if (newPos < 0) newPos = 0;
    if (newPos > g_scrollMax) newPos = g_scrollMax;
    if (newPos == g_scrollPos) return;
    g_scrollPos = newPos;

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_POS;
    si.nPos = g_scrollPos;
    SetScrollInfo(hDlg, SB_VERT, &si, TRUE);

    ApplyScrollCtx ctx{ hDlg, 0 };
    EnumChildWindows(hDlg, ApplyScrollToChild, reinterpret_cast<LPARAM>(&ctx));
    InvalidateRect(hDlg, nullptr, TRUE);
}

void SetupScrollingIfNeeded(HWND hDlg) {
    g_originalY.clear();
    g_scrollPos = 0;
    g_scrollMax = 0;

    // Record every child's original client Y, then compute content height.
    EnumChildWindows(hDlg, RecordChildY, reinterpret_cast<LPARAM>(hDlg));

    int contentBottom = 0;
    for (const auto& kv : g_originalY) {
        RECT rc;
        GetWindowRect(kv.first, &rc);
        POINT br{ rc.right, rc.bottom };
        ScreenToClient(hDlg, &br);
        if (br.y > contentBottom) contentBottom = br.y;
    }
    contentBottom += 10; // bottom padding

    // Find the screen work area (excludes taskbar).
    HMONITOR hMon = MonitorFromWindow(hDlg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(hMon, &mi);
    const int workH = mi.rcWork.bottom - mi.rcWork.top;

    // Current window size incl. caption/frame.
    RECT winRc;
    GetWindowRect(hDlg, &winRc);
    const int winH = winRc.bottom - winRc.top;

    RECT clientRc;
    GetClientRect(hDlg, &clientRc);
    const int frameChrome = winH - (clientRc.bottom - clientRc.top);

    // If the dialog fits, leave everything alone.
    const int maxAllowedWinH = workH - 40; // leave some room
    if (winH <= maxAllowedWinH && contentBottom <= clientRc.bottom) {
        g_visibleHeight = clientRc.bottom;
        return;
    }

    // Shrink the window to fit the screen.
    const int newWinH = (winH > maxAllowedWinH) ? maxAllowedWinH : winH;
    const int newClientH = newWinH - frameChrome;
    SetWindowPos(hDlg, nullptr, 0, 0, winRc.right - winRc.left, newWinH,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);

    g_visibleHeight = newClientH;
    g_scrollMax = contentBottom - newClientH;
    if (g_scrollMax < 0) g_scrollMax = 0;

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = contentBottom;   // range is content height; nPage shrinks thumb
    si.nPage = newClientH;
    si.nPos = 0;
    SetScrollInfo(hDlg, SB_VERT, &si, TRUE);
}
} // anonymous namespace

#pragma comment(lib, "comctl32.lib")

namespace MCP {

std::string ConfigEditor::s_configPath;
json ConfigEditor::s_config;

bool ConfigEditor::Show(HMODULE hModule, HWND parentWindow, const std::string& configPath) {
    s_configPath = configPath;
    
    // 添加调试日志
    Logger::Info("ConfigEditor::Show called with configPath: {}", configPath);
    
    // 加载配置
    try {
        std::ifstream file(configPath);
        if (file.is_open()) {
            file >> s_config;
            file.close();
            if (!s_config.is_object()) {
                Logger::Warning("Config root is not a JSON object, fallback to default config");
                s_config = ConfigManager::Instance().GetDefaultConfig();
            }
            Logger::Info("Config file loaded successfully");
        } else {
            // 如果配置文件不存在,使用默认配置
            Logger::Warning("Config file not found, using default configuration: {}", configPath);
            s_config = ConfigManager::Instance().GetDefaultConfig();
            
            // 尝试创建配置文件目录
            std::filesystem::path path(configPath);
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
        }
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse config: {}", e.what());
        MessageBoxA(parentWindow, "Failed to parse config file!", "Error", MB_OK | MB_ICONERROR);
        return false;
    }
    
    Logger::Info("Creating dialog with module handle: 0x{:X}", reinterpret_cast<uintptr_t>(hModule));
    
    // 创建对话框
    INT_PTR result = DialogBoxParamA(
        hModule,  // 使用插件的模块句柄
        MAKEINTRESOURCEA(IDD_CONFIG_EDITOR),
        parentWindow,
        DialogProc,
        0
    );
    
    if (result == -1) {
        DWORD error = GetLastError();
        Logger::Error("DialogBoxParamA failed with error: {}", error);
        char errorMsg[256];
        sprintf_s(errorMsg, "Failed to create dialog! Error code: %lu", error);
        MessageBoxA(parentWindow, errorMsg, "Error", MB_OK | MB_ICONERROR);
        return false;
    }
    
    Logger::Info("Dialog closed with result: {}", result);
    return result == IDOK;
}

INT_PTR CALLBACK ConfigEditor::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (uMsg) {
        case WM_INITDIALOG: {
            // 设置对话框标题
            SetWindowTextA(hwndDlg, "MCP Server Configuration Editor");
            
            // 初始化控件
            LoadConfigToControls(hwndDlg, s_config);

            // If the dialog is taller than the screen, shrink it and enable scrolling.
            SetupScrollingIfNeeded(hwndDlg);

            // 居中显示 (use the possibly-resized dimensions)
            RECT rc;
            GetWindowRect(hwndDlg, &rc);
            int x = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
            int y = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;
            if (y < 0) y = 0;
            SetWindowPos(hwndDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

            return TRUE;
        }

        case WM_VSCROLL: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwndDlg, SB_VERT, &si);
            int newPos = si.nPos;
            const int lineStep = 20;
            const int pageStep = (si.nPage > 0) ? static_cast<int>(si.nPage) - lineStep : 100;
            switch (LOWORD(wParam)) {
                case SB_LINEUP:        newPos -= lineStep; break;
                case SB_LINEDOWN:      newPos += lineStep; break;
                case SB_PAGEUP:        newPos -= pageStep; break;
                case SB_PAGEDOWN:      newPos += pageStep; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: newPos = si.nTrackPos; break;
                case SB_TOP:           newPos = 0; break;
                case SB_BOTTOM:        newPos = g_scrollMax; break;
            }
            UpdateScroll(hwndDlg, newPos);
            return TRUE;
        }

        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int lines = -delta / 40; // ~3 lines per notch at default res.
            UpdateScroll(hwndDlg, g_scrollPos + lines * 10);
            return TRUE;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDOK_SAVE: {
                    if (SaveConfig(hwndDlg, s_configPath)) {
                        MessageBoxA(hwndDlg, "Configuration saved successfully!", "Success", MB_OK | MB_ICONINFORMATION);
                        EndDialog(hwndDlg, IDOK);
                    } else {
                        MessageBoxA(hwndDlg, "Failed to save configuration!", "Error", MB_OK | MB_ICONERROR);
                    }
                    return TRUE;
                }
                
                case IDCANCEL_CLOSE: {
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
                }
                
                case IDC_METHOD_ADD: {
                    char methodName[256] = {0};
                    GetDlgItemTextA(hwndDlg, IDC_METHOD_INPUT, methodName, sizeof(methodName));
                    
                    if (strlen(methodName) > 0) {
                        // 添加到列表框
                        HWND listBox = GetDlgItem(hwndDlg, IDC_METHODS_LIST);
                        SendMessageA(listBox, LB_ADDSTRING, 0, (LPARAM)methodName);
                        
                        // 清空输入框
                        SetDlgItemTextA(hwndDlg, IDC_METHOD_INPUT, "");
                    }
                    return TRUE;
                }
                
                case IDC_METHOD_REMOVE: {
                    HWND listBox = GetDlgItem(hwndDlg, IDC_METHODS_LIST);
                    LRESULT sel = SendMessageA(listBox, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR) {
                        SendMessageA(listBox, LB_DELETESTRING, static_cast<WPARAM>(sel), 0);
                    }
                    return TRUE;
                }
            }
            break;
        }
        
        case WM_CLOSE: {
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        }
    }
    
    return FALSE;
}

void ConfigEditor::LoadConfigToControls(HWND hwndDlg, const json& config) {
    // Server settings
    SetDlgItemTextA(hwndDlg, IDC_SERVER_ADDRESS, 
        config.value("server", json::object()).value("address", "127.0.0.1").c_str());
    SetDlgItemInt(hwndDlg, IDC_SERVER_PORT, 
        config.value("server", json::object()).value("port", 3000), FALSE);
    
    // Permissions
    auto perms = config.value("permissions", json::object());
    CheckDlgButton(hwndDlg, IDC_ALLOW_MEMORY_WRITE, 
        perms.value("allow_memory_write", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_ALLOW_REGISTER_WRITE, 
        perms.value("allow_register_write", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_ALLOW_SCRIPT_EXEC, 
        perms.value("allow_script_execution", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_ALLOW_BREAKPOINT_MOD, 
        perms.value("allow_breakpoint_modification", true) ? BST_CHECKED : BST_UNCHECKED);
    
    // Allowed methods
    HWND listBox = GetDlgItem(hwndDlg, IDC_METHODS_LIST);
    SendMessageA(listBox, LB_RESETCONTENT, 0, 0);
    
    auto methods = perms.value("allowed_methods", json::array());
    for (const auto& method : methods) {
        if (method.is_string()) {
            SendMessageA(listBox, LB_ADDSTRING, 0, (LPARAM)method.get<std::string>().c_str());
        }
    }
    
    // Logging
    auto logging = config.value("logging", json::object());
    CheckDlgButton(hwndDlg, IDC_LOG_ENABLED, 
        logging.value("enabled", true) ? BST_CHECKED : BST_UNCHECKED);
    
    std::string logLevel = logging.value("level", "info");
    HWND comboBox = GetDlgItem(hwndDlg, IDC_LOG_LEVEL);
    SendMessageA(comboBox, CB_RESETCONTENT, 0, 0);
    SendMessageA(comboBox, CB_ADDSTRING, 0, (LPARAM)"debug");
    SendMessageA(comboBox, CB_ADDSTRING, 0, (LPARAM)"info");
    SendMessageA(comboBox, CB_ADDSTRING, 0, (LPARAM)"warning");
    SendMessageA(comboBox, CB_ADDSTRING, 0, (LPARAM)"error");
    
    if (logLevel == "debug") SendMessageA(comboBox, CB_SETCURSEL, 0, 0);
    else if (logLevel == "info") SendMessageA(comboBox, CB_SETCURSEL, 1, 0);
    else if (logLevel == "warning") SendMessageA(comboBox, CB_SETCURSEL, 2, 0);
    else if (logLevel == "error") SendMessageA(comboBox, CB_SETCURSEL, 3, 0);
    
    SetDlgItemTextA(hwndDlg, IDC_LOG_FILE, 
        logging.value("file", "x64dbg_mcp.log").c_str());
    SetDlgItemInt(hwndDlg, IDC_LOG_MAX_SIZE, 
        logging.value("max_file_size_mb", 10), FALSE);
    CheckDlgButton(hwndDlg, IDC_LOG_CONSOLE, 
        logging.value("console_output", true) ? BST_CHECKED : BST_UNCHECKED);
    
    // Timeouts
    auto timeout = config.value("timeout", json::object());
    SetDlgItemInt(hwndDlg, IDC_TIMEOUT_REQUEST, 
        timeout.value("request_timeout_ms", 30000), FALSE);
    SetDlgItemInt(hwndDlg, IDC_TIMEOUT_STEP, 
        timeout.value("step_timeout_ms", 10000), FALSE);
    SetDlgItemInt(hwndDlg, IDC_TIMEOUT_MEMORY, 
        timeout.value("memory_read_timeout_ms", 5000), FALSE);
    
    // Features
    auto features = config.value("features", json::object());
    CheckDlgButton(hwndDlg, IDC_FEATURE_NOTIFICATIONS, 
        features.value("enable_notifications", true) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_FEATURE_HEARTBEAT, 
        features.value("enable_heartbeat", true) ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(hwndDlg, IDC_HEARTBEAT_INTERVAL, 
        features.value("heartbeat_interval_seconds", 30), FALSE);
    CheckDlgButton(hwndDlg, IDC_FEATURE_BATCH, 
        features.value("enable_batch_requests", true) ? BST_CHECKED : BST_UNCHECKED);
}

json ConfigEditor::GetConfigFromControls(HWND hwndDlg) {
    json config;
    
    // Server settings
    char buffer[256];
    GetDlgItemTextA(hwndDlg, IDC_SERVER_ADDRESS, buffer, sizeof(buffer));
    config["server"]["address"] = buffer;
    config["server"]["port"] = GetDlgItemInt(hwndDlg, IDC_SERVER_PORT, NULL, FALSE);
    
    // Permissions
    config["permissions"]["allow_memory_write"] = 
        IsDlgButtonChecked(hwndDlg, IDC_ALLOW_MEMORY_WRITE) == BST_CHECKED;
    config["permissions"]["allow_register_write"] = 
        IsDlgButtonChecked(hwndDlg, IDC_ALLOW_REGISTER_WRITE) == BST_CHECKED;
    config["permissions"]["allow_script_execution"] = 
        IsDlgButtonChecked(hwndDlg, IDC_ALLOW_SCRIPT_EXEC) == BST_CHECKED;
    config["permissions"]["allow_breakpoint_modification"] = 
        IsDlgButtonChecked(hwndDlg, IDC_ALLOW_BREAKPOINT_MOD) == BST_CHECKED;
    
    // Allowed methods
    json methodsArray = json::array();
    HWND listBox = GetDlgItem(hwndDlg, IDC_METHODS_LIST);
    const LRESULT countResult = SendMessageA(listBox, LB_GETCOUNT, 0, 0);
    const int count = (countResult < 0) ? 0 : static_cast<int>(countResult);
    
    for (int i = 0; i < count; i++) {
        char methodName[256];
        SendMessageA(listBox, LB_GETTEXT, i, (LPARAM)methodName);
        methodsArray.push_back(methodName);
    }
    config["permissions"]["allowed_methods"] = methodsArray;
    
    // Logging
    config["logging"]["enabled"] = 
        IsDlgButtonChecked(hwndDlg, IDC_LOG_ENABLED) == BST_CHECKED;
    
    HWND comboBox = GetDlgItem(hwndDlg, IDC_LOG_LEVEL);
    const LRESULT selectedIndex = SendMessageA(comboBox, CB_GETCURSEL, 0, 0);
    const int sel = (selectedIndex == CB_ERR) ? -1 : static_cast<int>(selectedIndex);
    const char* levels[] = {"debug", "info", "warning", "error"};
    config["logging"]["level"] = levels[sel >= 0 && sel < 4 ? sel : 1];
    
    GetDlgItemTextA(hwndDlg, IDC_LOG_FILE, buffer, sizeof(buffer));
    config["logging"]["file"] = buffer;
    config["logging"]["max_file_size_mb"] = 
        GetDlgItemInt(hwndDlg, IDC_LOG_MAX_SIZE, NULL, FALSE);
    config["logging"]["console_output"] = 
        IsDlgButtonChecked(hwndDlg, IDC_LOG_CONSOLE) == BST_CHECKED;
    
    // Timeouts
    config["timeout"]["request_timeout_ms"] = 
        GetDlgItemInt(hwndDlg, IDC_TIMEOUT_REQUEST, NULL, FALSE);
    config["timeout"]["step_timeout_ms"] = 
        GetDlgItemInt(hwndDlg, IDC_TIMEOUT_STEP, NULL, FALSE);
    config["timeout"]["memory_read_timeout_ms"] = 
        GetDlgItemInt(hwndDlg, IDC_TIMEOUT_MEMORY, NULL, FALSE);
    
    // Features
    config["features"]["enable_notifications"] = 
        IsDlgButtonChecked(hwndDlg, IDC_FEATURE_NOTIFICATIONS) == BST_CHECKED;
    config["features"]["enable_heartbeat"] = 
        IsDlgButtonChecked(hwndDlg, IDC_FEATURE_HEARTBEAT) == BST_CHECKED;
    config["features"]["heartbeat_interval_seconds"] = 
        GetDlgItemInt(hwndDlg, IDC_HEARTBEAT_INTERVAL, NULL, FALSE);
    config["features"]["enable_batch_requests"] = 
        IsDlgButtonChecked(hwndDlg, IDC_FEATURE_BATCH) == BST_CHECKED;
    // Keep menu-managed toggle persisted even though it has no dialog control.
    config["features"]["auto_start_mcp_on_plugin_load"] =
        s_config.value("features", json::object()).value("auto_start_mcp_on_plugin_load", false);
    
    // 保留version字段
    config["version"] = s_config.value("version", "1.0.4");
    
    return config;
}

bool ConfigEditor::SaveConfig(HWND hwndDlg, const std::string& configPath) {
    try {
        json newConfig = GetConfigFromControls(hwndDlg);
        
        // 保存到文件
        std::ofstream file(configPath);
        if (!file.is_open()) {
            Logger::Error("Failed to open config file for writing: {}", configPath);
            return false;
        }
        
        file << newConfig.dump(2);  // 格式化输出,缩进2个空格
        file.close();
        
        // 更新全局配置
        s_config = newConfig;
        
        Logger::Info("Configuration saved successfully");
        return true;
        
    } catch (const std::exception& e) {
        Logger::Error("Failed to save config: {}", e.what());
        return false;
    }
}

} // namespace MCP
