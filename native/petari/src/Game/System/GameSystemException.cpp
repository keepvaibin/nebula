#include "Game/System/GameSystemException.hpp"
#include <JSystem/JUtility/JUTAssert.hpp>
#include <JSystem/JUtility/JUTConsole.hpp>

void GameSystemException::init() {
    JUTAssertion::create();
    JUTAssertion::changeDisplayTime(600);
    JUTAssertion::changeDevice(3);
    JUTConsoleManager::createManager(nullptr);
}
