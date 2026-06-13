#pragma once

#include <xaudio2.h>
#include <windows.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>

void ABI_Audio_Init();
void ABI_Audio_Play(const char* filename, bool interruptable);
void ABI_Audio_Stop_Interruptable();
void ABI_Audio_Stop_Not_Interruptable();
void ABI_Audio_Stop_All();
void ABI_Audio_Set_Volume(float volume);