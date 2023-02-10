#include "sqflite_sqlcipher_plugin.h"

// This must be included before many other Windows headers.
#include <KnownFolders.h>
#include <shlobj.h>
#include <windows.h>

// For getPlatformVersion; remove unless needed for your plugin implementation.
#include <VersionHelpers.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <codecvt>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#define SQLITE_HAS_CODEC 1
#include <sqlite3.h>
#include <sstream>

#define DEBUG_LOG_FILE 1
#ifdef DEBUG_LOG_FILE
#include <fstream>
#endif

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
// or #include "spdlog/sinks/stdout_sinks.h" if no colors needed.
#include <spdlog/fmt/ranges.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "Database.h"
#include "FlutterUtil.h"

using namespace std::string_literals;
using namespace FlutterUtil;

template<>
struct fmt::formatter<flutter::EncodableValue>
{
    // template<typename T, typename FormatContext>
    // auto format_vector(const std::vector<T> &vec, const FormatContext &ctx) -> decltype(ctx.out())
    // {
    //     auto &&out = ctx.out();
    //     fmt::format_to(out, "[");
    //     bool isFirst = true;
    //     for (auto const &el : vec) {
    //         if (isFirst) {
    //             isFirst = false;
    //         } else {
    //             fmt::format_to(out, ", ");
    //         }
    //         fmt::format_to(out, "{}", el);
    //     }
    //     return fmt::format_to(out, "]");
    // }

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(const flutter::EncodableValue &value, FormatContext &ctx) const
        -> decltype(ctx.out())
    {
        // ctx.out() is an output iterator to write to.
        // return presentation == 'f'
        //           ? fmt::format_to(ctx.out(), "({:.1f}, {:.1f})", p.x, p.y)
        //           : fmt::format_to(ctx.out(), "({:.1e}, {:.1e})", p.x, p.y);

        if (std::holds_alternative<std::monostate>(value)) {
            return fmt::format_to(ctx.out(), "NULL");
        } else if (std::holds_alternative<bool>(value)) {
            return fmt::format_to(ctx.out(), "{}", std::get<bool>(value));
        } else if (std::holds_alternative<int32_t>(value)) {
            return fmt::format_to(ctx.out(), "{}", std::get<int32_t>(value));
        } else if (std::holds_alternative<int64_t>(value)) {
            return fmt::format_to(ctx.out(), "{}", std::get<int64_t>(value));
        } else if (std::holds_alternative<double>(value)) {
            return fmt::format_to(ctx.out(), "{}", std::get<double>(value));
        } else if (std::holds_alternative<std::string>(value)) {
            return fmt::format_to(ctx.out(), "\"{}\"", std::get<std::string>(value));
            // } else if (std::holds_alternative<std::vector<uint8_t>>(value)) {
            //     //return fmt::format_to(ctx.out(), "{}", std::get<std::vector<uint8_t>>(value));
            //     const auto vec = std::get<std::vector<uint8_t>>(value);
            //     return format_vector(vec, ctx);
            // } else if (std::holds_alternative<std::vector<int32_t>>(value)) {
            //     const auto vec = std::get<std::vector<int32_t>>(value);
            //     //output_vector(output, vec);
            //     return fmt::format_to(ctx.out(), "{}", vec);
            // } else if (std::holds_alternative<std::vector<int64_t>>(value)) {
            //     const auto vec = std::get<std::vector<int64_t>>(value);
            //     //output_vector(output, vec);
            //     return fmt::format_to(ctx.out(), "{}", vec);
            // } else if (std::holds_alternative<std::vector<double>>(value)) {
            //     const auto vec = std::get<std::vector<double>>(value);
            //     //output_vector(output, vec);
            //     return fmt::format_to(ctx.out(), "{}", vec);
            // } else if (std::holds_alternative<std::vector<float>>(value)) {
            //     const auto vec = std::get<std::vector<float>>(value);
            //     //output_vector(output, vec);
            //     return fmt::format_to(ctx.out(), "{}", vec);
            // } else if (std::holds_alternative<flutter::EncodableList>(value)) {
            //     const auto vec = std::get<flutter::EncodableList>(value);
            //     return format_vector(vec, ctx);
            //     //output_vector(output, vec);
            //     return fmt::format_to(ctx.out(), "EncodableList({})[{}]", vec.size(), vec);
        } else if (std::holds_alternative<flutter::EncodableMap>(value)) {
            const auto map = std::get<flutter::EncodableMap>(value);
            auto &&out = ctx.out();
            fmt::format_to(out, "{{");
            bool isFirst = true;
            for (auto const &it : map) {
                if (isFirst) {
                    isFirst = false;
                } else {
                    fmt::format_to(out, ", ");
                }
                fmt::format_to(out, "{}: {}", it.first, it.second);
            }
            return fmt::format_to(out, "}}");
            //return fmt::format_to(ctx.out(), "EncodableMap({}){}", map.size(), map);
            //     output << "{";
            //     bool isFirst = true;
            //     for (auto const &it : map) {
            //         if (isFirst) {
            //             isFirst = false;
            //         } else {
            //             output << ", ";
            //         }
            //         output << it.first << ": " << it.second;
            //     }
            //     output << "}";
        } else {
            return fmt::format_to(ctx.out(), "EncodableValue.index({})", value.index());
        }
    }
};

