/*
 * Copyright (C) 2005-2013 MaNGOS <http://getmangos.com/>
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

#include "ObjectMgr.h"
#include "Player.h"
#include "Spell.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"

#define MAX_RESEARCH_PROJECTS 12

enum ArchaeologyBotDistance
{
    ARCHAEOLOGY_DIG_SITE_FAR_DIST   = 60,
    ARCHAEOLOGY_DIG_SITE_MED_DIST   = 20,
    ARCHAEOLOGY_DIG_SITE_FIND_DIST  = 5
};

const static int q_patt[2][2] = { { 0, 1 }, { 3, 2 } };

typedef std::set<uint32> SiteSet;

bool Player::GenerateDigSiteLoot(uint16 siteId, DigSite &site)
{
    ResearchSiteDataMap::iterator dataItr = sResearchSiteDataMap.find(siteId);
    if (dataItr == sResearchSiteDataMap.end())
        return false;

    DigSitePositionVector const& loot = dataItr->second.digSites;
    if (loot.empty())
        return false;

    switch (dataItr->second.branch_id)
    {
        case ARCHAEOLOGY_BRANCH_DWARF:      site.find_id = GO_DWARF_FIND; break;
        case ARCHAEOLOGY_BRANCH_DRAENEI:    site.find_id = GO_DRAENEI_FIND; break;
        case ARCHAEOLOGY_BRANCH_FOSSIL:     site.find_id = GO_FOSSIL_FIND; break;
        case ARCHAEOLOGY_BRANCH_NIGHT_ELF:  site.find_id = GO_NIGHT_ELF_FIND; break;
        case ARCHAEOLOGY_BRANCH_NERUBIAN:   site.find_id = GO_NERUBIAN_FIND; break;
        case ARCHAEOLOGY_BRANCH_ORC:        site.find_id = GO_ORC_FIND; break;
        case ARCHAEOLOGY_BRANCH_TOLVIR:     site.find_id = GO_TOLVIR_FIND; break;
        case ARCHAEOLOGY_BRANCH_TROLL:      site.find_id = GO_TROLL_FIND; break;
        case ARCHAEOLOGY_BRANCH_VRYKUL:     site.find_id = GO_VRYKUL_FIND; break;
        case ARCHAEOLOGY_BRANCH_MANTID:     site.find_id = GO_MANTID_FIND; break;
        case ARCHAEOLOGY_BRANCH_PANDAREN:   site.find_id = GO_PANDAREN_FIND; break;
        case ARCHAEOLOGY_BRANCH_MOGU:       site.find_id = GO_MOGU_FIND; break;
        default:                            site.find_id = 0; break;
    }

    DigSitePositionVector::const_iterator entry = loot.begin();
    std::advance(entry, urand(0, loot.size() - 1));

    site.loot_x = entry->x;
    site.loot_y = entry->y;

    return true;
}

bool Player::OnSurvey(uint32& entry, float& x, float& y, float& z, float &orientation)
{
    entry = 0;

    uint16 skill_now = GetSkillValue(SKILL_ARCHAEOLOGY);
    if (!skill_now)
        return false;

    uint16 site_id = GetResearchSiteID();
    if (!site_id)
        return false;

    uint8 i = 0;
    for(; i < MAX_RESEARCH_SITES; ++i)
        if (GetDynamicUInt32Value(PLAYER_DYNAMIC_RESEARCH_SITES, i) == site_id)
            break;

    ASSERT(i < MAX_RESEARCH_SITES);

    DigSite &site = _digSites[i];
    if (site.site_id != site_id)
    {
        if (!GenerateDigSiteLoot(site_id, site))
            return false;

        site.site_id = site_id;
    }

    ResearchSiteDataMap::iterator rsite = sResearchSiteDataMap.find(site_id);
    if (rsite == sResearchSiteDataMap.end())
        return false;

    orientation = GetAngle(site.loot_x, site.loot_y);
    float dist_now = GetDistance2d(site.loot_x, site.loot_y);

    if (dist_now >= ARCHAEOLOGY_DIG_SITE_FAR_DIST)
    {
        entry = GO_FAR_SURVEYBOT;
        orientation += frand(-M_PI / 6, M_PI / 6);
        SendSurveyCast(site.count, MAX_DIGSITE_FINDS, rsite->second.branch_id, false);
        return false;
    }
    if (dist_now >= ARCHAEOLOGY_DIG_SITE_MED_DIST)
    {
        entry = GO_MEDIUM_SURVEYBOT;
        orientation += frand(-M_PI / 18, M_PI / 18);
        SendSurveyCast(site.count, MAX_DIGSITE_FINDS, rsite->second.branch_id, false);
        return false;
    }
    if (dist_now >= ARCHAEOLOGY_DIG_SITE_FIND_DIST)
    {
        entry = GO_CLOSE_SURVEYBOT;
        SendSurveyCast(site.count, MAX_DIGSITE_FINDS, rsite->second.branch_id, false);
        return false;
    }

    if (skill_now < 50)
        UpdateSkill(SKILL_ARCHAEOLOGY, 1);

    entry = site.find_id;
    /*x = site.loot_x;
    y = site.loot_y;
    z = GetBaseMap()->GetHeight(GetPhaseMask(), x, y, GetPositionZ(), true, 5);
    if (z > INVALID_HEIGHT)
        z += 0.05f;                                     // just to be sure that we are not 
    else*/
        GetPosition(x, y, z);

    SendSurveyCast(site.count + 1, MAX_DIGSITE_FINDS, rsite->second.branch_id, true);

    if (site.count < MAX_DIGSITE_FINDS - 1)
    {
        ++site.count;
        if (!GenerateDigSiteLoot(site_id, site))
            return true;
    }
    else
    {
        site.clear();
        UseResearchSite(site_id);
    }

    _archaeologyChanged = true;

    return true;
}

