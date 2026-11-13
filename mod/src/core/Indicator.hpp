#pragma once

#include "Hacks.hpp"
#include <vector>

// Pointercrate-rules cheat indicator.
//   green = no hack Pointercrate disallows has been used this attempt
//   red   = one is on, or was on earlier in this attempt (until you restart)
// "Sometimes allowed" hacks don't count.
namespace cmm::indicator {
    // Allowed or NotAllowed, from the hacks that are on right now.
    PC current();
    // For the labels: NotAllowed if one is on now or was used earlier this attempt.
    PC attempt();
    // True if a disallowed hack was on at any point this attempt. Used by
    // Auto Safe Mode so turning a cheat off mid-run can't save progress.
    bool usedThisAttempt();
    // Names of the hacks making it red.
    std::vector<std::string> const& reasons();

    // Called by PlayLayer hooks.
    void onAttemptStart();
    void onFrame();
}
