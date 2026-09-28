#include "ui/menu_actions.hpp"
#include <cassert>
#include <thread>
int main() {
    sbk::ui::MenuActions queue;
    assert(queue.take() == sbk::ui::MenuAction::None);
    // A render-thread click may not overwrite an unconsumed save request.
    std::thread producer([&] {
        assert(queue.submit(sbk::ui::MenuAction::QuickSave));
        assert(!queue.submit(sbk::ui::MenuAction::QuickLoad));
    });
    producer.join();
    assert(queue.take() == sbk::ui::MenuAction::QuickSave);
    assert(queue.take() == sbk::ui::MenuAction::None);
    assert(queue.submit(sbk::ui::MenuAction::QuickLoad));
    assert(queue.take() == sbk::ui::MenuAction::QuickLoad);
}
