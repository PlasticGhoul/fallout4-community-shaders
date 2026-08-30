#pragma once

// Pulls in F4SE/Impl/PCH.h, which in turn brings REL, REX (including REX::INFO
// and REX::FModule) and the RE ID tables. One include covers everything
// subproject A needs.
//
// <RE/Fallout.h> is deliberately absent: subproject A touches no engine type,
// and its ~1450 headers would slow every translation unit. Subproject B adds it
// when it starts using them.
#include <F4SE/F4SE.h>

using namespace std::literals;
