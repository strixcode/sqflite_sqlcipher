#ifndef DATABASE_H
#define DATABASE_H
#include <flutter/encodable_value.h>
#include <flutter/method_call.h>
#include <flutter/method_result.h>
#include <string>

struct sqlite3;

class FlutterError
{
public:
    std::string code;
    std::string message;
    flutter::EncodableValue details;

    bool IsEmpty() const { return code.empty(); }

    operator bool() const { return !IsEmpty(); }
};

class Database
{
public:
    bool singleInstance = false;
    std::string path;
    std::string password;
    int id = 0;
    int logLevel = 0;
    sqlite3 *db = nullptr;
    bool inTransaction = false;
    bool inUse = false; // TODO, modify holding m_databaseMapMutex
};

class DatabaseOperation
{
public:
    std::string sql;
    flutter::EncodableList sqlArguments;
    bool noResult = false;
    bool continueOnError = false;
    bool inTransaction = false;

    virtual void onError(const std::string &errorCode, const std::string &errorMessage = "");
    virtual void onSuccess(const flutter::EncodableValue &value);

    DatabaseOperation(const flutter::EncodableValue &params);
};

class BatchOperation : public DatabaseOperation
{
public:
    std::string method;

    BatchOperation(const flutter::EncodableMap &params);

    void onError(const std::string &errorCode, const std::string &errorMessage = "") override;
    void onSuccess(const flutter::EncodableValue &value) override;

    void appendErrorTo(flutter::EncodableList &out);
    void appendResultTo(flutter::EncodableList &out);
    void copyResultTo(flutter::MethodResult<flutter::EncodableValue> *result);
    void copyErrorTo(flutter::MethodResult<flutter::EncodableValue> *result);

protected:
    flutter::EncodableValue m_result;

private:
    FlutterError m_error;
};

class MethodCallOperation : public DatabaseOperation
{
public:
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> methodResult;

    MethodCallOperation(const flutter::MethodCall<flutter::EncodableValue> &call,
                        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> &&result);

    void onError(const std::string &errorCode, const std::string &errorMessage = "") override;
    void onSuccess(const flutter::EncodableValue &value) override;
};

#endif // DATABASE_H