#include "StdAfx.h"

// stalker-online loot system: how each item type applies a rolled stat change.
// `value` is relative (e.g. -0.2 = 20% less) unless noted. See ItemLoot.h.

#include "Weapon.h"
#include "CustomOutfit.h"
#include "ActorHelmet.h"
#include "Artefact.h"
#include "CustomDetector.h"
#include "BoneProtections.h"

using namespace loot;

namespace
{
void ScaleRecoil(CameraRecoil& recoil, float k)
{
    recoil.Dispersion *= k;
    recoil.DispersionInc *= k;
    recoil.MaxAngleVert *= k;
    recoil.MaxAngleHorz *= k;
    recoil.StepAngleHorz *= k;
}

void ScaleArmor(SBoneProtections& bones, float k)
{
    for (auto& [bone, protection] : bones.m_bones_koeff)
        protection.armor *= k;
}

// Hit types covered by a protection stat, -1 if the stat is not a protection.
int ProtectionHitType(EStat stat)
{
    switch (stat)
    {
    case stBurn: return ALife::eHitTypeBurn;
    case stShock: return ALife::eHitTypeShock;
    case stChemical: return ALife::eHitTypeChemicalBurn;
    case stRadiation: return ALife::eHitTypeRadiation;
    case stPsi: return ALife::eHitTypeTelepatic;
    default: return -1;
    }
}

void ScaleProtections(HitImmunity::HitTypeSVec& protection, EStat stat, float k)
{
    if (stat == stArmor)
    {
        for (const auto hit : { ALife::eHitTypeWound, ALife::eHitTypeFireWound, ALife::eHitTypeStrike,
                 ALife::eHitTypeExplosion, ALife::eHitTypeLightBurn })
        {
            protection[hit] *= k;
        }
        return;
    }
    const int hit = ProtectionHitType(stat);
    if (hit >= 0)
        protection[hit] *= k;
}
} // namespace

void CWeapon::loot_apply_stat(EStat stat, float value)
{
    const float k = std::max(0.05f, 1.f + value);
    switch (stat)
    {
    case stDispersion: fireDispersionBase *= k; break;
    case stRecoil:
        ScaleRecoil(cam_recoil, k);
        ScaleRecoil(zoom_cam_recoil, k);
        break;
    case stDamage:
        fvHitPower.mul(k);
        fvHitPowerCritical.mul(k);
        break;
    case stFireRate: fOneShotTime /= k; break; // faster = less time per shot
    case stMagSize: iMagazineSize = std::max(1, iFloor(float(iMagazineSize) * k + 0.5f)); break;
    case stWear: conditionDecreasePerShot *= k; break;
    default: inherited::loot_apply_stat(stat, value); break;
    }
}

void CCustomOutfit::loot_apply_stat(EStat stat, float value)
{
    const float k = std::max(0.05f, 1.f + value);
    switch (stat)
    {
    case stArmor:
        ScaleProtections(m_HitTypeProtection, stat, k);
        ScaleArmor(m_boneProtection, k);
        break;
    case stBurn:
    case stShock:
    case stChemical:
    case stRadiation:
    case stPsi: ScaleProtections(m_HitTypeProtection, stat, k); break;
    case stCarry: // absolute kg
        m_additional_weight += value;
        m_additional_weight2 += value;
        break;
    default: inherited::loot_apply_stat(stat, value); break;
    }
}

void CHelmet::loot_apply_stat(EStat stat, float value)
{
    const float k = std::max(0.05f, 1.f + value);
    switch (stat)
    {
    case stArmor:
        ScaleProtections(m_HitTypeProtection, stat, k);
        if (m_boneProtection)
            ScaleArmor(*m_boneProtection, k);
        break;
    case stBurn:
    case stShock:
    case stChemical:
    case stRadiation:
    case stPsi: ScaleProtections(m_HitTypeProtection, stat, k); break;
    default: inherited::loot_apply_stat(stat, value); break;
    }
}

void CArtefact::loot_apply_stat(EStat stat, float value)
{
    const float k = std::max(0.f, 1.f + value);
    if (stat != stArtefactPositive && stat != stArtefactNegative)
    {
        inherited::loot_apply_stat(stat, value);
        return;
    }

    const bool positive = stat == stArtefactPositive;
    // Scale a value whose "good" direction is `good_sign` (+1: higher is better).
    auto scale = [&](float& v, float good_sign)
    {
        const bool beneficial = v * good_sign > 0.f;
        if (beneficial == positive)
            v *= k;
    };
    scale(m_fHealthRestoreSpeed, 1.f);
    scale(m_fSatietyRestoreSpeed, 1.f);
    scale(m_fPowerRestoreSpeed, 1.f);
    scale(m_fBleedingRestoreSpeed, 1.f);
    scale(m_fRadiationRestoreSpeed, -1.f); // positive radiation restore = gaining radiation
    scale(m_additional_weight, 1.f);

    if (positive)
        m_ArtefactHitImmunities.ScaleImmunities(k, 1.f);
    else
        m_ArtefactHitImmunities.ScaleImmunities(1.f, k);
}

void CCustomDetector::loot_apply_stat(EStat stat, float value)
{
    if (stat == stDetectRange)
    {
        const float k = std::max(0.1f, 1.f + value);
        m_fAfDetectRadius *= k;
        m_fAfVisRadius *= k;
        return;
    }
    inherited::loot_apply_stat(stat, value);
}
