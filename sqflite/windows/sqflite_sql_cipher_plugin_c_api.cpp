#include "include/sqflite_sqlcipher/sqflite_sql_cipher_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "sqflite_sqlcipher_plugin.h"

void SqfliteSqlCipherPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  sqflite_sqlcipher::SqfliteSqlCipherPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