template<>
struct fmt::formatter<flutter::EncodableList>
{
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(const flutter::EncodableList &vec, FormatContext &ctx) const -> decltype(ctx.out())
    {
        auto &&out = ctx.out();
        fmt::format_to(out, "[");
        bool isFirst = true;
        for (auto const &el : vec) {
            if (isFirst) {
                isFirst = false;
            } else {
                fmt::format_to(out, ", ");
            }
            fmt::format_to(out, "{}", el);
        }
        return fmt::format_to(out, "]");
    }
};

template<>
struct fmt::formatter<flutter::EncodableMap>
{
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(const flutter::EncodableMap &map, FormatContext &ctx) const -> decltype(ctx.out())
    {
        auto &&out = ctx.out();
        fmt::format_to(out, "{{");
        bool isFirst = true;
        for (auto const &it : map) {
            if (isFirst) {
                isFirst = false;
            } else {
                fmt::format_to(out, ", ");
            }
            fmt::format_to(out, "{}: {}", it.first, it.second);
        }
        return fmt::format_to(out, "}}");
    }
};

namespace sqflite_sqlcipher {

namespace {

const char *PARAM_PATH = "path";
const char *SQLITE_ERROR_CODE = "sqlite_error";

// https://gist.github.com/rosasurfer/33f0beb4b10ff8a8c53d943116f8a872
std::string utf8_encode(const std::wstring &wstr)
{
    int size_needed
        = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

void HandleGetDatabasesPathCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                                flutter::MethodResult<flutter::EncodableValue> *result)
{
    spdlog::info("GetDatabasesPathCall()");
    PWSTR path;
    SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path);
    std::wstringstream ss;
    ss << path;
    result->Success(utf8_encode(ss.str()));
    CoTaskMemFree(static_cast<void *>(path));
}

// {
// 'id': xxx
// 'recovered': true // if recovered only for single instance
// }
flutter::EncodableValue MakeOpenResult(int databaseId, bool recovered, bool recoveredInTransaction)
{
    flutter::EncodableMap result;
    result[std::string("id")] = databaseId;
    if (recovered) {
        result[std::string("recovered")] = true;
    }
    if (recoveredInTransaction) {
        result[std::string("recoveredInTransaction")] = flutter::EncodableValue(true);
    }
    return result;
}

} // namespace

template<typename T>
void output_vector(std::ostream &output, const std::vector<T> &vec)
{
    output << "[";
    bool isFirst = true;
    for (auto const &el : vec) {
        if (isFirst) {
            isFirst = false;
        } else {
            output << ", ";
        }
        output << el;
    }
    output << "]";
}

