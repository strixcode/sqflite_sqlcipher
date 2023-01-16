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

#include "lib/sqlcipher/include/sqlite3.h"
#include <codecvt>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>

#define DEBUG_LOG_FILE 1
#ifdef DEBUG_LOG_FILE
#include <fstream>
#endif

#include "Database.h"
#include "FlutterUtil.h"

using namespace std::string_literals;
using namespace FlutterUtil;

#ifdef DEBUG_LOG_FILE
std::ofstream s_logFile;
#endif

namespace sqflite_sqlcipher {

namespace {

const char *PARAM_PATH = "path";

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
    auto channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
        registrar->messenger(),
        "com.davidmartos96.sqflite_sqlcipher",
        &flutter::StandardMethodCodec::GetInstance());

    auto plugin = std::make_unique<SqfliteSqlCipherPlugin>();

    channel->SetMethodCallHandler([plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
    });

    registrar->AddPlugin(std::move(plugin));
}

SqfliteSqlCipherPlugin::SqfliteSqlCipherPlugin()
{
#ifdef DEBUG_LOG_FILE
    s_logFile.open("c:\\temp\\flutter_sqlcipher_plugin_log.txt", std::ios_base::app);
#endif
}

SqfliteSqlCipherPlugin::~SqfliteSqlCipherPlugin()
{
#ifdef DEBUG_LOG_FILE
    s_logFile.close();
#endif
}

void SqfliteSqlCipherPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
#ifdef DEBUG_LOG_FILE
    s_logFile << "Method call " << method_call.method_name();
    if (method_call.arguments()) {
        s_logFile << " arg " << *method_call.arguments();
    }
    s_logFile << std::endl;
#endif

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
    } else if (method_call.method_name().compare("options") == 0) {
        OptionsCall(method_call, std::move(result));
    } else {
        result->NotImplemented();
    }
}

void SqfliteSqlCipherPlugin::DeleteDatabaseCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    s_logFile << "Executing DeleteDatabaseCall()" << std::endl;
    std::string fileName = GetArgument<std::string>(method_call, "path");
    // TODO: handle open database
    s_logFile << "fileName: " << fileName << std::endl;

    if (std::filesystem::exists(fileName)) {
        s_logFile << "file exists" << std::endl;
        try {
            std::filesystem::remove(fileName);
            s_logFile << "file removed" << std::endl;
            result->Success();
        } catch (const std::filesystem::filesystem_error &e) {
            s_logFile << "could not remove file, error: " << e.code().value() << ' ' << e.what()
                      << std::endl;
            result->Error(std::to_string(e.code().value()), e.what());
        } catch (const std::exception &e) {
            s_logFile << "could not remove file, exception: " << e.what() << std::endl;
            result->Error("-1", e.what());
        } catch (...) {
            s_logFile << "could not remove file, unexpected exception: " << std::endl;
            result->Error("-1");
        }
    } else {
        s_logFile << "file does not exists" << std::endl;
        result->Success();
    }
}

