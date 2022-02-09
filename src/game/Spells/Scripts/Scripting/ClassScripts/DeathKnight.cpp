/*
* This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "Spells/Scripts/SpellScript.h"
#include "Spells/SpellAuras.h"

struct ScourgeStrike : public SpellScript
{
    void OnHit(Spell* spell, SpellMissInfo missInfo) const override
    {
        uint32 count = 0;
        Unit* target = spell->GetUnitTarget();
        Unit::SpellAuraHolderMap const& auras = target->GetSpellAuraHolderMap();
        for (const auto& aura : auras)
        {
            if (aura.second->GetSpellProto()->Dispel == DISPEL_DISEASE &&
                aura.second->GetCasterGuid() == spell->GetCaster()->GetObjectGuid())
                ++count;
        }

        if (count)
        {
            int32 bp = count * spell->CalculateSpellEffectValue(EFFECT_INDEX_2, target) * spell->GetTotalTargetDamage() / 100;
            if (bp)
                spell->GetCaster()->CastCustomSpell(target, 70890, &bp, nullptr, nullptr, TRIGGERED_OLD_TRIGGERED);
        }
    }
};

struct RaiseDead : public SpellScript
{
    bool OnCheckTarget(const Spell* spell, Unit* target, SpellEffectIndex eff) const override
    {
        if (eff == EFFECT_INDEX_1)
        {
            Unit* caster = spell->GetCaster();
            if (target->IsTrivialForTarget(caster))
                return false;

            if (target->GetDeathState() != CORPSE)
                return false;

            if (target->GetCreatureType() != CREATURE_TYPE_HUMANOID)
                return false;
        }

        return true;
    }

    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx == EFFECT_INDEX_2) // reagent consuming version
            if (spell->GetTargetList().size() > 1) // more than caster is hit
                return;

        Unit* caster = spell->GetCaster();
        uint32 spellId = caster->HasAura(52143) ? 52150 : 46585; // Master of Ghouls talent
        if (effIdx == EFFECT_INDEX_2) // corpse dust version
        {
            if (caster->CastSpell(nullptr, 48289, TRIGGERED_IGNORE_GCD) == SPELL_CAST_OK)
            {
                Position pos = Pet::GetPetSpawnPosition(caster);
                caster->CastSpell(pos.x, pos.y, pos.z, spellId, TRIGGERED_OLD_TRIGGERED);
            }
            else
                caster->RemoveSpellCooldown(*spell->m_spellInfo, true);
        }
        else if (effIdx == EFFECT_INDEX_1) // corpse version
            caster->CastSpell(spell->GetUnitTarget(), spellId, TRIGGERED_OLD_TRIGGERED);
    }
};

struct DeathCoilDK : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_0)
            return;

        uint32 damage = spell->GetDamage();
        Unit* caster = spell->GetCaster();
        Unit* target = spell->GetUnitTarget();
        if (caster->CanAssistSpell(target, spell->m_spellInfo))
        {
            if (!target || target->GetCreatureType() != CREATURE_TYPE_UNDEAD)
                return;

            int32 bp = int32(damage * 1.5f);
            caster->CastCustomSpell(target, 47633, &bp, nullptr, nullptr, TRIGGERED_OLD_TRIGGERED);
        }
        else
        {
            int32 bp = damage;
            caster->CastCustomSpell(target, 47632, &bp, nullptr, nullptr, TRIGGERED_OLD_TRIGGERED);
        }
    }
};

struct UnholyBlightDK : public AuraScript
{
    SpellAuraProcResult OnProc(Aura* aura, ProcExecutionData& procData) const override
    {
        procData.triggeredSpellId = 50536; // unholy blight DOT
        uint32 damagePercent = aura->GetModifier()->m_amount;
        if (Aura* glyphAura = aura->GetTarget()->GetAura(63332, EFFECT_INDEX_0))
            damagePercent += (damagePercent * glyphAura->GetModifier()->m_amount / 100);
        procData.basepoints[0] = procData.damage * damagePercent / 100;
        return SPELL_AURA_PROC_OK;
    }
};

struct SuddenDoom : public AuraScript
{
    SpellAuraProcResult OnProc(Aura* aura, ProcExecutionData& procData) const override
    {
        Player* player = dynamic_cast<Player*>(aura->GetTarget());
        if (!player)
            return SPELL_AURA_PROC_OK;

        // dk death coil rank 1 id - need max
        procData.triggeredSpellId = player->LookupHighestLearnedRank(47541);
        return SPELL_AURA_PROC_OK;
    }
};

struct DeathRuneDK : public AuraScript
{
    bool OnCheckProc(Aura* aura, ProcExecutionData& procData) const override
    {
        if (!procData.spell || !aura->GetTarget()->IsPlayer())
            return false;

        if (aura->GetTarget()->getClass() != CLASS_DEATH_KNIGHT)
            return false;

        return true;
    }

    SpellAuraProcResult OnProc(Aura* aura, ProcExecutionData& procData) const override
    {
        uint8 oldRunesState = procData.spell->GetOldRuneState();
        uint8 newRunesState = procData.spell->GetNewRuneState();
        if (oldRunesState == newRunesState)
            return SPELL_AURA_PROC_OK;

        Player* player = static_cast<Player*>(aura->GetTarget());

        for (uint8 i = 0; i < MAX_RUNES; ++i)
        {
            uint8 mask = (1 << i);
            if (mask & oldRunesState && (!(mask & newRunesState))) // runes that were used by spell as cost
            {
                player->AddRuneByAuraEffect(i, RUNE_DEATH, aura);
            }
        }

        aura->ForcePeriodicity(aura->GetModifier()->periodictime);
        return SPELL_AURA_PROC_OK;
    }

    void OnPeriodicDummy(Aura* aura) const override
    {
        Unit* target = aura->GetTarget();
        if (!target->IsPlayer())
            return;

        // timer expired - remove death runes
        static_cast<Player*>(target)->RemoveRunesByAura(aura);
    }
};

struct Bloodworm : public SpellScript
{
    void OnSummon(Spell* /*spell*/, Creature* summon) const override
    {
        summon->CastSpell(nullptr, 50453, TRIGGERED_OLD_TRIGGERED);
        summon->AI()->SetReactState(REACT_DEFENSIVE);
    }
};