std::ostream &operator<<(std::ostream &output, const flutter::EncodableValue &value)
{
    if (std::holds_alternative<std::monostate>(value)) {
        output << "null";
    } else if (std::holds_alternative<bool>(value)) {
        output << std::boolalpha << std::get<bool>(value);
    } else if (std::holds_alternative<int32_t>(value)) {
        output << std::get<int32_t>(value);
    } else if (std::holds_alternative<int64_t>(value)) {
        output << std::get<int64_t>(value);
    } else if (std::holds_alternative<double>(value)) {
        output << std::get<double>(value);
    } else if (std::holds_alternative<std::string>(value)) {
        output << '"' << std::get<std::string>(value) << '"';
    } else if (std::holds_alternative<std::vector<uint8_t>>(value)) {
        const auto vec = std::get<std::vector<uint8_t>>(value);
        output_vector(output, vec);
    } else if (std::holds_alternative<std::vector<int32_t>>(value)) {
        const auto vec = std::get<std::vector<int32_t>>(value);
        output_vector(output, vec);
    } else if (std::holds_alternative<std::vector<int64_t>>(value)) {
        const auto vec = std::get<std::vector<int64_t>>(value);
        output_vector(output, vec);
    } else if (std::holds_alternative<std::vector<double>>(value)) {
        const auto vec = std::get<std::vector<double>>(value);
        output_vector(output, vec);
    } else if (std::holds_alternative<std::vector<float>>(value)) {
        const auto vec = std::get<std::vector<float>>(value);
        output_vector(output, vec);
    } else if (std::holds_alternative<flutter::EncodableList>(value)) {
        const auto vec = std::get<flutter::EncodableList>(value);
        output_vector(output, vec);
    } else if (std::holds_alternative<flutter::EncodableMap>(value)) {
        const auto map = std::get<flutter::EncodableMap>(value);
        output << "{";
        bool isFirst = true;
        for (auto const &it : map) {
            if (isFirst) {
                isFirst = false;
            } else {
                output << ", ";
            }
            output << it.first << ": " << it.second;
        }
        output << "}";
    } else {
        output << "EncodableValue.index(" << value.index() << ')';
    }
    return output;
}

// static
void SqfliteSqlCipherPlugin::RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar)
{
    {
        auto channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            registrar->messenger(),
            "com.davidmartos96.sqflite_sqlcipher",
            &flutter::StandardMethodCodec::GetInstance());

        auto plugin = std::make_unique<SqfliteSqlCipherPlugin>();

        channel->SetMethodCallHandler(
            [plugin_pointer = plugin.get()](const auto &call, auto result) {
                plugin_pointer->HandleMethodCall(call, std::move(result));
            });

        registrar->AddPlugin(std::move(plugin));
    }
    {
        auto channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            registrar->messenger(),
            "com.tekartik.sqflite",
            &flutter::StandardMethodCodec::GetInstance());

        auto plugin = std::make_unique<SqfliteSqlCipherPlugin>();

        channel->SetMethodCallHandler(
            [plugin_pointer = plugin.get()](const auto &call, auto result) {
                plugin_pointer->HandleMethodCall(call, std::move(result));
            });

        registrar->AddPlugin(std::move(plugin));
    }
}

SqfliteSqlCipherPlugin::SqfliteSqlCipherPlugin()
{
#ifdef DEBUG_LOG_FILE

#endif

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        "c:\\temp\\flutter_sqlcipher_plugin.log", true);

    auto algLgr = std::make_shared<spdlog::logger>("",
                                                   spdlog::sinks_init_list(
                                                       {console_sink, file_sink}));
    spdlog::set_default_logger(algLgr);
    spdlog::flush_every(std::chrono::milliseconds(100));

    spdlog::info("Plugin started");
}

SqfliteSqlCipherPlugin::~SqfliteSqlCipherPlugin()
{
    spdlog::shutdown();
}

void SqfliteSqlCipherPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    // if (method_call.arguments()) {
    //     spdlog::info("Method call {} args {}", method_call.method_name(), *method_call.arguments());
    // } else {
    spdlog::info("Method call {}", method_call.method_name());
    //    }

    if (method_call.method_name().compare("getPlatformVersion") == 0) {
        GetPlatformVersionCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("getDatabasesPath") == 0) {
        HandleGetDatabasesPathCall(method_call, result.get());
    } else if (method_call.method_name().compare("deleteDatabase") == 0) {
        DeleteDatabaseCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("openDatabase") == 0) {
        OpenDatabaseCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("closeDatabase") == 0) {
        CloseDatabaseCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("insert") == 0) {
        InsertCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("execute") == 0) {
        ExecuteCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("query") == 0) {
        QueryCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("update") == 0) {
        UpdateCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("batch") == 0) {
        BatchCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("debug") == 0) {
        DebugCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("debugMode") == 0) {
        DebugModeCall(method_call, std::move(result));
    } else if (method_call.method_name().compare("options") == 0) {
        OptionsCall(method_call, std::move(result));
    } else {
        spdlog::error("Not implemented method called: {}", method_call.method_name());
        result->NotImplemented();
    }
}

