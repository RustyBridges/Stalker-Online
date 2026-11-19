// hit_immunity.h: класс для тех объектов, которые поддерживают
//				   коэффициенты иммунитета для разных типов хитов
//////////////////////////////////////////////////////////////////////

#pragma once

#include "alife_space.h"
#include "hit_immunity_space.h"

class CInifile;

class CHitImmunity
{
    //коэффициенты на которые домножается хит
    //при соответствующем типе воздействия
    //(для защитных костюмов и специфичных животных)
private:
    HitImmunity::HitTypeSVec m_HitImmunityKoefs;

public:
    CHitImmunity();
    virtual ~CHitImmunity() = default;

    void LoadImmunities(const char* section, const CInifile* ini, bool invert = false);
    void AddImmunities(const char* section, const CInifile* ini, bool invert = false);
    float GetHitImmunity(ALife::EHitType hit_type) const { return m_HitImmunityKoefs[hit_type]; }
    float AffectHit(float power, ALife::EHitType hit_type) const { return power * GetHitImmunity(hit_type); }

    // stalker-online loot system: scale how far each coefficient is from 1.
    // Protective coefficients (< 1) by protective_k, harmful ones (> 1) by harmful_k.
    void ScaleImmunities(float protective_k, float harmful_k)
    {
        for (float& k : m_HitImmunityKoefs)
        {
            if (k < 1.f)
                k = std::max(0.f, 1.f - (1.f - k) * protective_k);
            else if (k > 1.f)
                k = 1.f + (k - 1.f) * harmful_k;
        }
    }
};