// find id of research site that we are on
uint16 Player::GetResearchSiteID()
{
    ResearchPOIPoint pt;
    pt.x = int32(GetPositionX());
    pt.y = int32(GetPositionY());

    for (ResearchSiteDataMap::iterator itr = sResearchSiteDataMap.begin(); itr != sResearchSiteDataMap.end(); ++itr)
    {
        if (itr->second.entry->MapID != GetMapId())
            continue;

        if (!IsPointInZone(pt, itr->second.points))
            continue;

        return itr->second.entry->ID;
    }

    return 0;
}

bool Player::IsPointInZone(ResearchPOIPoint &test, ResearchPOIPointVector &polygon)
{
    if (polygon.size() < 3)
        return false;

    ResearchPOIPointVector::const_iterator end = polygon.end();
    ResearchPOIPoint pred_pt = polygon.back();
    pred_pt.x -= test.x;
    pred_pt.y -= test.y;

    int pred_q = q_patt[pred_pt.y < 0][pred_pt.x < 0];

    int w = 0;

    for (std::vector<ResearchPOIPoint>::const_iterator iter = polygon.begin(); iter != end; ++iter)
    {
        ResearchPOIPoint cur_pt = *iter;

        cur_pt.x -= test.x;
        cur_pt.y -= test.y;

        int q = q_patt[cur_pt.y < 0][cur_pt.x < 0];

        switch (q - pred_q)
        {
            case -3:
                ++w;
                break;
            case 3:
                --w;
                break;
            case -2:
                if (pred_pt.x * cur_pt.y >= pred_pt.y * cur_pt.x)
                    ++w;
                break;
            case 2:
                if (!(pred_pt.x * cur_pt.y >= pred_pt.y * cur_pt.x))
                    --w;
                break;
        }

        pred_pt = cur_pt;
        pred_q = q;
    }
    return w != 0;
}

void Player::UseResearchSite(uint32 id)
{
    _researchSites.erase(id);
    GenerateResearchSiteInMap(GetMapId());
}

void Player::ShowResearchSites()
{
    if (!GetSkillValue(SKILL_ARCHAEOLOGY))
        return;

    uint8 count = 0;

    for (ResearchSiteSet::const_iterator itr = _researchSites.begin(); itr != _researchSites.end(); ++itr)
    {
        uint32 id = (*itr);
        ResearchSiteEntry const* rs = GetResearchSiteEntryById(id);

        if (!rs || CanResearchWithSkillLevel(rs->ID) == 2)
            id = 0;

        SetDynamicUInt32Value(PLAYER_DYNAMIC_RESEARCH_SITES, count, id);
        ++count;
    }
}

