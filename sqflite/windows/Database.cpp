#include "Database.h"
#include "FlutterUtil.h"

using namespace std::string_literals;
using namespace FlutterUtil;

DatabaseOperation::DatabaseOperation(
    const flutter::MethodCall<flutter::EncodableValue> &call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> &&result)
    : result(std::move(result))
{
    auto params = *call.arguments();
    sql = GetOptionalArgument<std::string>(params, "sql");
}