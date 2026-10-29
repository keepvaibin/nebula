#include "Game/System/GameSystemException.hpp"
#include <JSystem/JUtility/JUTAssert.hpp>
#include <JSystem/JUtility/JUTConsole.hpp>
#include <JSystem/JUtility/JUTDirectPrint.hpp>

void GameSystemException::init() {
    JUTDirectPrint::start();

    JUTAssertion::create();
    JUTAssertion::changeDisplayTime(600);
    JUTAssertion::changeDevice(3);
    JUTConsoleManager::createManager(nullptr);
}
