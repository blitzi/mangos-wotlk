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

#include "Common.h"
#include "Tools/CharacterDatabaseCleaner.h"
#include "World/World.h"
#include "Database/DatabaseEnv.h"
#include "Server/DBCStores.h"
#include "ProgressBar.h"
#include "Server/SQLStorages.h"

void CharacterDatabaseCleaner::CleanDatabase()
{
    // config to disable
    if (!sWorld.getConfig(CONFIG_BOOL_CLEAN_CHARACTER_DB))
        return;

    sLog.outString("Cleaning character database...");

    // check flags which clean ups are necessary
    QueryResult* result = CharacterDatabase.PQuery("SELECT cleaning_flags FROM saved_variables");
    if (!result)
        return;
    uint32 flags = (*result)[0].GetUInt32();
    delete result;

    // clean up
    if (flags & CLEANING_FLAG_ACHIEVEMENT_PROGRESS)
        CleanCharacterAchievementProgress();
    if (flags & CLEANING_FLAG_SKILLS)
        CleanCharacterSkills();
    if (flags & CLEANING_FLAG_SPELLS)
        CleanCharacterSpell();
    if (flags & CLEANING_FLAG_TALENTS)
        CleanCharacterTalent();
    CharacterDatabase.Execute("UPDATE saved_variables SET cleaning_flags = 0");
}

void CharacterDatabaseCleaner::CheckUnique(const char* column, const char* table, bool (*check)(uint32))
{
    QueryResult* result = CharacterDatabase.PQuery("SELECT DISTINCT %s FROM %s", column, table);
    if (!result)
    {
        sLog.outString("Table %s is empty.", table);
        return;
    }

    bool found = false;
    std::ostringstream ss;
    BarGoLink bar(result->GetRowCount());
    do
    {
        bar.step();

        Field* fields = result->Fetch();

        uint32 id = fields[0].GetUInt32();

        if (!check(id))
        {
            if (!found)
            {
                ss << "DELETE FROM " << table << " WHERE " << column << " IN (";
                found = true;
            }
            else
                ss << ",";
            ss << id;
        }
    }
    while (result->NextRow());
    delete result;

    if (found)
    {
        ss << ")";
        CharacterDatabase.Execute(ss.str().c_str());
    }
}

bool CharacterDatabaseCleaner::AchievementProgressCheck(uint32 criteria)
{
    return sAchievementCriteriaStore.LookupEntry(criteria) != nullptr;
}

void CharacterDatabaseCleaner::CleanCharacterAchievementProgress()
{
    CheckUnique("criteria", "character_achievement_progress", &AchievementProgressCheck);
}

bool CharacterDatabaseCleaner::SkillCheck(uint32 skill)
{
    return sSkillLineStore.LookupEntry(skill) != nullptr;
}

void CharacterDatabaseCleaner::CleanCharacterSkills()
{
    CheckUnique("skill", "character_skills", &SkillCheck);
}

bool CharacterDatabaseCleaner::SpellCheck(uint32 spell_id)
{
    return sSpellTemplate.LookupEntry<SpellEntry>(spell_id) && !GetTalentSpellPos(spell_id);
}

void CharacterDatabaseCleaner::CleanCharacterSpell()
{
    CheckUnique("spell", "character_spell", &SpellCheck);
}

bool CharacterDatabaseCleaner::TalentCheck(uint32 talent_id)
{
    TalentEntry const* talentInfo = sTalentStore.LookupEntry(talent_id);
    if (!talentInfo)
        return false;

    return sTalentTabStore.LookupEntry(talentInfo->TalentTab) != nullptr;
}

void CharacterDatabaseCleaner::CleanCharacterTalent()
{
    CharacterDatabase.DirectPExecute("DELETE FROM character_talent WHERE spec > %u OR current_rank > %u", MAX_TALENT_SPEC_COUNT, MAX_TALENT_RANK);

    CheckUnique("talent_id", "character_talent", &TalentCheck);
}

