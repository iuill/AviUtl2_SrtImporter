#include <windows.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <memory>
#include <utility>
#include <sstream>

#include "plugin2.h"
#include "logger2.h"

// SRTインポート + 設定UI付き実装
// 前提: UTF-8/CRLFのみ対応。時間→フレームは切り捨て。

static HOST_APP_TABLE* g_host = nullptr;
static EDIT_HANDLE* g_edit = nullptr;
static LOG_HANDLE* g_logger = nullptr;

struct UiControls {
    HWND hwnd{};
    HWND editLayer{};
    HWND editX{};
    HWND editY{};
    HWND editSize{};
    HWND editFont{};
    HWND editColor{};
    HWND editOutline{};
    HWND checkOutline{};
};
static UiControls g_ui{};

struct Settings {
    int layer = 1;
    double x = 0.0;
    double y = 0.0;
    double size = 40.0;
    std::wstring font = L"Yu Gothic UI";
    std::string color = "ffffff";
    std::string outline = "000000";
    bool outline_enabled = true;
};

struct SrtEntry {
    int start_frame{};
    int end_frame{};
    std::string text_utf8;
};

// 前方宣言
static void on_import_menu(EDIT_SECTION* edit);
static void on_config_menu(HWND hwnd, HINSTANCE dll_hinst);
static void register_window_client();
static std::vector<SrtEntry> parse_srt(const std::wstring& path, int rate, int scale);
static Settings read_settings_from_ui();
static void apply_entries_to_timeline(const std::vector<SrtEntry>& entries, const Settings& cfg, EDIT_SECTION* edit);
static std::string build_alias(const SrtEntry& e, const Settings& cfg);

//---------------------------------------------------------------------
// ログ出力機能初期化 (任意)
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* logger) {
    g_logger = logger;
}

//---------------------------------------------------------------------
// プラグインDLL初期化 (任意)
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
    (void)version;
    return true;
}

//---------------------------------------------------------------------
// プラグインDLL解放 (任意)
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) void UninitializePlugin() {
}

//---------------------------------------------------------------------
// プラグイン登録
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    g_host = host;
    if (!g_host) return;

    g_host->set_plugin_information(L"SRT Importer");

    // プロジェクト操作用ハンドル
    g_edit = g_host->create_edit_handle();

    // インポートメニューを登録 (ファイル→インポート)
    g_host->register_import_menu(L"Import SRT", on_import_menu);

    // 設定メニュー (設定→SRT Importer)
    g_host->register_config_menu(L"SRT Importer Settings", on_config_menu);

    // 独自ウィンドウクライアント
    register_window_client();
}

//---------------------------------------------------------------------
// SRTパース (UTF-8/CRLF前提)
//---------------------------------------------------------------------
static double parse_time_to_seconds(const std::string& s) {
    // 00:00:00,000 フォーマット
    int hh = 0, mm = 0, ss = 0, ms = 0;
    if (sscanf(s.c_str(), "%d:%d:%d,%d", &hh, &mm, &ss, &ms) != 4) return -1.0;
    return hh * 3600.0 + mm * 60.0 + ss + ms / 1000.0;
}

