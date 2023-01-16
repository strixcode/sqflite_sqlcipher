#pragma once
#include <flutter/encodable_value.h>
#include <flutter/method_call.h>
#include <fstream>

//#ifdef DEBUG_LOG_FILE
extern std::ofstream s_logFile;
//#endif

namespace FlutterUtil {

template<typename T>
T GetArgument(const flutter::EncodableValue &arguments, std::string name)
{
    auto map = std::get<flutter::EncodableMap>(arguments);
    if (map.count(name) == 0) {
        s_logFile << "GetArgument() key '" << name << "' not found";
        assert(map.count(name) == 1);
        return {};
    } else {
        auto value = map[name];
        if (!std::holds_alternative<T>(value)) {
            s_logFile << "GetArgument() the value for '" << name
                      << "' is of wrong type, index:" << value.index() << std::endl;
        }
        return std::get<T>(value);
    }
}

template<typename T>
T GetOptionalArgument(const flutter::EncodableValue &arguments,
                      std::string name,
                      const T &defaultValue = {})
{
    auto map = std::get<flutter::EncodableMap>(arguments);
    if (map.count(name) > 0) {
        auto value = map[name];
        if (!std::holds_alternative<T>(value)) {
            s_logFile << "GetArgument() the value is of wrong type, index:" << value.index()
                      << std::endl;
        }
        return std::get<T>(value);
    } else {
        return defaultValue;
    }
}

template<typename T>
T GetArgument(const flutter::MethodCall<flutter::EncodableValue> &methodCall, std::string name)
{
    return GetArgument<T>(*methodCall.arguments(), name);
}

template<typename T>
T GetOptionalArgument(const flutter::MethodCall<flutter::EncodableValue> &methodCall,
                      std::string name,
                      const T &defaultValue = {})
{
    return GetOptionalArgument<T>(*methodCall.arguments(), name, defaultValue);
}

} // namespace FlutterUtil
