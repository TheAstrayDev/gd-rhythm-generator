#pragma once

// Step-by-step trace written straight to a file (flushed every line), so that if the game
// crashes we can see the last step that ran. Geode's own crash handler is disabled when it
// runs on a newer GD version than it targets, so this is the only reliable breadcrumb.

#include <chrono>
#include <cstdio>
#include <string>

inline void rgTrace(std::string const& step) {
    static FILE* f = std::fopen("geode/logs/rhythm-generator-trace.txt", "w");
    if (!f) return;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    std::fprintf(f, "%lld %s\n", (long long)ms, step.c_str());
    std::fflush(f);
}