static std::vector<SrtEntry> parse_srt(const std::wstring& path, int rate, int scale) {
    std::vector<SrtEntry> out;

    // wfstream がパス非対応の環境があるため、_wfopen で読み込む
    std::string data;
    {
        FILE* fp = _wfopen(path.c_str(), L"rb");
        if (!fp) return out;
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (size < 0) { fclose(fp); return out; }
        data.resize((size_t)size);
        if (size > 0) fread(&data[0], 1, (size_t)size, fp);
        fclose(fp);
    }

    // 簡易UTF-8チェック: BOMがあればスキップ
    size_t pos = 0;
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        pos = 3;
    }
    data = data.substr(pos);

    // CRLF前提。CRを除去してLFでsplit
    std::string cleaned;
    cleaned.reserve(data.size());
    for (char c : data) if (c != '\r') cleaned.push_back(c);

    std::vector<std::string> lines;
    std::stringstream ss(cleaned);
    std::string line;
    while (std::getline(ss, line, '\n')) {
        lines.push_back(line);
    }

    size_t i = 0;
    auto to_frame = [&](double sec) -> int {
        double f = sec * rate / scale;
        return (int)std::floor(f); // 切り捨て
    };

    while (i < lines.size()) {
        // インデックス行をスキップ（内容は使わない）
        if (lines[i].empty()) { i++; continue; }
        i++;
        if (i >= lines.size()) break;

        // 時刻行
        const auto& tl = lines[i];
        auto arrow = tl.find("-->");
        if (arrow == std::string::npos) { i++; continue; }
        std::string start_str = tl.substr(0, arrow);
        std::string end_str = tl.substr(arrow + 3);
        // trim spaces
        auto trim = [](std::string& s) {
            while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
        };
        trim(start_str);
        trim(end_str);
        double start_sec = parse_time_to_seconds(start_str);
        double end_sec = parse_time_to_seconds(end_str);
        i++;

        // テキスト行
        std::string text;
        while (i < lines.size() && !lines[i].empty()) {
            if (!text.empty()) text += '\n';
            text += lines[i];
            i++;
        }

        if (start_sec < 0 || end_sec < 0 || end_sec <= start_sec || text.empty()) {
            continue;
        }

        SrtEntry e;
        e.start_frame = to_frame(start_sec);
        e.end_frame = std::max(to_frame(end_sec), e.start_frame + 1);
        e.text_utf8 = text;
        out.push_back(std::move(e));
    }

    return out;
}

//---------------------------------------------------------------------
// インポートメニュー選択時コールバック
//---------------------------------------------------------------------
static void handle_import(HWND owner) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"SRT Files (*.srt)\0*.srt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;

    if (!g_edit) return;
    auto cfg = read_settings_from_ui();

    // プロジェクト操作をまとめて行う
    g_edit->call_edit_section_param(new std::wstring(path), [](void* param, EDIT_SECTION* edit) {
        std::unique_ptr<std::wstring> path_ptr((std::wstring*)param);
        auto entries = parse_srt(*path_ptr, edit->info->rate, edit->info->scale);
        if (entries.empty()) {
            if (g_logger) g_logger->warn(g_logger, L"SRT parse failed or empty");
            MessageBox(edit->info ? nullptr : nullptr, L"SRTの内容が空か、読み込みに失敗しました。(UTF-8/CRLFのみ対応)", L"SRT Import", MB_OK | MB_ICONWARNING);
            return;
        }
        Settings cfg_local = read_settings_from_ui();
        apply_entries_to_timeline(entries, cfg_local, edit);
        if (g_logger) g_logger->info(g_logger, L"SRT import completed");
    });
}

static void on_import_menu(EDIT_SECTION* edit) {
    (void)edit;
    handle_import(nullptr);
}

//---------------------------------------------------------------------
// 設定メニュー (注意書き表示のみ)
//---------------------------------------------------------------------
static void on_config_menu(HWND hwnd, HINSTANCE dll_hinst) {
    MessageBox(hwnd, L"SRT Importer\n- UTF-8/CRLF のみ対応\n- 時刻→フレームは切り捨て\n- ウィンドウからレイヤー/色/位置を設定してください", L"SRT Importer", MB_OK | MB_ICONINFORMATION);
    (void)dll_hinst;
}

//---------------------------------------------------------------------
// テキストオブジェクト生成
//---------------------------------------------------------------------
static bool set_item(EDIT_SECTION* edit, OBJECT_HANDLE obj, LPCWSTR effect, LPCWSTR item, const std::string& value) {
    return edit->set_object_item_value(obj, effect, item, value.c_str());
}
static bool set_item_w(EDIT_SECTION* edit, OBJECT_HANDLE obj, LPCWSTR effect, LPCWSTR item, const std::wstring& value) {
    std::string utf8;
    // 簡易UTF-8変換 (Win32のWideCharToMultiByte)
    int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return false;
    utf8.resize(len - 1);
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), len, nullptr, nullptr);
    return edit->set_object_item_value(obj, effect, item, utf8.c_str());
}

