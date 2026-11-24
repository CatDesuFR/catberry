#pragma once
// Differences between the GD versions the mod builds for: 2.2091 (Geode 6)
// and 2.2081 (Geode 5). GEODE_COMP_GD_VERSION is 22091 or 22081.
#include <Geode/Geode.hpp>

#if GEODE_COMP_GD_VERSION >= 22091
#define CMM_GD_2091 1
// PlayLayer::init(level, dontCreateObjects)
#define CMM_PL_INIT_PARAMS GJGameLevel* level, bool dontCreateObjects
#define CMM_PL_INIT_ARGS level, dontCreateObjects
#define CMM_SET_START_POS(layer, obj) (layer)->setActiveStartPosObject(obj)
#else
#define CMM_GD_2091 0
// PlayLayer::init(level, useReplay, dontCreateObjects)
#define CMM_PL_INIT_PARAMS GJGameLevel* level, bool useReplay, bool dontCreateObjects
#define CMM_PL_INIT_ARGS level, useReplay, dontCreateObjects
#define CMM_SET_START_POS(layer, obj) (layer)->setStartPosObject(obj)
#endif