void SqfliteSqlCipherPlugin::DeleteDatabaseCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("Executing DeleteDatabaseCall()");
    std::string fileName = GetArgument<std::string>(method_call, "path");
    spdlog::info("DeleteDatabaseCall() fileName: {}", fileName);

    // Handle hot-restart for single instance
    // The dart code is killed but the native code remains
    Database *db = nullptr;
    do {
        {
            const std::lock_guard<std::mutex> lock(m_databaseMapMutex);
            for (auto const &pair : m_databaseMap) {
                if (pair.second->path == fileName) {
                    db = pair.second;
                    break;
                }
            }
        }
        if (db) {
            CloseDatabase(db);
        }
    } while (db);

    std::filesystem::path path(fileName);
    if (std::filesystem::exists(path)) {
        std::error_code error = {};
        if (std::filesystem::remove(path, error)) {
            spdlog::info("file removed: {}", fileName);
        } else {
            spdlog::error("could not remove file: {}", fileName);
            result->Error("delete_failed " + fileName);
            return;
        }
    }
    result->Success();
}

void SqfliteSqlCipherPlugin::OpenDatabaseCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("OpenDatabaseCall()");
    std::string path = GetArgument<std::string>(method_call, "path");
    std::string password = GetOptionalArgument<std::string>(method_call, "password");
    bool readOnly = GetOptionalArgument<bool>(method_call, "readOnly");
    bool singleInstance = GetOptionalArgument<bool>(method_call, "singleInstance");
    bool inMemoryPath = (path == ":memory:");

    spdlog::info("Opening: {}{} {} instance, password: {}",
                 path,
                 (readOnly ? " read-only" : ""),
                 (singleInstance ? "single" : "new"),
                 password);

    // Handle hot-restart for single instance
    // The dart code is killed but the native code remains
    if (singleInstance) {
        const std::lock_guard<std::mutex> lock(m_databaseMapMutex);
        auto iter = m_singleInstanceMap.find(path);
        if (iter != m_singleInstanceMap.end()) {
            Database *database = m_singleInstanceMap[path];
            spdlog::info("Re-opened: {} single instance id: {} in transaction: {}",
                         path,
                         database->id,
                         database->inTransaction);
            result->Success(MakeOpenResult(database->id, true, database->inTransaction));
            return;
        }
    }

    // Make sure the directory exists
    if (!inMemoryPath && !readOnly) {
        auto parentDir = std::filesystem::path(path).parent_path();
        if (!std::filesystem::exists(parentDir)) {
            spdlog::info("Creating parent dir: {}", parentDir.string());
            std::error_code ec;
            bool ok = std::filesystem::create_directories(parentDir, ec);
            if (!ok) {
                spdlog::error("Could not create parent dir: {}", parentDir.string());
            }
        }
    }

    sqlite3 *conn = nullptr;
    int ret = sqlite3_open_v2(path.c_str(),
                              &conn,
                              (readOnly ? SQLITE_OPEN_READONLY
                                        : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE),
                              nullptr);
    if (ret != SQLITE_OK) {
        result->Error(SQLITE_ERROR_CODE, "open_failed " + path);
        return;
    }
    spdlog::info("Opened database at: {}", path);

    if (!password.empty()) {
        ret = sqlite3_key(conn, password.c_str(), static_cast<int>(password.size()));
        if (ret != SQLITE_OK) {
            spdlog::error("Failed to set key, error: {}", ret);
        } else {
            spdlog::info("Set db key");
        }
    }

    // TODO: open db
    static int id = 0;
    auto db = new Database();
    db->db = conn;
    db->id = ++id;
    db->inTransaction = false;
    db->password = password;
    db->path = path;
    db->singleInstance = singleInstance;

    if (!password.empty()) {
        flutter::EncodableMap params;
        params["sql"s] = "PRAGMA cipher_migrate";
        BatchOperation operation(params);
        if (ExecuteOrError(db, &operation)) {
            spdlog::error("Error executing PRAGMA cipher_migrate");
        } else {
            spdlog::info("Executed PRAGMA cipher_migrate");
        }
    }

    {
        const std::lock_guard<std::mutex> lock(m_databaseMapMutex);

        m_databaseMap[db->id] = db;

        if (singleInstance) {
            m_singleInstanceMap[path] = db;
        }

        ++m_databaseOpenCount;
    }

    result->Success(MakeOpenResult(db->id, false, false));
}

void SqfliteSqlCipherPlugin::CloseDatabaseCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("CloseDatabaseCall()");
    Database *db = GetDatabaseOrError(method_call, result.get());
    if (!db) {
        return;
    }

    CloseDatabase(db);
    result->Success();
}

