cmake_minimum_required(VERSION 3.20)

project(discord_drover_cpp LANGUAGES CXX)

set(MINHOOK_ROOT "C:/Libraries/MinHook" CACHE PATH "Path to a MinHook installation with include/ and lib/")
set(MINHOOK_LIB "${MINHOOK_ROOT}/lib/libMinHook-x64-v100-md.lib" CACHE FILEPATH "Path to MinHook x64 /MD release library")

add_library(version SHARED
  src/discord_folders.cpp
  src/dllmain.cpp
  src/options.cpp
  src/socket_manager.cpp
  src/version_exports.cpp
  version.def
)

target_compile_features(version PRIVATE cxx_std_20)
target_compile_definitions(version PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WINSOCK_DEPRECATED_NO_WARNINGS)
target_include_directories(version PRIVATE src "${MINHOOK_ROOT}/include")
target_link_libraries(version PRIVATE ws2_32 psapi "${MINHOOK_LIB}")

if(MSVC)
  target_compile_options(version PRIVATE /W4 /permissive-)
endif()

add_executable(drover-installer
  src/console_installer.cpp
  src/discord_folders.cpp
)

set_target_properties(drover-installer PROPERTIES OUTPUT_NAME drover)
target_compile_features(drover-installer PRIVATE cxx_std_20)
target_compile_definitions(drover-installer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(drover-installer PRIVATE src)
target_link_libraries(drover-installer PRIVATE advapi32)

if(MSVC)
  target_compile_options(drover-installer PRIVATE /W4 /permissive-)
endif()

add_custom_command(TARGET drover-installer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/../drover.ini"
          "$<TARGET_FILE_DIR:drover-installer>/drover.ini"
  VERBATIM
)
