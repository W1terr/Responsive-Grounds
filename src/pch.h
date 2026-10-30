#pragma once

// windows.h ends up in here through the libraries; keep its macros from breaking std::min/max and RE:: names
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logs = SKSE::log;
using namespace std::literals;

#undef GetObject
#undef PlaySound
