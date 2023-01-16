#pragma once
#include <flutter/method_call.h>
#include <flutter/method_result.h>
#include <string>

struct sqlite3;

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
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result;
    std::string method;
    std::string sql;
    bool inTransaction = false;
    bool noResult = false;
    bool continueOnError = false;

    void handleError();
    void handleSuccess();

    DatabaseOperation(const flutter::MethodCall<flutter::EncodableValue> &call,
                      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> &&result);
};