#pragma once

#include <array>
#include <cstring>
#include <optional>

namespace sbk::compat {

struct Snapshot {
    int mode;
    int course;
    int character;
    int players;
    int item;
    int results;
    int progression;
};

struct Event {
    const char* name;
    Snapshot snapshot;
};

// Called only in opt-in diagnostic mode. It observes named guest callbacks
// and emits an item ID once per race; it never writes guest memory.
class Coverage {
public:
    std::optional<Event> observe(const char* function, Snapshot snapshot) {
        if (!function) return std::nullopt;
        auto is = [function](const char* name) { return std::strcmp(function, name) == 0; };
        if (is("initTitleDemoRaceIntro")) return Event{"title_enter", snapshot};
        if (is("initMainMenuDemoRaceIntro")) return Event{"attract_enter", snapshot};
        if (is("initMainMenu")) return Event{"main_menu", snapshot};
        if (is("initMainMenuSettings")) return Event{"options", snapshot};
        if (is("initRaceSetupMenu")) return Event{"player_count", snapshot};
        if (is("initRaceSetupSaveMenu")) return Event{"save_select", snapshot};
        if (is("initCharacterSelectMenu")) return Event{"character_select", snapshot};
        if (is("initCourseSelectMenu") || is("initMultiplayerCourseSelectMenu"))
            return Event{"course_select", snapshot};
        if (is("initRaceSplitscreenSelectMenu")) return Event{"splitscreen_select", snapshot};
        if (is("initRaceTypeSelectMenu")) return Event{"race_type_select", snapshot};
        if (is("initTrainingCourseRace")) return Event{"training", snapshot};
        if (is("initRaceGhostReplayFlow")) return Event{"replay", snapshot};
        if (is("initEndingCreditsFlow")) return Event{"ending", snapshot};
        if (is("initRaceSceneFlow")) {
            race_started_ = true;
            finished_ = false;
            items_.fill(false);
            return Event{"race_start", snapshot};
        }
        if (is("waitRaceFinishResultsFlow") && race_started_ && !finished_) {
            finished_ = true;
            return Event{"race_finish", snapshot};
        }
        if (is("updateRaceGameplayFlow") && race_started_ && snapshot.item >= 1 && snapshot.item <= 5) {
            if (!items_[snapshot.item]) {
                items_[snapshot.item] = true;
                return Event{"item_observed", snapshot};
            }
        }
        return std::nullopt;
    }

private:
    bool race_started_ = false;
    bool finished_ = false;
    std::array<bool, 6> items_{};
};

} // namespace sbk::compat
