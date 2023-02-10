# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "sqflite_sqlcipher")
project(${PROJECT_NAME} LANGUAGES CXX)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "sqflite_sqlcipher_plugin")

# Any new source files that you add to the plugin should be added here.
list(APPEND PLUGIN_SOURCES
  "sqflite_sqlcipher_plugin.cpp"
  "sqflite_sqlcipher_plugin.h"
  "Database.h"
  "Database.cpp"
  "FlutterUtil.h"
  "FlutterUtil.cpp"
)

# Define the plugin library target. Its name must not be changed (see comment
# on PLUGIN_NAME above).
add_library(${PLUGIN_NAME} SHARED
  "include/sqflite_sqlcipher/sqflite_sql_cipher_plugin_c_api.h"
  "sqflite_sql_cipher_plugin_c_api.cpp"
  ${PLUGIN_SOURCES}
)

# Apply a standard set of build settings that are configured in the
# application-level CMakeLists.txt. This can be removed for plugins that want
# full control over build settings.
apply_standard_settings(${PLUGIN_NAME})

# Symbols are hidden by default to reduce the chance of accidental conflicts
# between plugins. This should not be removed; any symbols that should be
# exported should be explicitly exported with the FLUTTER_PLUGIN_EXPORT macro.
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# Source include directories and library dependencies. Add any plugin-specific
# dependencies here.
target_include_directories(${PLUGIN_NAME} INTERFACE
"${CMAKE_CURRENT_SOURCE_DIR}/include"
) 

  target_include_directories(${PLUGIN_NAME} SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/sqlcipher/include"
  )  


  target_link_libraries(${PLUGIN_NAME} PRIVATE
    flutter
    flutter_wrapper_plugin
    C:/Projects/QliqSOFT/flutter/sqflite_sqlcipher/sqflite/windows/lib/sqlcipher/lib/sqlcipher.lib
  )

  link_directories("${CMAKE_CURRENT_SOURCE_DIR}/lib/sqlcipher/lib")

# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or libraries created by an
# external build triggered from this build file.
set(sqflite_sqlcipher_bundled_libraries
  ""
  PARENT_SCOPE
)

add_compile_definitions(DEBUG_LOG_FILE=1)