# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/Espressif/frameworks/esp-idf-v5.2.1/components/bootloader/subproject"
  "D:/BaiduNetdiskDownload/ESP32_AI_BOX1/MY_Assistant-main/factory_nvs/build/bootloader"
  "D:/BaiduNetdiskDownload/ESP32_AI_BOX1/MY_Assistant-main/factory_nvs/build/bootloader-prefix"
  "D:/BaiduNetdiskDownload/ESP32_AI_BOX1/MY_Assistant-main/factory_nvs/build/bootloader-prefix/tmp"
  "D:/BaiduNetdiskDownload/ESP32_AI_BOX1/MY_Assistant-main/factory_nvs/build/bootloader-prefix/src/bootloader-stamp"
  "D:/BaiduNetdiskDownload/ESP32_AI_BOX1/MY_Assistant-main/factory_nvs/build/bootloader-prefix/src"
  "D:/BaiduNetdiskDownload/ESP32_AI_BOX1/MY_Assistant-main/factory_nvs/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/BaiduNetdiskDownload/ESP32_AI_BOX1/MY_Assistant-main/factory_nvs/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/BaiduNetdiskDownload/ESP32_AI_BOX1/MY_Assistant-main/factory_nvs/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
