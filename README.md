# Audio_Player_Windows

Play audio files using XAudio2 with .zip support using PhysFS via ABI_PhysFS_Loader.

Currently in a very early state and should be consider BETA.

# Usage

```cpp
#include <Audio_Player_Windows/VLC_Video_Player.h>

ABI_PhysFS_Load_Package("Zips/Audio.zip","/Data");   // (.zip file location), (Package mount point)

ABI_Audio_Init();                           // Initate ABI_Audio

ABI_Audio_Set_Volume(float volume);         // Set Volume Level
ABI_Audio_Play(Data/Audio.wav, true);       // (File to Play), (Can be interupted by another playback(bool))
ABI_Audio_Stop_Interruptable();             // Stop All Interruptable
ABI_Audio_Stop_Not_Interruptable();         // Stop All Not Interruptable
ABI_Audio_Stop_All();                       // Stop All
```

# Build instructions
## AUTOMATED INSTALL (CMAKE)

```cmake
include(FetchContent)

# If you have `ABI_Audio_Player` cloned locally, set `""` to its location to avoid fetching it from GitHub.
set(ABI_Audio_Player_LOCAL_PATH "" CACHE PATH "CACHE_ABI_Audio_Player" FORCE)

if(ABI_Audio_Player_LOCAL_PATH)
  FetchContent_Declare(
    ABI_Audio_Player
    SOURCE_DIR ${ABI_Audio_Player_LOCAL_PATH}
  )
else()
  FetchContent_Declare(
    ABI_Audio_Player
    GIT_REPOSITORY https://github.com/ActingBadly/ABI_Audio_Player.git
    GIT_TAG        main
  )
endif()
FetchContent_MakeAvailable(ABI_Audio_Player)

target_include_directories(ABI_Audio_Player PUBLIC ${ABI_Audio_Player_SOURCE_DIR})

target_link_libraries(APP_NAME PRIVATE
    ABI_Audio_Player
)
```