void SqfliteSqlCipherPlugin::CloseDatabase(Database *db)
{
    spdlog::info("Closing database at: {}", db->path);
    const std::lock_guard<std::mutex> lock(m_databaseMapMutex);
    int ret = sqlite3_close_v2(db->db);
    if (ret == SQLITE_OK) {
        spdlog::info("Closed database at: {}", db->path);
    } else {
        spdlog::error("Error closing database at: {}", db->path, sqlite3_errstr(ret));
    }

    m_databaseMap.erase(db->id);
    if (db->singleInstance) {
        m_singleInstanceMap.erase(db->path);
    }

    delete db;

    if (--m_databaseOpenCount == 0) {
        spdlog::info("No more databases open");
    }
}

void SqfliteSqlCipherPlugin::InsertCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("InsertCall()");
    Database *db = GetDatabaseOrError(method_call, result.get());
    if (!db) {
        return;
    }

    MethodCallOperation operation(method_call, std::move(result));
    Insert(db, &operation);
}

bool SqfliteSqlCipherPlugin::Insert(Database *db, DatabaseOperation *operation)
{
    if (!ExecuteOrError(db, operation)) {
        return false;
    }

    if (operation->noResult) {
        spdlog::info("Insert() noResult is set");
        operation->onSuccess({});
        return true;
    }

    // handle ON CONFLICT IGNORE (issue #164) by checking the number of changes before
    int changes = sqlite3_changes(db->db);
    if (changes == 0) {
        spdlog::info("Insert() no db changes");
        operation->onSuccess({});
        return true;
    }

    int64_t rowId = sqlite3_last_insert_rowid(db->db);
    spdlog::info("Insert() last insert rowid: {}", rowId);
    operation->onSuccess(rowId);
    return true;
}

bool SqfliteSqlCipherPlugin::Execute(Database *db, DatabaseOperation *operation)
{
    if (ExecuteOrError(db, operation)) {
        operation->onSuccess({});
        return true;
    } else {
        return false;
    }
}

bool bind(sqlite3_stmt *stmt, const flutter::EncodableList &arguments)
{
    for (int i = 0; i < static_cast<int>(arguments.size()); ++i) {
        const flutter::EncodableValue &arg = arguments[i];
        const int paramIndex = i + 1;

        if (std::holds_alternative<std::monostate>(arg)) {
            sqlite3_bind_null(stmt, paramIndex);
            spdlog::info("Bound NULL as query arg {}", paramIndex);
        } else if (std::holds_alternative<bool>(arg)) {
            sqlite3_bind_int(stmt, paramIndex, std::get<bool>(arg) ? 1 : 0);
            spdlog::info("Bound bool {} as query arg {}", std::get<bool>(arg), paramIndex);
        } else if (std::holds_alternative<int32_t>(arg)) {
            sqlite3_bind_int(stmt, paramIndex, std::get<int32_t>(arg));
            spdlog::info("Bound int {} as query arg {}", std::get<int32_t>(arg), paramIndex);
        } else if (std::holds_alternative<int64_t>(arg)) {
            sqlite3_bind_int64(stmt, paramIndex, std::get<int64_t>(arg));
            spdlog::info("Bound int64 {} as query arg {}", std::get<int64_t>(arg), paramIndex);
        } else if (std::holds_alternative<double>(arg)) {
            sqlite3_bind_double(stmt, paramIndex, std::get<double>(arg));
            spdlog::info("Bound double {} as query arg {}", std::get<double>(arg), paramIndex);
        } else if (std::holds_alternative<std::string>(arg)) {
            const std::string &text = std::get<std::string>(arg);
            sqlite3_bind_text(stmt, paramIndex, text.c_str(), static_cast<int>(text.size()), nullptr);
            spdlog::info("Bound string {} as query arg {}", text, paramIndex);
        } else if (std::holds_alternative<std::vector<uint8_t>>(arg)) {
            const std::vector<uint8_t> &buffer = std::get<std::vector<uint8_t>>(arg);
            spdlog::info("Bound blob [{}] of size {} for {}", buffer, buffer.size(), paramIndex);
            sqlite3_bind_blob(stmt,
                              paramIndex,
                              reinterpret_cast<const void *>(buffer.data()),
                              static_cast<int>(buffer.size()),
                              nullptr);
        } else {
            spdlog::error("Unsupported bind argument type: {} for {}", arg.index(), paramIndex);
            return false;
        }
    }

    return true;
}

