cmake_minimum_required(VERSION 3.25)

add_library(ABI_Audio_Player STATIC
  ABI_Audio_Player.cpp
)

target_compile_features(ABI_Audio_Player PUBLIC cxx_std_17)
target_compile_definitions(ABI_Audio_Player PRIVATE _WIN32_WINNT=0x0602)  # Windows 8+ required by XAudio2
target_include_directories(ABI_Audio_Player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Add PhysFS via FetchContent
include(FetchContent)

# If you have ABI_PhysFS_Loader cloned locally, set "" to it's location to avoid fetching it from GitHub.
set(ABI_PhysFS_Loader_LOCAL_PATH "C:/Programming/GitHub/ABI_PhysFS_Loader" CACHE PATH "CACHE_ABI_PhysFS_Loader" FORCE)

if(ABI_PhysFS_Loader_LOCAL_PATH)
  FetchContent_Declare(
    ABI_PhysFS_Loader
    SOURCE_DIR ${ABI_PhysFS_Loader_LOCAL_PATH}
  )
else()
  FetchContent_Declare(
    ABI_PhysFS_Loader
    GIT_REPOSITORY https://github.com/ActingBadly/ABI_PhysFS_Loader.git
    GIT_TAG        main
  )
endif()
FetchContent_MakeAvailable(ABI_PhysFS_Loader)

target_link_libraries(ABI_Audio_Player ABI_PhysFS_Loader)