#pragma once

#include <cstdlib>
#include <cstring>

#include "barkeep.h"

namespace skwr {

struct ProgressOutputMode {
    bool no_tty;
    barkeep::Duration interval;
    barkeep::ProgressBarStyle style;
};

inline ProgressOutputMode GetProgressOutputMode() {
    static const ProgressOutputMode mode = [] {
        const char* env = std::getenv("SKEWER_NO_TTY");
        const bool no_tty = env != nullptr && env[0] != '\0' && std::strcmp(env, "0") != 0;
        return ProgressOutputMode{
            .no_tty = no_tty,
            .interval = no_tty ? barkeep::Duration{15.0} : barkeep::Duration{0.5},
            .style = no_tty ? barkeep::ProgressBarStyle::Blocks : barkeep::ProgressBarStyle::Rich};
    }();
    return mode;
}

}  // namespace skwr
