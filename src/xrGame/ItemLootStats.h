#pragma once

// Stats that loot affixes can change (stalker-online loot system, see ItemLoot.h).
// Values are relative ("pct": -0.2 = 20% less), except where noted.
namespace loot
{
enum EStat : u8
{
    stWeight, // all items
    // weapons
    stDispersion,
    stRecoil,
    stDamage,
    stFireRate,
    stMagSize,
    stWear, // condition lost per shot
    // suits and helmets
    stArmor, // bullet / wound / strike / explosion protection
    stBurn,
    stShock,
    stChemical,
    stRadiation,
    stPsi,
    stCarry, // absolute: extra carry weight in kg (suits)
    // artefacts
    stArtefactPositive, // strength of beneficial effects
    stArtefactNegative, // strength of harmful effects
    // detectors
    stDetectRange,
    stCount,
    stInvalid = stCount
};
} // namespace loot
