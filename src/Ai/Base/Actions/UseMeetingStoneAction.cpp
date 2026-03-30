/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "UseMeetingStoneAction.h"

#include "CellImpl.h"
#include "Event.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "NearestGameObjects.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PositionValue.h"

bool UseMeetingStoneAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    WorldPacket p(event.getPacket());
    p.rpos(0);
    ObjectGuid guid;
    p >> guid;

    if (master->GetTarget() && master->GetTarget() != bot->GetGUID())
        return false;

    if (!master->GetTarget() && master->GetGroup() != bot->GetGroup())
        return false;

    if (master->IsBeingTeleported())
        return false;

    if (bot->IsInCombat())
    {
        botAI->TellError("我正在战斗中");
        return false;
    }

    Map* map = master->GetMap();
    if (!map)
        return false;

    GameObject* gameObject = map->GetGameObject(guid);
    if (!gameObject)
        return false;

    GameObjectTemplate const* goInfo = gameObject->GetGOInfo();
    if (!goInfo || goInfo->entry != 179944)
        return false;

    return Teleport(master, bot, false);
}

bool SummonAction::Execute(Event /*event*/)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    if (Pet* pet = bot->GetPet())
        botAI->PetFollow();

    if (master->GetSession()->GetSecurity() >= SEC_PLAYER)
    {
        // botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set({});
        AI_VALUE(std::list<FleeInfo>&, "recently flee info").clear();
        return Teleport(master, bot, true);
    }

    if (SummonUsingGos(master, bot, true) || SummonUsingNpcs(master, bot, true))
    {
        botAI->TellMasterNoFacing("你好啊!");
        return true;
    }

    if (SummonUsingGos(bot, master, true) || SummonUsingNpcs(bot, master, true))
    {
        botAI->TellMasterNoFacing("欢迎!");
        return true;
    }

    return false;
}

bool SummonAction::SummonUsingGos(Player* summoner, Player* player, bool preserveAuras)
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(summoner, sPlayerbotAIConfig.sightDistance);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(summoner, targets, u_check);
    Cell::VisitObjects(summoner, searcher, sPlayerbotAIConfig.sightDistance);

    for (GameObject* go : targets)
    {
        if (go->isSpawned() && go->GetGoType() == GAMEOBJECT_TYPE_MEETINGSTONE)
            return Teleport(summoner, player, preserveAuras);
    }

    botAI->TellError(summoner == bot ? "附近没有集合石" : "你的附近没有集合石");
    return false;
}

bool SummonAction::SummonUsingNpcs(Player* summoner, Player* player, bool preserveAuras)
{
    if (!sPlayerbotAIConfig.summonAtInnkeepersEnabled)
        return false;

    std::list<Unit*> targets;
    Acore::AnyUnitInObjectRangeCheck u_check(summoner, sPlayerbotAIConfig.sightDistance);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(summoner, targets, u_check);
    Cell::VisitObjects(summoner, searcher, sPlayerbotAIConfig.sightDistance);

    for (Unit* unit : targets)
    {
        if (unit && unit->HasNpcFlag(UNIT_NPC_FLAG_INNKEEPER))
        {
            if (!player->HasItemCount(6948, 1, false))
            {
                botAI->TellError(player == bot ? "我没有炉石" : "你没有炉石");
                return false;
            }

            if (player->HasSpellCooldown(8690))
            {
                botAI->TellError(player == bot ? "我的炉石还没有准备好" : "你的炉石还没有准备好");
                return false;
            }

            // Trigger cooldown
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(8690);
            if (!spellInfo)
                return false;

            Spell spell(player, spellInfo, TRIGGERED_NONE);
            spell.SendSpellCooldown();

            return Teleport(summoner, player, preserveAuras);
        }
    }

    botAI->TellError(summoner == bot ? "附近没有旅店老板" : "你的附近没有旅店老板");
    return false;
}

bool SummonAction::Teleport(Player* summoner, Player* player, bool preserveAuras)
{
    // Player* master = GetMaster();
    if (!summoner || summoner == player)
        return false;

    if (player->GetVehicle())
    {
        botAI->TellError("你不能召唤正在载具上的我");
        return false;
    }

    if (!summoner->IsBeingTeleported() && !player->IsBeingTeleported())
    {
        float followAngle = GetFollowAngle();
        for (float angle = followAngle - M_PI; angle <= followAngle + M_PI; angle += M_PI / 4)
        {
            uint32 mapId = summoner->GetMapId();
            float x = summoner->GetPositionX() + cos(angle) * sPlayerbotAIConfig.followDistance;
            float y = summoner->GetPositionY() + sin(angle) * sPlayerbotAIConfig.followDistance;
            float z = summoner->GetPositionZ();

            if (summoner->IsWithinLOS(x, y, z))
            {
                if (sPlayerbotAIConfig.botRepairWhenSummon)  // .conf option to repair bot gear when summoned 0 = off, 1 = on
                    bot->DurabilityRepairAll(false, 1.0f, false);

                if (summoner->IsInCombat() && !sPlayerbotAIConfig.allowSummonInCombat)
                {
                    botAI->TellError("你无法在战斗状态中召唤我");
                    return false;
                }

                if (!summoner->IsAlive() && !sPlayerbotAIConfig.allowSummonWhenMasterIsDead)
                {
                    botAI->TellError("你无法在死亡状态下召唤我");
                    return false;
                }

                if (bot->isDead() && !bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) &&
                    !sPlayerbotAIConfig.allowSummonWhenBotIsDead)
                {
                    botAI->TellError("你无法召唤已死亡的我，请先释放我的灵魂");
                    return false;
                }

                bool revive =
                    sPlayerbotAIConfig.reviveBotWhenSummoned == 2 ||
                    (sPlayerbotAIConfig.reviveBotWhenSummoned == 1 && !summoner->IsInCombat() && summoner->IsAlive());

                if (bot->isDead() && revive)
                {
                    bot->ResurrectPlayer(1.0f, false);
                    bot->SpawnCorpseBones();
                    botAI->TellMasterNoFacing("我复活了!");
                    botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Reset();
                }

                player->GetMotionMaster()->Clear();
                AI_VALUE(LastMovement&, "last movement").clear();

                if (!preserveAuras)
                    player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED |
                                                          AURA_INTERRUPT_FLAG_CHANGE_MAP);
                player->TeleportTo(mapId, x, y, z, 0);
                if (player->GetPet())
                    player->GetPet()->NearTeleportTo(x, y, z, player->GetOrientation());
                if (player->GetGuardianPet())
                    player->GetGuardianPet()->NearTeleportTo(x, y, z, player->GetOrientation());
                if (botAI->HasStrategy("stay", botAI->GetState()))
                {
                    PositionMap& posMap = AI_VALUE(PositionMap&, "position");
                    PositionInfo stayPosition = posMap["stay"];

                    stayPosition.Set(x,y, z, mapId);
                    posMap["stay"] = stayPosition;
                }

                return true;
            }
        }
    }

    if (summoner != player)
         botAI->TellError("没有足够的空间进行召唤");
    return false;
}