bool SqfliteSqlCipherPlugin::ExecuteOrError(Database *db, DatabaseOperation *operation)
{
    const std::string &sql = operation->sql;
    const flutter::EncodableList &arguments = operation->sqlArguments;

    spdlog::info("Execute() sql: {}, arg count: {}", sql, arguments.size());

    // If wanted, we leave the transaction even if it fails
    if (!operation->inTransaction) {
        db->inTransaction = false;
    }

    bool retValue = false;

    sqlite3_stmt *stmt;
    int ret = sqlite3_prepare_v2(db->db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (ret != SQLITE_OK) {
        spdlog::error("Execute() sqlite3_prepare_v2 error: {}", ret);
        HandleError(ret, operation);
    } else {
        bind(stmt, arguments);

        ret = sqlite3_step(stmt);

        if (ret != SQLITE_DONE) {
            spdlog::error("Execute() sqlite3_step error: {}", ret);
            HandleError(ret, operation);
        } else {
            retValue = true;
        }

        ret = sqlite3_finalize(stmt);
        if (ret != SQLITE_OK) {
            spdlog::error("Execute() sqlite3_finalize error: {}", ret);
        }

        // We enter the transaction on success
        if (ret == SQLITE_OK && operation->inTransaction) {
            db->inTransaction = true;
        }
    }

    return retValue;
}

void SqfliteSqlCipherPlugin::ExecuteCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("ExecuteCall()");
    Database *db = GetDatabaseOrError(method_call, result.get());
    if (!db) {
        return;
    }

    MethodCallOperation operation(method_call, std::move(result));
    if (ExecuteOrError(db, &operation)) {
        operation.onSuccess({});
    }
}

void SqfliteSqlCipherPlugin::QueryCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("QueryCall()");

    Database *db = GetDatabaseOrError(method_call, result.get());
    if (!db) {
        return;
    }

    MethodCallOperation operation(method_call, std::move(result));
    if (Query(db, &operation)) {
    }
}

flutter::EncodableValue SqliteColumnToFlutterValue(sqlite3_stmt *stmt, int i)
{
    flutter::EncodableValue value;
    const int type = sqlite3_column_type(stmt, i);
    if (type == SQLITE_NULL) {
    } else if (type == SQLITE_INTEGER) {
        int64_t intValue = sqlite3_column_int64(stmt, i);
        value = intValue;
        spdlog::info("column int64: {}", intValue);
    } else if (type == SQLITE_FLOAT) {
        double doubleValue = sqlite3_column_double(stmt, i);
        value = doubleValue;
        spdlog::info("column double: {}", doubleValue);
    } else if (type == SQLITE_TEXT) {
        std::string stringValue = reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
        value = stringValue;
        spdlog::info("column string: {}", stringValue);
    } else if (type == SQLITE_BLOB) {
        int len = sqlite3_column_bytes(stmt, i);
        std::vector<uint8_t> buffer;
        buffer.resize(len);
        std::memcpy(reinterpret_cast<void *>(buffer.data()), sqlite3_column_blob(stmt, i), len);
        value = buffer;
        spdlog::info("column blob of size {}", buffer.size());
    } else {
        spdlog::error("Unsupported column sql type: {}", type);
    }
    return value;
}

