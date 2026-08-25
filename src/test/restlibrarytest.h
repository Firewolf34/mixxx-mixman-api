#pragma once

#include "mixer/playerinfo.h"
#include "test/librarytest.h"

class RestLibraryTest : public LibraryTest {
  public:
    RestLibraryTest() {
        PlayerInfo::create();
    }

    ~RestLibraryTest() override {
        PlayerInfo::destroy();
    }
};