bool Player::CanResearchWithLevel(uint32 site_id)
{
    if (!GetSkillValue(SKILL_ARCHAEOLOGY))
        return false;

    ResearchSiteDataMap::const_iterator itr = sResearchSiteDataMap.find(site_id);
    if (itr != sResearchSiteDataMap.end())
        return getLevel() + 19 >= itr->second.level;

    return true;
}

uint8 Player::CanResearchWithSkillLevel(uint32 site_id)
{
    uint16 skill_now = GetSkillValue(SKILL_ARCHAEOLOGY);
    if (!skill_now)
        return 0;

    ResearchSiteDataMap::const_iterator itr = sResearchSiteDataMap.find(site_id);
    if (itr != sResearchSiteDataMap.end())
    {
        ResearchSiteData const& entry = itr->second;

        uint16 skill_cap = 0;
        switch (entry.entry->MapID)
        {
            case 0:
                if (entry.zone == 4922)         // Twilight Hightlands
                    skill_cap = 450;
                break;
            case 1:
                if (entry.zone == 616)          // Hyjal
                    skill_cap = 450;
                else if (entry.zone == 5034)    // Uldum
                    skill_cap = 450;
                break;
            case 530:
                skill_cap = 300;                // Outland
                break;
            case 571:
                skill_cap = 375;                // Northrend
                break;
            case 870:                           // Pandaria
                skill_cap = 525;
                break;
        }

        if (skill_now >= skill_cap)
            return 1;

        if (entry.entry->MapID == 530 || entry.entry->MapID == 571 || entry.entry->MapID == 870)
            return 2;
    }

    return 0;
}

void Player::GenerateResearchSiteInMap(uint32 mapId)
{
    SiteSet tempSites;

    for (ResearchSiteDataMap::const_iterator itr = sResearchSiteDataMap.begin(); itr != sResearchSiteDataMap.end(); ++itr)
    {
        ResearchSiteEntry const* entry = itr->second.entry;
        if (!HasResearchSite(entry->ID) &&
            entry->MapID == mapId &&
            CanResearchWithLevel(entry->ID) &&
            CanResearchWithSkillLevel(entry->ID))
            tempSites.insert(entry->ID);
    }

    if (tempSites.empty())
        return;

    SiteSet::const_iterator entry = tempSites.begin();
    std::advance(entry, urand(0, tempSites.size() - 1));

    _researchSites.insert(*entry);
    _archaeologyChanged = true;

    ShowResearchSites();
}

void Player::GenerateResearchSites()
{
    _researchSites.clear();

    typedef std::map<uint32, SiteSet> Sites;
    Sites tempSites;
    for (ResearchSiteDataMap::const_iterator itr = sResearchSiteDataMap.begin(); itr != sResearchSiteDataMap.end(); ++itr)
    {
        ResearchSiteEntry const* entry = itr->second.entry;
        if (CanResearchWithLevel(entry->ID) && CanResearchWithSkillLevel(entry->ID))
            tempSites[entry->MapID].insert(entry->ID);
    }

    for (Sites::const_iterator itr = tempSites.begin(); itr != tempSites.end(); ++itr)
    {
        uint8 mapMax = std::min<int>(itr->second.size(), 4);

        for (uint8 i = 0; i < mapMax;)
        {
            SiteSet::const_iterator itr2 = itr->second.begin();
            std::advance(itr2, urand(0, itr->second.size() - 1));
            if (!HasResearchSite(*itr2))
            {
                _researchSites.insert(*itr2);
                ++i;
            }
        }
    }

    _archaeologyChanged = true;

    sLog->outInfo(LOG_FILTER_SPELLS_AURAS, "Player::GenerateResearchSites(): %u", _researchSites.size());

    ShowResearchSites();
}

float Player::GetRareArtifactChance(uint32 skill_value)
{
    return std::min<float>(sWorld->getFloatConfig(CONFIG_ARCHAEOLOGY_RARE_BASE_CHANCE) + skill_value * sWorld->getFloatConfig(CONFIG_ARCHAEOLOGY_RARE_MAXLEVEL_CHANCE) / 600.f, 100.0f);
}

