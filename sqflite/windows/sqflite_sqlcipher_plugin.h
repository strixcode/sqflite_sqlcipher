#ifndef FLUTTER_PLUGIN_SQFLITESQLCIPHER_PLUGIN_H_
#define FLUTTER_PLUGIN_SQFLITESQLCIPHER_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>

#include <map>
#include <memory>
#include <mutex>
#include <thread>

class Database;
class DatabaseOperation;
class FlutterError;

namespace sqflite_sqlcipher {

class SqfliteSqlCipherPlugin : public flutter::Plugin
{
public:
    static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

    SqfliteSqlCipherPlugin();

    virtual ~SqfliteSqlCipherPlugin();

    // Disallow copy and assign.
    SqfliteSqlCipherPlugin(const SqfliteSqlCipherPlugin &) = delete;
    SqfliteSqlCipherPlugin &operator=(const SqfliteSqlCipherPlugin &) = delete;

private:
    // Called when a method is called on this plugin's channel from Dart.
    void HandleMethodCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                          std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void GetPlatformVersionCall(
        const flutter::MethodCall<flutter::EncodableValue> &method_call,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void OpenDatabaseCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                          std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void CloseDatabaseCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                           std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void DeleteDatabaseCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void InsertCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void ExecuteCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                     std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void QueryCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                   std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void UpdateCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void BatchCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                   std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void DebugCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                   std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void DebugModeCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                   std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void OptionsCall(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                     std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void CloseDatabase(Database *db);

    bool Execute(Database *db, DatabaseOperation *operation);

    bool ExecuteOrError(Database *db, DatabaseOperation *operation);

    bool Insert(Database *db, DatabaseOperation *operation);

    bool Update(Database *db, DatabaseOperation *operation);

    bool Query(Database *db, DatabaseOperation *operation);

    // bool Execute(Database *db,
    //              const flutter::EncodableValue &arguments,
    //              flutter::MethodResult<flutter::EncodableValue> *result);

    // bool Execute(const flutter::EncodableValue &arguments,
    //              flutter::MethodResult<flutter::EncodableValue> *result);

    Database *GetDatabaseOrError(const flutter::EncodableValue &arguments,
                                 flutter::MethodResult<flutter::EncodableValue> *result);

    Database *GetDatabaseOrError(const flutter::MethodCall<flutter::EncodableValue> &method_call,
                                 flutter::MethodResult<flutter::EncodableValue> *result);

    bool HandleError(int returnCode, flutter::MethodResult<flutter::EncodableValue> *result);

    bool HandleError(int returnCode, FlutterError *error);

    bool HandleError(int returnCode, DatabaseOperation *operation);

    int m_databaseOpenCount = 0;
    std::thread *m_handlerThread = nullptr;
    std::map<int, Database *> m_databaseMap;
    std::map<std::string, Database *> m_singleInstanceMap;
    std::mutex m_databaseMapMutex;
    int m_databaseId = 0;
    bool m_queryReturnsListOfMaps = true;
};

} // namespace sqflite_sqlcipher

#endif // FLUTTER_PLUGIN_SQFLITESQLCIPHER_PLUGIN_H_