static void apply_entries_to_timeline(const std::vector<SrtEntry>& entries, const Settings& cfg, EDIT_SECTION* edit) {
    const int target_layer = std::max(0, cfg.layer - 1); // UIは1始まり、APIは0始まり
    for (const auto& e : entries) {
        int length = std::max(1, e.end_frame - e.start_frame);
        auto alias = build_alias(e, cfg);
        OBJECT_HANDLE obj = edit->create_object_from_alias(alias.c_str(), target_layer, e.start_frame, length);
        if (!obj) {
            if (g_logger) g_logger->warn(g_logger, L"create_object_from_alias failed");
            continue;
        }
    }
}

//---------------------------------------------------------------------
// UIヘルパ
//---------------------------------------------------------------------
static std::wstring get_window_text(HWND h) {
    int len = GetWindowTextLengthW(h);
    std::wstring s(len, L'\0');
    if (len > 0) {
        GetWindowTextW(h, &s[0], len + 1);
    }
    return s;
}
static double to_double(const std::wstring& s, double def) {
    try {
        return std::stod(s);
    } catch (...) {
        return def;
    }
}
static int to_int(const std::wstring& s, int def) {
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}
static std::string narrow_utf8(const std::wstring& ws) {
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

static Settings read_settings_from_ui() {
    Settings cfg;
    if (!g_ui.hwnd) return cfg;
    cfg.layer = to_int(get_window_text(g_ui.editLayer), cfg.layer);
    cfg.x = to_double(get_window_text(g_ui.editX), cfg.x);
    cfg.y = to_double(get_window_text(g_ui.editY), cfg.y);
    cfg.size = to_double(get_window_text(g_ui.editSize), cfg.size);
    cfg.font = get_window_text(g_ui.editFont);
    cfg.color = narrow_utf8(get_window_text(g_ui.editColor));
    cfg.outline = narrow_utf8(get_window_text(g_ui.editOutline));
    cfg.outline_enabled = g_ui.checkOutline
        ? (SendMessage(g_ui.checkOutline, BM_GETCHECK, 0, 0) == BST_CHECKED)
        : true;
    if (cfg.color.empty()) cfg.color = "ffffff";
    if (cfg.outline.empty()) cfg.outline = "000000";
    return cfg;
}

//---------------------------------------------------------------------
// alias生成: テキスト + 標準描画 + 縁取り
//---------------------------------------------------------------------
static std::string build_alias(const SrtEntry& e, const Settings& cfg) {
    // 数値は少数2桁程度に丸めて文字列化
    auto num_to_str = [](double v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return std::string(buf);
    };
    auto to_crlf = [](const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '\n') {
                if (i > 0 && s[i - 1] == '\r') {
                    out.push_back('\n');
                } else {
                    out.push_back('\r');
                    out.push_back('\n');
                }
            } else if (c != '\r') {
                out.push_back(c);
            }
        }
        // 末尾に改行が無ければCRLFを追加
        if (out.empty() || out.back() != '\n') {
            out.push_back('\r');
            out.push_back('\n');
        }
        return out;
    };

    std::ostringstream oss;
    oss << "[Object]\r\n";

    // Object.0 テキスト (本文/フォント/色/サイズ)
    oss << "[Object.0]\r\n";
    oss << "effect.name=テキスト\r\n";
    oss << "サイズ=" << num_to_str(cfg.size) << "\r\n";
    oss << "文字色=" << cfg.color << "\r\n";
    {
        std::string font_utf8 = narrow_utf8(cfg.font);
        if (!font_utf8.empty()) {
            oss << "フォント=" << font_utf8 << "\r\n";
        }
    }
    oss << "テキスト=" << to_crlf(e.text_utf8);

    // Object.1 標準描画 (位置)
    oss << "[Object.1]\r\n";
    oss << "effect.name=標準描画\r\n";
    oss << "X=" << num_to_str(cfg.x) << "\r\n";
    oss << "Y=" << num_to_str(cfg.y) << "\r\n";

    // Object.2 縁取り (縁色) ※任意
    if (cfg.outline_enabled) {
        oss << "[Object.2]\r\n";
        oss << "effect.name=縁取り\r\n";
        oss << "サイズ=3\r\n";
        oss << "縁色=" << cfg.outline << "\r\n";
    }

    return oss.str();
}

