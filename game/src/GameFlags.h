#pragma once
#include <cstdint>

// All Flag IDs for this game
// Ranges:
//    10000-19999: World / environment
//    20000-29999: Boss kills
//    30000-39999: NPC Quest Lines
//    40000-49999: Player progression
namespace GameFlags {
// World
constexpr uint32_t BONFIRE_HIGHWALL_LIT = 10000;
constexpr uint32_t GATE_HIGHWALL_OPEN = 10001;
constexpr uint32_t FOGGATE_VORDT_OPEN = 10002;


// Bosses
constexpr uint32_t BOSS_DUMMY_DEAD = 20000;
constexpr uint32_t BOSS_VORDT_DEAD = 20001;
constexpr uint32_t BOSS_DANCER_DEAD = 20002;

// NPC Quests
constexpr uint32_t QUEST_SIEGWARD_MET = 30000;
constexpr uint32_t QUEST_SIEGWARD_GAVE_ESTUS = 30001;
constexpr uint32_t QUEST_SIEGWARD_COMPLETE = 30002;

// Player
constexpr uint32_t PLAYER_HAS_LORDVESSEL = 40000;
constexpr uint32_t PLAYER_KINDLED_FIRST_FLAME = 40001;
}
