/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "HireAction.h"

#include "Event.h"
#include "RandomPlayerbotMgr.h"
#include "PlayerbotAI.h"

bool HireAction::Execute(Event /*event*/)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    if (!RandomPlayerbotMgr::instance().IsRandomBot(bot))
        return false;

    uint32 account = master->GetSession()->GetAccountId();
    QueryResult results = CharacterDatabase.Query("SELECT COUNT(*) FROM characters WHERE account = {}", account);

    uint32 charCount = 10;
    if (results)
    {
        Field* fields = results->Fetch();
        charCount = uint32(fields[0].Get<uint64>());
    }

    if (charCount >= 10)
    {
        botAI->TellMaster("你拥有的角色数量已达上限");
        return false;
    }

    if (bot->GetLevel() > master->GetLevel())
    {
        botAI->TellMaster("你不能雇佣比你等级更高的角色");
        return false;
    }

    uint32 discount = RandomPlayerbotMgr::instance().GetTradeDiscount(bot, master);
    uint32 m = 1 + (bot->GetLevel() / 10);
    uint32 moneyReq = m * 5000 * bot->GetLevel();
    if (discount < moneyReq)
    {
        std::ostringstream out;
        out << "您不能雇佣我 - 我几乎不认识你. 请确保您至少拥有 " << chat->formatMoney(moneyReq)
            << " 用于交易抵扣.";
        botAI->TellMaster(out.str());
        return false;
    }

    botAI->TellMaster("我会在你下次重新登录时加入你.");

    bot->SetMoney(moneyReq);
    RandomPlayerbotMgr::instance().Remove(bot);
    CharacterDatabase.Execute("UPDATE characters SET account = {} WHERE guid = {}", account,
                              bot->GetGUID().GetCounter());

    return true;
}