void SqfliteSqlCipherPlugin::OpenDatabaseCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    s_logFile << "Executing OpenDatabaseCall()" << std::endl;
    std::string path = GetArgument<std::string>(method_call, "path");
    std::string password = GetOptionalArgument<std::string>(method_call, "password");
    bool readOnly = GetOptionalArgument<bool>(method_call, "readOnly");
    bool singleInstance = GetOptionalArgument<bool>(method_call, "singleInstance");
    bool inMemoryPath = (path == ":memory:");

    s_logFile << "Opening: " << path << (readOnly ? " read-only" : "") << " "
              << (singleInstance ? "single" : "new") << " instance" << std::endl;

    // Handle hot-restart for single instance
    // The dart code is killed but the native code remains
    if (singleInstance) {
        const std::lock_guard<std::mutex> lock(m_databaseMapMutex);
        auto iter = m_singleInstanceMap.find(path);
        if (iter != m_singleInstanceMap.end()) {
            Database *database = m_singleInstanceMap[path];
            s_logFile << "Re-opened: " << path << " single instance id " << database->id
                      << "in transaction: " << database->inTransaction << std::endl;
            result->Success(MakeOpenResult(database->id, true, database->inTransaction));
            return;
        }
    }

    // Make sure the directory exists
    if (!inMemoryPath && !readOnly) {
        auto parentDir = std::filesystem::path(path).parent_path();
        if (!std::filesystem::exists(parentDir)) {
            s_logFile << "Creating parent dir: " << parentDir << std::endl;
            bool ok = std::filesystem::create_directory(parentDir);
            if (!ok) {
                s_logFile << "Could not create parent dir: " << parentDir << std::endl;
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
        result->Error(std::to_string(ret), sqlite3_errstr(ret));
        return;
    }
    s_logFile << "Opened database at: " << path << std::endl;

    // TODO: open db
    static int id = 0;
    auto db = new Database();
    db->db = conn;
    db->id = ++id;
    db->inTransaction = false;
    db->password = password;
    db->path = path;
    db->singleInstance = singleInstance;

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
    Database *db = GetDatabaseOrError(method_call, result.get());
    if (!db) {
        return;
    }

    CloseDatabase(db);
    result->Success();
}

void SqfliteSqlCipherPlugin::CloseDatabase(Database *db)
{
    s_logFile << "Closing database at: " << db->path << std::endl;
    const std::lock_guard<std::mutex> lock(m_databaseMapMutex);
    int ret = sqlite3_close_v2(db->db);
    if (ret == SQLITE_OK) {
        s_logFile << "Closed database at: " << db->path << std::endl;
    } else {
        s_logFile << "Error closing database at: " << db->path << sqlite3_errstr(ret) << std::endl;
    }

    m_databaseMap.erase(db->id);
    if (db->singleInstance) {
        m_singleInstanceMap.erase(db->path);
    }

    delete db;

    if (--m_databaseOpenCount == 0) {
        s_logFile << "No more databases open" << std::endl;
    }
}

void SqfliteSqlCipherPlugin::InsertCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    if (Execute(*method_call.arguments(), result.get())) {
        bool noResult = GetOptionalArgument<bool>(method_call, "noResult");
        if (noResult) {
            result->Success();
        } else {
            Database *db = GetDatabaseOrError(method_call, result.get());
            // TODO: handle ON CONFLICT IGNORE (issue #164) by checking the number of changes
            // before
            int64_t rowId = sqlite3_last_insert_rowid(db->db);
            result->Success(rowId);
        }
    }
}

bool SqfliteSqlCipherPlugin::Execute(Database *db,
                                     const flutter::EncodableValue &params,
                                     flutter::MethodResult<flutter::EncodableValue> *result)
{
    s_logFile << "Execute()" << std::endl;
    std::string sql = GetArgument<std::string>(params, "sql");
    flutter::EncodableList arguments = GetOptionalArgument<flutter::EncodableList>(params,
                                                                                   "arguments");

    s_logFile << "Execute() sql: " << sql << ", arg count: " << arguments.size() << std::endl;

    sqlite3_stmt *stmt;
    int ret = sqlite3_prepare_v2(db->db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (ret != SQLITE_OK) {
        s_logFile << "Execute() sqlite3_prepare_v2 error: " << ret << std::endl;
        return false;
    }

    for (int i = 0; i < static_cast<int>(arguments.size()); ++i) {
        const flutter::EncodableValue &arg = arguments[i];

        if (!std::holds_alternative<std::monostate>(arg)) {
            sqlite3_bind_null(stmt, i);
        } else if (!std::holds_alternative<bool>(arg)) {
            sqlite3_bind_int(stmt, i, std::get<bool>(arg) ? 1 : 0);
        } else if (!std::holds_alternative<int32_t>(arg)) {
            sqlite3_bind_int(stmt, i, std::get<int32_t>(arg));
        } else if (!std::holds_alternative<int64_t>(arg)) {
            sqlite3_bind_int64(stmt, i, std::get<int64_t>(arg));
        } else if (!std::holds_alternative<double>(arg)) {
            sqlite3_bind_double(stmt, i, std::get<double>(arg));
        } else if (!std::holds_alternative<std::string>(arg)) {
            const std::string &text = std::get<std::string>(arg);
            sqlite3_bind_text(stmt, i, text.c_str(), static_cast<int>(text.size()), nullptr);
        } else {
            s_logFile << "Execute() Unsupported execute argument type: " << arg.index()
                      << std::endl;
        }
    }

    bool retValue = false;
    ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE) {
        s_logFile << "Execute() sqlite3_step error: " << ret << std::endl;
        HandleError(ret, result);
    } else {
        retValue = true;
    }

    ret = sqlite3_finalize(stmt);
    if (ret != SQLITE_OK) {
        s_logFile << "Execute() sqlite3_finalize error: " << ret << std::endl;
    }

    return retValue;
}

bool SqfliteSqlCipherPlugin::Execute(const flutter::EncodableValue &params,
                                     flutter::MethodResult<flutter::EncodableValue> *result)
{
    Database *db = GetDatabaseOrError(params, result);
    if (!db) {
        return false;
    }

    return Execute(db, params, result);
}