void CharacterDatabaseCleaner::CleanItemIds()
{

	QueryResult* result = CharacterDatabase.PQuery("SELECT DISTINCT guid FROM item_instance ORDER BY guid ASC");
	BarGoLink bar(result->GetRowCount());
	
	int newItemGuid = 1;

	do
	{
		bool idChanged = false;
		bar.step();

		Field* fields = result->Fetch();
		int itemId = fields[0].GetInt32();
	
		QueryResult* ciItemResult = CharacterDatabase.PQuery("SELECT item from character_inventory WHERE item = '%u'", itemId);
		if (ciItemResult)
		{
			CharacterDatabase.PExecute("UPDATE character_inventory SET item = '%u' WHERE item = '%u'", newItemGuid, itemId);
			idChanged = true;
		}
		delete ciItemResult;

		QueryResult* ciBagResult = CharacterDatabase.PQuery("SELECT bag from character_inventory WHERE bag = '%u'", itemId);
		if (ciBagResult)
		{
			CharacterDatabase.PExecute("UPDATE character_inventory SET bag = '%u' WHERE bag = '%u'", newItemGuid, itemId);
			idChanged = true;
		}
		delete ciBagResult;

		QueryResult* caResult = CharacterDatabase.PQuery("SELECT item_guid from character_aura WHERE item_guid = '%u'", itemId);
		if (caResult)
		{
			CharacterDatabase.PExecute("UPDATE character_aura SET item_guid = '%u' WHERE item_guid = '%u'", newItemGuid, itemId);
			idChanged = true;
		}
		delete caResult;

		QueryResult* gbResult = CharacterDatabase.PQuery("SELECT item_guid from guild_bank_item WHERE item_guid = '%u'", itemId);
		if (gbResult)
		{
			CharacterDatabase.PExecute("UPDATE guild_bank_item SET item_guid = '%u' WHERE item_guid = '%u'", newItemGuid, itemId);
			idChanged = true;
		}
		delete gbResult;

		QueryResult* miResult = CharacterDatabase.PQuery("SELECT item_guid from mail_items WHERE item_guid = '%u'", itemId);
		if (miResult)
		{
			CharacterDatabase.PExecute("UPDATE mail_items SET item_guid = '%u' WHERE item_guid = '%u'", newItemGuid, itemId);
			idChanged = true;
		}
		delete miResult;

		QueryResult* paResult = CharacterDatabase.PQuery("SELECT item_guid from pet_aura WHERE item_guid = '%u'", itemId);
		if (paResult)
		{
			CharacterDatabase.PExecute("UPDATE pet_aura SET item_guid = '%u' WHERE item_guid = '%u'", newItemGuid, itemId);
			idChanged = true;
		}
		delete paResult;

		for (int setId = 0; setId <= 18; setId++)
		{
			QueryResult* setResult = CharacterDatabase.PQuery("SELECT item%u from character_equipmentsets WHERE item%u = '%u'", setId, setId, itemId);
			if (setResult)
			{
				CharacterDatabase.PExecute("UPDATE character_equipmentsets SET item%u = '%u' WHERE item%u = '%u'", setId, newItemGuid, setId, itemId);
				idChanged = true;
			}
		}

		/*QueryResult* aResult = CharacterDatabase.PQuery("SELECT item_guid from auction WHERE item_guid = '%u'", oldId);

		if (aResult)
		{
			int x = 0;
		}*/

		if (idChanged)
		{
			CharacterDatabase.PExecute("UPDATE item_instance SET guid = '%u' WHERE guid = '%u'", newItemGuid, itemId);
			newItemGuid++;
		}
		else
		{
			CharacterDatabase.PExecute("DELETE FROM item_instance WHERE guid = '%u'", itemId);
			sLog.outString("item id %u not used", itemId);
		}

	} while (result->NextRow());

	delete result;
}