void Player::GenerateResearchProjects()
{
    if (sResearchProjectSet.empty())
        return;

    uint16 skill_now = GetSkillValue(SKILL_ARCHAEOLOGY);
    if (!skill_now)
        return;

    for (uint32 i = 0; i < MAX_RESEARCH_PROJECTS / 2; ++i)
        SetUInt32Value(PLAYER_FIELD_RESEARCHING_1 + i, 0);

    typedef std::map<uint32, ResearchProjectSet> ProjectsByBranch;
    ProjectsByBranch tempProjects;
    ProjectsByBranch tempRareProjects;
    float rare_chance = GetRareArtifactChance(skill_now);

    for (std::set<ResearchProjectEntry const*>::const_iterator itr = sResearchProjectSet.begin(); itr != sResearchProjectSet.end(); ++itr)
    {
        ResearchProjectEntry const* entry = (*itr);

        if (entry->rare)
        {
            if (IsCompletedProject(entry->ID, true))
                continue;

            tempRareProjects[entry->branchId].insert(entry->ID);
        }
        else
            tempProjects[entry->branchId].insert(entry->ID);
    }

    for (ProjectsByBranch::const_iterator itr = tempProjects.begin(); itr != tempProjects.end(); ++itr)
    {
        ResearchProjectSet::const_iterator itr2;

        if (tempRareProjects[itr->first].size() > 0 && roll_chance_f(rare_chance))
        {
            itr2 = tempRareProjects[itr->first].begin();
            std::advance(itr2, urand(0, tempRareProjects[itr->first].size() - 1));
        }
        else
        {
            itr2 = itr->second.begin();
            std::advance(itr2, urand(0, itr->second.size() - 1));
        }

        ReplaceResearchProject(0, *itr2);
    }

    _archaeologyChanged = true;
}

bool Player::HasResearchProject(uint32 id) const
{
    for (uint32 i = 0; i < MAX_RESEARCH_PROJECTS; ++i)
        if (GetUInt16Value(PLAYER_FIELD_RESEARCHING_1 + i / 2, i % 2) == id)
            return true;

    return false;
}

bool Player::HasResearchProjectOfBranch(uint32 id) const
{
    for (uint32 i = 0; i < MAX_RESEARCH_PROJECTS; ++i)
        if (uint16 val = GetUInt16Value(PLAYER_FIELD_RESEARCHING_1 + i / 2, i % 2))
        {
            ResearchProjectEntry const* rp = sResearchProjectStore.LookupEntry(val);
            if (!rp)
                continue;

            if (rp->branchId == id)
                return true;
        }

    return false;
}

void Player::ReplaceResearchProject(uint32 oldId, uint32 newId)
{
    for (uint32 i = 0; i < MAX_RESEARCH_PROJECTS; ++i)
        if (GetUInt16Value(PLAYER_FIELD_RESEARCHING_1 + i / 2, i % 2) == oldId)
        {
            SetUInt16Value(PLAYER_FIELD_RESEARCHING_1 + i / 2, i % 2, newId);
            return;
        }
}