void SqfliteSqlCipherPlugin::ExecuteCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    if (Execute(*method_call.arguments(), result.get())) {
        result->Success();
    }
    /*  
    Database *db = GetDatabaseOrError(method_call, result.get());
    if (!db) {
        return;
    }

    s_logFile << "Executing ExecuteCall()" << std::endl;
    std::string sql = GetArgument<std::string>(method_call, "sql");
    flutter::EncodableList arguments = GetOptionalArgument<flutter::EncodableList>(method_call,
                                                                                   "arguments");

    s_logFile << "ExecuteCall() sql: " << sql << ", arg count: " << arguments.size() << std::endl;

    sqlite3_stmt *stmt;
    int ret = sqlite3_prepare_v2(db->db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (ret != SQLITE_OK) {
        s_logFile << "ExecuteCall() sqlite3_prepare_v2 error: " << ret << std::endl;
    }

    for (int i = 0; i < static_cast<int>(arguments.size()); ++i) {
        const flutter::EncodableValue &arg = arguments[i];

        if (!std::holds_alternative<std::monostate>(arg)) {
            sqlite3_bind_null(stmt, i);
        } else if (!std::holds_alternative<bool>(arg)) {
            sqlite3_bind_int(stmt, i, std::get<bool>(arg) ? 1 : 0);
        } else if (!std::holds_alternative<int32_t>(arg)) {
            sqlite3_bind_int(stmt, i, std::get<int32_t>(arg));
        } else if (!std::holds_alternative<int64_t>(arg)) {
            sqlite3_bind_int64(stmt, i, std::get<int64_t>(arg));
        } else if (!std::holds_alternative<double>(arg)) {
            sqlite3_bind_double(stmt, i, std::get<double>(arg));
        } else if (!std::holds_alternative<std::string>(arg)) {
            const std::string &text = std::get<std::string>(arg);
            sqlite3_bind_text(stmt, i, text.c_str(), static_cast<int>(text.size()), nullptr);
        } else {
            s_logFile << "ExecuteCall() Unsupported execute argument type: " << arg.index()
                      << std::endl;
        }
    }

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE) {
        s_logFile << "ExecuteCall() sqlite3_step error: " << ret << std::endl;
        HandleError(ret, result.get());
    } else {
        result->Success();
    }

    ret = sqlite3_finalize(stmt);
    if (ret != SQLITE_OK) {
        s_logFile << "ExecuteCall() sqlite3_finalize error: " << ret << std::endl;
    }
*/
}