//---------------------------------------------------------------------
// シンプルなクライアントウィンドウ
//---------------------------------------------------------------------
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE: {
        g_ui.hwnd = hwnd;
        int x = 10, y = 10, label_w = 80, edit_w = 120, h = 22, gap = 4;
        CreateWindowExW(0, L"STATIC", L"レイヤー", WS_CHILD | WS_VISIBLE, x, y, label_w, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        g_ui.editLayer = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x + label_w + 5, y, 60, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        y += h + gap;
        CreateWindowExW(0, L"STATIC", L"X", WS_CHILD | WS_VISIBLE, x, y, label_w, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        g_ui.editX = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x + label_w + 5, y, 80, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        y += h + gap;
        CreateWindowExW(0, L"STATIC", L"Y", WS_CHILD | WS_VISIBLE, x, y, label_w, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        g_ui.editY = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x + label_w + 5, y, 80, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        y += h + gap;
        CreateWindowExW(0, L"STATIC", L"サイズ", WS_CHILD | WS_VISIBLE, x, y, label_w, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        g_ui.editSize = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"40", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x + label_w + 5, y, 80, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        y += h + gap;
        CreateWindowExW(0, L"STATIC", L"フォント", WS_CHILD | WS_VISIBLE, x, y, label_w, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        g_ui.editFont = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Yu Gothic UI", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x + label_w + 5, y, edit_w + 80, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        y += h + gap;
        CreateWindowExW(0, L"STATIC", L"文字色", WS_CHILD | WS_VISIBLE, x, y, label_w, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        g_ui.editColor = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"ffffff", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x + label_w + 5, y, 100, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        y += h + gap;
        CreateWindowExW(0, L"STATIC", L"影・縁色", WS_CHILD | WS_VISIBLE, x, y, label_w, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        g_ui.editOutline = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"000000", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x + label_w + 5, y, 100, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        g_ui.checkOutline = CreateWindowExW(0, L"BUTTON", L"縁取りを有効", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            x + label_w + 5 + 110, y, 120, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessage(g_ui.checkOutline, BM_SETCHECK, BST_CHECKED, 0);
        y += h + gap;

        CreateWindowExW(0, L"STATIC", L"※UTF-8/CRLFのみ対応", WS_CHILD | WS_VISIBLE, x, y, 200, h, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        y += h + gap;

        CreateWindowExW(0, L"BUTTON", L"Import SRT...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, 180, 28, hwnd, (HMENU)1001, GetModuleHandle(nullptr), nullptr);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == 1001) {
            handle_import(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (hwnd == g_ui.hwnd) g_ui = {};
        break;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void register_window_client() {
    static const wchar_t kClassName[] = L"SRTImporterWindow";

    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(wcex);
    wcex.lpfnWndProc = wnd_proc;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.lpszClassName = kClassName;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wcex)) return;

    auto hwnd = CreateWindowExW(
        0,
        kClassName,
        L"SRT Importer",
        WS_POPUP, // register_window_clientでWS_CHILDが付与される
        CW_USEDEFAULT, CW_USEDEFAULT, 340, 320,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!hwnd) return;

    g_host->register_window_client(L"SRT Importer", hwnd);
}
