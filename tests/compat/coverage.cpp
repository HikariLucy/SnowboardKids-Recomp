#include "compat/coverage.hpp"
#include <cassert>
#include <cstring>

int main() {
    sbk::compat::Coverage coverage;
    sbk::compat::Snapshot guest{2, 7, 3, 4, 0, 0, 1};
    assert(!coverage.observe("updateRaceGameplayFlow", guest));
    auto title = coverage.observe("initTitleDemoRaceIntro", guest);
    assert(title && std::strcmp(title->name, "title_enter") == 0);
    assert(!coverage.observe("updateTitleDemoRaceIntro", guest));
    auto menu = coverage.observe("initMainMenu", guest);
    assert(menu && std::strcmp(menu->name, "main_menu") == 0);
    auto character = coverage.observe("initCharacterSelectMenu", guest);
    assert(character && std::strcmp(character->name, "character_select") == 0);
    assert(character->snapshot.character == 3 && character->snapshot.players == 4);
    auto course = coverage.observe("initMultiplayerCourseSelectMenu", guest);
    assert(course && std::strcmp(course->name, "course_select") == 0);
    auto race = coverage.observe("initRaceSceneFlow", guest);
    assert(race && std::strcmp(race->name, "race_start") == 0);
    assert(race->snapshot.course == 7 && race->snapshot.mode == 2);
    assert(!coverage.observe("updateRaceGameplayFlow", guest));
    guest.item = 5;
    auto item = coverage.observe("updateRaceGameplayFlow", guest);
    assert(item && std::strcmp(item->name, "item_observed") == 0 && item->snapshot.item == 5);
    assert(!coverage.observe("updateRaceGameplayFlow", guest));
    guest.item = 0;
    assert(!coverage.observe("updateRaceGameplayFlow", guest));
    guest.item = 5;
    assert(!coverage.observe("updateRaceGameplayFlow", guest));
    auto finish = coverage.observe("waitRaceFinishResultsFlow", guest);
    assert(finish && std::strcmp(finish->name, "race_finish") == 0);
    assert(!coverage.observe("waitRaceFinishResultsFlow", guest));
    auto next_race = coverage.observe("initRaceSceneFlow", guest);
    assert(next_race && std::strcmp(next_race->name, "race_start") == 0);
}