bool SqfliteSqlCipherPlugin::Query(Database *db, DatabaseOperation *operation)
{
    const std::string &sql = operation->sql;
    const flutter::EncodableList &arguments = operation->sqlArguments;

    spdlog::info("Query() sql: {}, args: {}", sql, arguments);

    sqlite3_stmt *stmt;
    int ret = sqlite3_prepare_v2(db->db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (ret != SQLITE_OK) {
        spdlog::error("Query() sqlite3_prepare_v2 error: {}", ret);
        HandleError(ret, operation);
    } else {
        bind(stmt, arguments);

        ret = sqlite3_step(stmt);

        if (m_queryReturnsListOfMaps) {
            flutter::EncodableList results;

            while (ret == SQLITE_ROW) {
                flutter::EncodableMap row;
                int columnCount = sqlite3_column_count(stmt);

                spdlog::info("Query() result column count: {}", columnCount);

                for (int i = 0; i < columnCount; ++i) {
                    const char *rawName = sqlite3_column_origin_name(stmt, i);
                    std::string name = rawName ? rawName : "";

                    flutter::EncodableValue value = SqliteColumnToFlutterValue(stmt, i);
                    spdlog::info("Query() column {} {} flutter type(index) {}",
                                 i,
                                 name,
                                 value.index());
                    row[name] = value;
                }
                results.push_back(row);

                ret = sqlite3_step(stmt);
            }

            if (ret == SQLITE_DONE) {
                spdlog::info("Query() storing result as map list of size: {}: ",
                             results.size(),
                             results);
                operation->onSuccess(results);
            }
        } else {
            flutter::EncodableList columns;
            flutter::EncodableList rows;

            while (ret == SQLITE_ROW) {
                if (columns.empty()) {
                    std::string names;
                    columns.resize(sqlite3_column_count(stmt));
                    for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
                        const char *rawName = sqlite3_column_origin_name(stmt, i);
                        std::string name = rawName ? rawName : "";
                        columns[i] = name;
                        names += name + ", ";
                    }
                    spdlog::info("Query() columns[{}]: {}", columns.size(), names);
                }

                spdlog::info("Query() row {}", rows.size());

                flutter::EncodableList row;
                for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
                    flutter::EncodableValue value = SqliteColumnToFlutterValue(stmt, i);
                    spdlog::info("Query() column of flutter type: {} {}", value.index(), value);
                    row.push_back(value);
                }
                spdlog::info("Query() appended row with {} columns", row.size());
                rows.push_back(row);
                ret = sqlite3_step(stmt);
            }

            if (ret == SQLITE_DONE) {
                flutter::EncodableMap map;
                map["columns"] = columns; //flutter::EncodableList{"user_version"};
                map["rows"] = rows; //flutter::EncodableList{flutter::EncodableList{int32_t{0}}};
                //result->Success(map);
                spdlog::info("Query() storing result. columns: {}, rows: {}",
                             columns.size(),
                             rows.size());
                operation->onSuccess(map);
            }
        }

        if (ret != SQLITE_DONE) {
            spdlog::error("Query() sqlite3_step error: {}", ret);
            HandleError(ret, operation);
        }

        ret = sqlite3_finalize(stmt);
        if (ret != SQLITE_OK) {
            spdlog::error("Query() sqlite3_finalize error: {}", ret);
        }
    }

    return ret == SQLITE_OK;
}

void SqfliteSqlCipherPlugin::UpdateCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("UpdateCall()");
    Database *db = GetDatabaseOrError(method_call, result.get());
    if (!db) {
        return;
    }

    MethodCallOperation operation(method_call, std::move(result));
    Update(db, &operation);
}

bool SqfliteSqlCipherPlugin::Update(Database *db, DatabaseOperation *operation)
{
    if (!ExecuteOrError(db, operation)) {
        return false;
    }

    if (operation->noResult) {
        spdlog::info("Update() noResult is set");
        operation->onSuccess({});
        return true;
    }

    int changes = sqlite3_changes(db->db);
    spdlog::info("Update() changes: {}", changes);

    operation->onSuccess(changes);
    return true;
}

void SqfliteSqlCipherPlugin::BatchCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("BatchCall()");

    Database *db = GetDatabaseOrError(method_call, result.get());
    if (!db) {
        return;
    }

    auto mainOperation = std::make_unique<MethodCallOperation>(method_call, std::move(result));
    flutter::EncodableList operations = GetArgument<flutter::EncodableList>(method_call,
                                                                            "operations");
    flutter::EncodableList operationResults;

    for (const flutter::EncodableValue &operationValue : operations) {
        flutter::EncodableMap operationMap = std::get<flutter::EncodableMap>(operationValue);
        auto operation = new BatchOperation(operationMap);
        operation->noResult = mainOperation->noResult;

        spdlog::info("BatchCall() operation: {}, sql: {}", operation->method, operation->sql);

        bool ok = false;

        if (operation->method == "insert") {
            ok = Insert(db, operation);
        } else if (operation->method == "update") {
            ok = Update(db, operation);
        } else if (operation->method == "execute") {
            ok = Execute(db, operation);
        } else if (operation->method == "query") {
            ok = Query(db, operation);
        } else {
            spdlog::error("BatchCall() unsupported method: {}", operation->method);
            mainOperation->onError("bad_param",
                                   "Batch method '" + operation->method + "' not supported");
            return;
        }

        if (ok) {
            operation->appendResultTo(operationResults);
        } else if (mainOperation->continueOnError) {
            spdlog::error("BatchCall() error, continuing");
            operation->appendErrorTo(operationResults);
        } else {
            spdlog::error("BatchCall() error, exiting");
            operation->copyErrorTo(mainOperation->methodResult.get());
            return;
        }
    }

    if (mainOperation->noResult) {
        mainOperation->onSuccess({});
    } else {
        mainOperation->onSuccess(operationResults);
    }

    //result->Success();
}