void SqfliteSqlCipherPlugin::QueryCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    s_logFile << "Executing QueryCall()" << std::endl;

    Database *db = GetDatabaseOrError(method_call, result.get());
    if (!db) {
        return;
    }

    std::string sql = GetArgument<std::string>(method_call, "sql");
    flutter::EncodableList arguments = GetOptionalArgument<flutter::EncodableList>(method_call,
                                                                                   "arguments");

    s_logFile << "QueryCall() sql: " << sql << ", arg count: " << arguments.size() << std::endl;

    sqlite3_stmt *stmt;
    int ret = sqlite3_prepare_v2(db->db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (ret != SQLITE_OK) {
        s_logFile << "QueryCall() sqlite3_prepare_v2 error: " << ret << std::endl;
    }

    for (int i = 0; i < static_cast<int>(arguments.size()); ++i) {
        const flutter::EncodableValue &arg = arguments[i];

        if (!std::holds_alternative<std::monostate>(arg)) {
            sqlite3_bind_null(stmt, i);
        } else if (!std::holds_alternative<bool>(arg)) {
            sqlite3_bind_int(stmt, i, std::get<bool>(arg) ? 1 : 0);
        } else if (!std::holds_alternative<int32_t>(arg)) {
            sqlite3_bind_int(stmt, i, std::get<int32_t>(arg));
        } else if (!std::holds_alternative<int64_t>(arg)) {
            sqlite3_bind_int64(stmt, i, std::get<int64_t>(arg));
        } else if (!std::holds_alternative<double>(arg)) {
            sqlite3_bind_double(stmt, i, std::get<double>(arg));
        } else if (!std::holds_alternative<std::string>(arg)) {
            const std::string &text = std::get<std::string>(arg);
            sqlite3_bind_text(stmt, i, text.c_str(), static_cast<int>(text.size()), nullptr);
        } else {
            s_logFile << "QueryCall() Unsupported execute argument type: " << arg.index()
                      << std::endl;
        }
    }

    // TODO: _queryAsMapList

    flutter::EncodableList columns;
    flutter::EncodableList rows;

    ret = sqlite3_step(stmt);
    while (ret == SQLITE_ROW) {
        if (columns.empty()) {
            s_logFile << "QueryCall() columns (" << sqlite3_column_count(stmt) << ") :";
            columns.resize(sqlite3_column_count(stmt));
            for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
                const char *rawName = sqlite3_column_origin_name(stmt, i);
                std::string name = rawName ? rawName : "";
                columns[i] = name;
                s_logFile << name << ", ";
            }
            s_logFile << std::endl;
        }

        flutter::EncodableList row;
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            s_logFile << "QueryCall() row " << i << std::endl;
            flutter::EncodableValue value;
            const int type = sqlite3_column_type(stmt, i);
            if (type == SQLITE_NULL) {
                s_logFile << "NULL, " << std::endl;
                //value = std::monostate;
            } else if (type == SQLITE_INTEGER) {
                int64_t intValue = sqlite3_column_int64(stmt, i);
                s_logFile << intValue << ", " << std::endl;
                value = intValue;
            } else if (type == SQLITE_FLOAT) {
                double doubleValue = sqlite3_column_double(stmt, i);
                s_logFile << doubleValue << ", " << std::endl;
                value = doubleValue;
            } else if (type == SQLITE_TEXT) {
                std::string stringValue = reinterpret_cast<const char *>(
                    sqlite3_column_text(stmt, i));
                s_logFile << "'" << stringValue << "'" << std::endl;
                value = stringValue;
            } else if (type == SQLITE_TEXT) {
                //std::string stringValue = sqlite3_column_text(stmt, i);
                s_logFile << "<blob unsupported yet>" << std::endl;
                //value = stringValue;
            }
            s_logFile << std::endl;
            row.push_back(value);
        }
        rows.push_back(row);
        ret = sqlite3_step(stmt);
    }

    if (ret == SQLITE_DONE) {
        flutter::EncodableMap map;
        map["columns"] = columns; //flutter::EncodableList{"user_version"};
        map["rows"] = rows;       //flutter::EncodableList{flutter::EncodableList{int32_t{0}}};
        result->Success(map);
    } else {
        s_logFile << "QueryCall() sqlite3_set error: " << ret << std::endl;
        HandleError(ret, result.get());
    }

    ret = sqlite3_finalize(stmt);
    if (ret != SQLITE_OK) {
        s_logFile << "QueryCall() sqlite3_finalize error: " << ret << std::endl;
    }
}

void SqfliteSqlCipherPlugin::UpdateCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    // TODO
    result->Success();
}

void SqfliteSqlCipherPlugin::BatchCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    s_logFile << "Executing BatchCall()" << std::endl;

    Database *db = GetDatabaseOrError(method_call, result.get());
    if (!db) {
        return;
    }

    //bool noResult = GetOptionalArgument<bool>(method_call, "noResult");
    //bool continueOnError = GetOptionalArgument<bool>(method_call, "continueOnError");
    flutter::EncodableList operations = GetArgument<flutter::EncodableList>(method_call,
                                                                            "operations");
    s_logFile << "BatchCall() got operations argument" << std::endl;
    flutter::EncodableList operationResults;

    for (const flutter::EncodableValue &operationValue : operations) {
        s_logFile << "BatchCall() processing next operations item" << std::endl;
        flutter::EncodableMap operationMap = std::get<flutter::EncodableMap>(operationValue);
        s_logFile << "BatchCall() converted operation item to map with item count:"
                  << operationMap.size() << std::endl;
        std::string method = std::get<std::string>(operationMap["method"s]);
        std::string sql = std::get<std::string>(operationMap["sql"s]);

        s_logFile << "BatchCall() operation: " << method << ", sql: " << sql << std::endl;

        if (method == "execute") {
            Execute(db, operationMap, result.get());
        } else {
            s_logFile << "BatchCall() unsupported method: " << method << std::endl;
        }
    }

    //result->Success();
}

void SqfliteSqlCipherPlugin::DebugCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
    // TODO
    result->Success();
}

void SqfliteSqlCipherPlugin::OptionsCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
{
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
        s_logFile << "Cannot find database with id: " << id << std::endl;
        result->Error("sqlite_error", "database_closed");
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
        result->Error("sqlite_error", sqlite3_errstr(returnCode));
        return true;
    }
    return false;
}

} // namespace sqflite_sqlcipher
