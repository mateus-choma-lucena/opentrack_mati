#pragma once
#include "freetrackclient/fttypes.h"
#include <windows.h>
#include <string>
// Test-only forced include: compile the production writer unchanged, redirect its
// named memory/mutex to this process, and never call registry/game initialization.
inline const char* enhanced_test_ft_heap() { static const std::string name="OpenTrackEnhancedPacketTest_"+std::to_string(GetCurrentProcessId()); return name.c_str(); }
inline const char* enhanced_test_ft_mutex() { static const std::string name="OpenTrackEnhancedPacketMutex_"+std::to_string(GetCurrentProcessId()); return name.c_str(); }
#undef FREETRACK_HEAP
#undef FREETRACK_MUTEX
#define FREETRACK_HEAP enhanced_test_ft_heap()
#define FREETRACK_MUTEX enhanced_test_ft_mutex()