bool Player::SolveResearchProject(uint32 spellId, SpellCastTargets& targets)
{
    uint16 skill_now = GetSkillValue(SKILL_ARCHAEOLOGY);
    if (!skill_now)
        return false;

    ResearchProjectEntry const* entry = NULL;
    for (std::set<ResearchProjectEntry const*>::const_iterator itr = sResearchProjectSet.begin(); itr != sResearchProjectSet.end(); ++itr)
    {
        if ((*itr)->SpellID != spellId)
            continue;

        entry = (*itr);
        break;
    }

    if (!entry || !HasResearchProject(entry->ID))
        return false;

    ResearchBranchEntry const* branch = NULL;
    for (uint32 i = 0; i < sResearchBranchStore.GetNumRows(); ++i)
    {
        ResearchBranchEntry const* _branch = sResearchBranchStore.LookupEntry(i);
        if (!_branch)
            continue;

        if (_branch->ID != entry->branchId)
            continue;

        branch = _branch;
        break;
    }

    if (!branch)
        return false;

    uint32 currencyId = branch->CurrencyID;
    int32 currencyAmt = int32(entry->RequiredCurrencyAmount);

    ArchaeologyWeights weights = targets.GetWeights();
    for (ArchaeologyWeights::iterator itr = weights.begin(); itr != weights.end(); ++itr)
    {
        ArchaeologyWeight& w = *itr;
        if (w.type == WEIGHT_KEYSTONE)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(w.keystone.itemId);
            if (!proto)
                return false;

            if (proto->GetCurrencySubstitutionId() != currencyId)
                return false;

            if (w.keystone.itemCount > entry->Complexity)
                return false;

            if (!HasItemCount(w.keystone.itemId, w.keystone.itemCount))
                return false;

            currencyAmt -= int32(proto->CurrencySubstitutionCount * w.keystone.itemCount);
        }
    }

    if (currencyAmt > 0 && !HasCurrency(currencyId, currencyAmt))
        return false;

    ModifyCurrency(currencyId, -currencyAmt);

    for (ArchaeologyWeights::iterator itr = weights.begin(); itr != weights.end(); ++itr)
    {
        ArchaeologyWeight& w = *itr;
        if (w.type == WEIGHT_KEYSTONE)
            DestroyItemCount(w.keystone.itemId, w.keystone.itemCount, true);
    }

    UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_ARCHAEOLOGY_PROJECTS, entry->ID, 1);

    uint32 newCount = AddCompletedProject(entry);

    ResearchProjectSet tempProjects;
    ResearchProjectSet tempRareProjects;
    float rare_chance = GetRareArtifactChance(skill_now);

    for (std::set<ResearchProjectEntry const*>::const_iterator itr = sResearchProjectSet.begin(); itr != sResearchProjectSet.end(); ++itr)
    {
        ResearchProjectEntry const* project = *itr;
        if (project->branchId != entry->branchId)
            continue;

        if (project->rare)
        {
            if (IsCompletedProject(project->ID, true))
                continue;

            tempRareProjects.insert(project->ID);
        }
        else
            tempProjects.insert(project->ID);
    }

    ResearchProjectSet::const_iterator itr;
    if (tempRareProjects.size() > 0 && roll_chance_f(rare_chance))
    {
        itr = tempRareProjects.begin();
        std::advance(itr, urand(0, tempRareProjects.size() - 1));
    }
    else
    {
        itr = tempProjects.begin();
        std::advance(itr, urand(0, tempProjects.size() - 1));
    }

    ReplaceResearchProject(entry->ID, *itr);

    _archaeologyChanged = true;

    WorldPacket data (SMSG_RESEARCH_COMPLETE, 4 * 3);
    data << uint32(time(NULL));
    data << uint32(newCount);
    data << uint32(entry->ID);
    SendDirectMessage(&data);

    return true;
}

uint32 Player::AddCompletedProject(ResearchProjectEntry const* entry)
{
    for (CompletedProjectList::iterator itr = _completedProjects.begin(); itr != _completedProjects.end(); ++itr)
        if (itr->entry->ID == entry->ID)
            return ++itr->count;

    _completedProjects.push_back(CompletedProject(entry));
    return 1;
}

bool Player::IsCompletedProject(uint32 id, bool onlyRare)
{
    for (CompletedProjectList::const_iterator itr = _completedProjects.begin(); itr != _completedProjects.end(); ++itr)
        if (id == itr->entry->ID && (!onlyRare || itr->entry->rare))
            return true;

    return false;
}

void Player::_SaveArchaeology(SQLTransaction& trans)
{
    if (!sWorld->getBoolConfig(CONFIG_ARCHAEOLOGY_ENABLED))
        return;

    if (!GetSkillValue(SKILL_ARCHAEOLOGY))
        return;

    if (!_archaeologyChanged)
        return;

    uint8 index = 0;
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_PLAYER_ARCHAEOLOGY);
    stmt->setInt32(index++, GetGUIDLow());
    std::stringstream ss;
    for (ResearchSiteSet::const_iterator itr = _researchSites.begin(); itr != _researchSites.end(); ++itr)
        ss << (*itr) << " ";
    stmt->setString(index++, ss.str());
    ss.str("");
    for (uint8 j = 0; j < MAX_RESEARCH_SITES; ++j)
        ss << uint32(_digSites[j].count) << " ";
    stmt->setString(index++, ss.str());
    ss.str("");
    for (uint32 i = 0; i < MAX_RESEARCH_PROJECTS; ++i)
        if (uint16 val = GetUInt16Value(PLAYER_FIELD_RESEARCHING_1 + i / 2, i % 2))
            ss << val << " ";
    stmt->setString(index++, ss.str());
    trans->Append(stmt);

    for (CompletedProjectList::iterator itr = _completedProjects.begin(); itr != _completedProjects.end(); ++itr)
    {
        index = 0;
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_PLAYER_ARCHAEOLOGY_FINDS);
        stmt->setUInt32(index++, GetGUIDLow());
        stmt->setUInt32(index++, itr->entry->ID);
        stmt->setUInt32(index++, itr->count);
        stmt->setUInt32(index++, itr->date);
        trans->Append(stmt);
    }

    _archaeologyChanged = false;
}

