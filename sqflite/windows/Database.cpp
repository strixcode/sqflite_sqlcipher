#include "Database.h"
#include "FlutterUtil.h"
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace FlutterUtil;

DatabaseOperation::DatabaseOperation(const flutter::EncodableValue &params)
{
    sql = GetOptionalArgument<std::string>(params, "sql");
    sqlArguments = GetOptionalArgument<flutter::EncodableList>(params, "arguments");
    noResult = GetOptionalArgument<bool>(params, "noResult");
    continueOnError = GetOptionalArgument<bool>(params, "continueOnError");
    inTransaction = GetOptionalArgument<bool>(params, "inTransaction");
}

void DatabaseOperation::onSuccess(const flutter::EncodableValue &value)
{
    // TODO: should this be no-op? in Obj-C it is
    spdlog::warn("DatabaseOperation::storeResult() no-op");
}

void DatabaseOperation::onError(const std::string &errorCode, const std::string &errorMessage)
{
    spdlog::warn("DatabaseOperation::onError() no-op: {} {}", errorCode, errorMessage);
}

BatchOperation::BatchOperation(const flutter::EncodableMap &params)
    : DatabaseOperation(params)
{
    method = GetOptionalArgument<std::string>(params, "method");
}

void BatchOperation::appendResultTo(flutter::EncodableList &out)
{
    if (noResult) {
        return;
    }

    // We wrap the result in 'result' map
    flutter::EncodableMap map;
    map["result"s] = m_result;
    out.push_back(map);
}

void BatchOperation::appendErrorTo(flutter::EncodableList &out)
{
    if (noResult) {
        spdlog::info("Not appending error to result because of noResult flag");
        return;
    }

    // We wrap the error in an 'error' map
    flutter::EncodableMap errorMap;
    errorMap["code"s] = m_error.code;
    if (!m_error.message.empty()) {
        errorMap["message"s] = m_error.message;
    }
    if (m_error.details.index() > 0) {
        errorMap["data"s] = m_error.details;
    }

    spdlog::error("Appending error to results, code: {}, message: {}",
                  m_error.code,
                  m_error.message);

    flutter::EncodableMap map;
    map["error"s] = errorMap;
    out.push_back(map);
}

void BatchOperation::copyResultTo(flutter::MethodResult<flutter::EncodableValue> *resultArg)
{
    resultArg->Success(m_result);
}

void BatchOperation::copyErrorTo(flutter::MethodResult<flutter::EncodableValue> *resultArg)
{
    resultArg->Error(m_error.code, m_error.message, m_error.details);
}

void BatchOperation::onSuccess(const flutter::EncodableValue &value)
{
    spdlog::info("BatchOperation::onSuccess()");
    m_result = value;
}

void BatchOperation::onError(const std::string &errorCode, const std::string &errorMessage)
{
    spdlog::info("BatchOperation::onError()");
    m_error.code = errorCode;
    m_error.message = errorMessage;
}

MethodCallOperation::MethodCallOperation(
    const flutter::MethodCall<flutter::EncodableValue> &call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> &&result)
    : DatabaseOperation(*call.arguments())
    , methodResult(std::move(result))
{}

void MethodCallOperation::onSuccess(const flutter::EncodableValue &value)
{
    methodResult->Success(value);
}

void MethodCallOperation::onError(const std::string &errorCode, const std::string &errorMessage)
{
    spdlog::warn("MethodCallOperation::onError(): {} {}", errorCode, errorMessage);
    methodResult->Error(errorCode, errorMessage);
}