void SqfliteSqlCipherPlugin::DebugCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("DebugCall()");
    const std::string cmd = GetArgument<std::string>(method_call, "cmd");
    spdlog::info("DebugCall() cmd: {}", cmd);

    flutter::EncodableMap info;

    if (cmd == "get") {
        const std::lock_guard<std::mutex> lock(m_databaseMapMutex);
        if (!m_databaseMap.empty()) {
            flutter::EncodableMap dbsInfo;

            for (auto iter : m_databaseMap) {
                flutter::EncodableMap dbInfo;
                dbInfo["path"s] = iter.second->path;
                dbInfo["singleInstance"s] = iter.second->singleInstance;
                dbsInfo[std::to_string(iter.first)] = dbInfo;
            }
            info["databases"s] = dbsInfo;
        }
    }
    // TODO: loglevel
    // info["logLevel"s] = ...;

    result->Success(info);
}

void SqfliteSqlCipherPlugin::DebugModeCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("DebugModeCall()");
    bool on = std::get<bool>(*method_call.arguments());
    spdlog::info("DebugModeCall() on: {}", on);
    // TODO: change log level
    result->Success();
}

void SqfliteSqlCipherPlugin::OptionsCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    spdlog::info("OptionsCall()");
    // TODO
    result->Success();
}

void SqfliteSqlCipherPlugin::GetPlatformVersionCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    std::ostringstream version_stream;
    version_stream << "Windows ";
    if (IsWindows10OrGreater()) {
        version_stream << "10+";
    } else if (IsWindows8OrGreater()) {
        version_stream << "8";
    } else if (IsWindows7OrGreater()) {
        version_stream << "7";
    }
    result->Success(flutter::EncodableValue(version_stream.str()));
}

Database *SqfliteSqlCipherPlugin::GetDatabaseOrError(
    const flutter::EncodableValue &arguments, flutter::MethodResult<flutter::EncodableValue> *result)
{
    int id = GetArgument<int>(arguments, "id");
    const std::lock_guard<std::mutex> lock(m_databaseMapMutex);
    auto iter = m_databaseMap.find(id);
    if (iter != m_databaseMap.end()) {
        return iter->second;
    } else {
        spdlog::error("Cannot find database with id: {}", id);
        result->Error(SQLITE_ERROR_CODE, "database_closed");
        return nullptr;
    }
}

Database *SqfliteSqlCipherPlugin::GetDatabaseOrError(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    flutter::MethodResult<flutter::EncodableValue> *result)
{
    return GetDatabaseOrError(*method_call.arguments(), result);
}

bool SqfliteSqlCipherPlugin::HandleError(int returnCode,
                                         flutter::MethodResult<flutter::EncodableValue> *result)
{
    if (returnCode != SQLITE_OK) {
        result->Error(SQLITE_ERROR_CODE, sqlite3_errstr(returnCode));
        return true;
    }
    return false;
}

bool SqfliteSqlCipherPlugin::HandleError(int returnCode, FlutterError *error)
{
    if (returnCode != SQLITE_OK) {
        spdlog::error("SqfliteSqlCipherPlugin::HandleError(FlutterError): {}",
                      sqlite3_errstr(returnCode));
        error->code = SQLITE_ERROR_CODE;
        error->message = sqlite3_errstr(returnCode);
        return true;
    }
    return false;
}

bool SqfliteSqlCipherPlugin::HandleError(int returnCode, DatabaseOperation *operation)
{
    if (returnCode != SQLITE_OK) {
        spdlog::error("SqfliteSqlCipherPlugin::HandleError(FlutterError): {}",
                      sqlite3_errstr(returnCode));
        operation->onError(SQLITE_ERROR_CODE, sqlite3_errstr(returnCode));
        return true;
    } else {
        spdlog::warn("HandleError with returnCode = SQLITE_OK");
    }
    return false;
}

} // namespace sqflite_sqlcipher