struct HealthLeechPassive : public AuraScript
{
    SpellAuraProcResult OnProc(Aura* aura, ProcExecutionData& procData) const override
    {
        int32 heal = procData.damage * 150 / 100; // 1.5x
        aura->GetTarget()->CastCustomSpell(aura->GetTarget()->GetSpawner(), 50454, &heal, nullptr, nullptr, TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_CURRENT_CASTED_SPELL);
        return SPELL_AURA_PROC_OK;
    }
};

struct AntiMagicZone : public SpellScript
{
    void OnSummon(Spell* /*spell*/, Creature* summon) const override
    {
        summon->SetFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_HIDE_BODY);
        summon->CastSpell(nullptr, 40031, TRIGGERED_OLD_TRIGGERED);
    }
};

struct CorpseExplosionDK : public SpellScript
{
    bool OnCheckTarget(const Spell* spell, Unit* target, SpellEffectIndex eff) const override
    {
        if (eff == EFFECT_INDEX_0) // can be cast on a ghoul
        {
            if (target->IsPlayerControlled() && target->GetOwnerGuid() == spell->GetCaster()->GetObjectGuid() &&
                target->GetEntry() == 26125)
                return true;
        }
        
        if (target->GetDeathState() == CORPSE &&
            (target->GetCreatureTypeMask() & CREATURE_TYPEMASK_MECHANICAL_OR_ELEMENTAL) == 0)
            return true;

        return false;
    }

    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (spell->GetScriptValue() == 1)
            return;

        spell->SetScriptValue(1); // mark as used
        Unit* target = spell->GetUnitTarget();
        if (effIdx == EFFECT_INDEX_0) // ghoul
        {
            target->InterruptNonMeleeSpells(false);
            target->CastSpell(nullptr, 47496, TRIGGERED_NORMAL_COMBAT_CAST);
        }
        else
        {
            int32 damage = spell->CalculateSpellEffectValue(EFFECT_INDEX_0, target);
            spell->GetCaster()->CastCustomSpell(spell->GetUnitTarget(), spell->CalculateSpellEffectValue(EFFECT_INDEX_1, spell->GetUnitTarget()),
                &damage, nullptr, nullptr, TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_CURRENT_CASTED_SPELL);
            spell->GetCaster()->CastSpell(spell->GetUnitTarget(), 51270, TRIGGERED_OLD_TRIGGERED);
        }
    }
};

struct ExplodeGhoulCorpseExplosion : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx == EFFECT_INDEX_0)
        {
            int32 percentage = spell->CalculateSpellEffectValue(EFFECT_INDEX_2, spell->GetUnitTarget());
            spell->SetDamage(spell->GetCaster()->GetMaxHealth() * percentage / 100);
		}
        else
        {
            spell->SetDamage(spell->GetCaster()->GetMaxHealth());
        }
    }

    void OnSuccessfulFinish(Spell* spell) const override
    {
        // Suicide
        if (spell->GetCaster()->IsAlive())
            spell->GetCaster()->CastSpell(nullptr, 7, TRIGGERED_OLD_TRIGGERED);
        // Set corpse look
        if (spell->GetCaster()->IsCreature())
            static_cast<Creature*>(spell->GetCaster())->ForcedDespawn(500);
    }          
};

void LoadDeathKnightScripts()
{
    RegisterSpellScript<ScourgeStrike>("spell_scourge_strike");
    RegisterSpellScript<RaiseDead>("spell_dk_raise_dead");
    RegisterSpellScript<DeathCoilDK>("spell_dk_death_coil");
    RegisterAuraScript<UnholyBlightDK>("spell_dk_unholy_blight");
    RegisterAuraScript<SuddenDoom>("spell_sudden_doom");
    RegisterAuraScript<DeathRuneDK>("spell_death_rune_dk");
    RegisterSpellScript<Bloodworm>("spell_bloodworm");
    RegisterAuraScript<HealthLeechPassive>("spell_health_leech_passive");
    RegisterSpellScript<AntiMagicZone>("spell_anti_magic_zone");
    RegisterSpellScript<CorpseExplosionDK>("spell_dk_corpse_explosion");
    RegisterSpellScript<ExplodeGhoulCorpseExplosion>("spell_explode_ghoul_corpse_explosion");
}