void Player::_LoadArchaeology(PreparedQueryResult result)
{
    for (uint8 i = 0; i < MAX_RESEARCH_SITES; ++i)
        _digSites[i].count = 0;

    if (!sWorld->getBoolConfig(CONFIG_ARCHAEOLOGY_ENABLED))
        return;

    if (!result)
    {
        GenerateResearchSites();
        GenerateResearchProjects();
        return;
    }

    Field* fields = result->Fetch();

    // Loading current zones
    Tokenizer tokens(fields[0].GetString(), ' ');
    if (tokens.size() == MAX_RESEARCH_SITES)
    {
        _researchSites.clear();

        for (uint8 i = 0; i < tokens.size(); ++i)
            _researchSites.insert(uint32(atoi(tokens[i])));
    }
    else
        GenerateResearchSites();

    // Loading current zone info
    Tokenizer tokens2(fields[1].GetString(), ' ');
    if (tokens2.size() == MAX_RESEARCH_SITES)
    {
        for (uint8 i = 0; i < MAX_RESEARCH_SITES; ++i)
            _digSites[i].count = uint32(atoi(tokens2[i]));
    }

    // Loading current projects
    Tokenizer tokens3(fields[2].GetString(), ' ');
    if (tokens.size() == MAX_RESEARCH_PROJECTS)
    {
        for (uint8 i = 0; i < MAX_RESEARCH_PROJECTS; ++i)
            if (ResearchProjectEntry const* entry = sResearchProjectStore.LookupEntry(atoi(tokens3[i])))
                if (entry->IsVaid())
                    ReplaceResearchProject(0, entry->ID);
    }
    else
        GenerateResearchProjects();
}

void Player::_LoadArchaeologyFinds(PreparedQueryResult result)
{
    // "SELECT id, count, UNIX_TIMESTAMP(date) FROM character_archaeology_finds WHERE guid = %u"
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();

        uint32 id = fields[0].GetUInt32();
        uint32 count = fields[1].GetUInt32();
        uint32 date = fields[2].GetUInt32();

        ResearchProjectEntry const* rs = sResearchProjectStore.LookupEntry(id);
        if (!rs)
        {
            _archaeologyChanged = true;
            continue;
        }

        CompletedProject cp;
        cp.entry = rs;
        cp.count = count;
        cp.date = date;

        _completedProjects.push_back(cp);
    }
    while (result->NextRow());
}

void Player::SendCompletedProjects()
{
    if (!HasSkill(SKILL_ARCHAEOLOGY))
        return;

    WorldPacket data(SMSG_RESEARCH_SETUP_HISTORY, 3 + _completedProjects.size() * 3 * 4);

    data.WriteBits(_completedProjects.size(), 20);

    for (CompletedProjectList::iterator itr = _completedProjects.begin(); itr != _completedProjects.end(); ++itr)
    {
        data << uint32(itr->entry->ID);
        data << uint32(itr->date);
        data << uint32(itr->count);
    }

    SendDirectMessage(&data);
}

void WorldSession::HandleRequestResearchHistory(WorldPacket& recv_data)
{
    // null opcode
    sLog->outDebug(LOG_FILTER_NETWORKIO, "World: received CMSG_REQUEST_RESEARCH_HISTORY from %s (account %u)", GetPlayerName(), GetAccountId());

    _player->SendCompletedProjects();
}

void Player::SendSurveyCast(uint32 count, uint32 max, uint32 fieldId, bool completed)
{
    WorldPacket data(SMSG_SURVEY_CAST, 4 * 3 + 1);
    data << uint32(count) << uint32(max) << uint32(fieldId);
    data.WriteBit(completed);
    SendDirectMessage(&data);
}