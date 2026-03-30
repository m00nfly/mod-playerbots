/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "SendMailAction.h"

#include "ChatHelper.h"
#include "Event.h"
#include "ItemVisitors.h"
#include "Mail.h"
#include "Playerbots.h"

bool SendMailAction::Execute(Event event)
{
    uint32 account = bot->GetSession()->GetAccountId();
    bool randomBot = sPlayerbotAIConfig.IsInRandomAccountList(account);

    GuidVector gos = *context->GetValue<GuidVector>("nearest game objects");
    bool mailboxFound = false;
    for (ObjectGuid const guid : gos)
    {
        if (GameObject* go = botAI->GetGameObject(guid))
            if (go->GetGoType() == GAMEOBJECT_TYPE_MAILBOX)
            {
                mailboxFound = true;
                break;
            }
    }

    std::string const text = event.getParam();

    Player* receiver = GetMaster();
    Player* tellTo = receiver;

    std::vector<std::string> ss = split(text, ' ');
    if (ss.size() > 1)
    {
        if (Player* p = ObjectAccessor::FindPlayer(ObjectGuid(uint64(ss[ss.size() - 1].c_str()))))
            receiver = p;
    }

    if (!receiver)
        receiver = event.getOwner();

    if (!receiver || receiver == bot)
    {
        return false;
    }

    if (!tellTo)
        tellTo = receiver;

    if (!mailboxFound && !randomBot)
    {
        bot->Whisper("附近没有邮箱", LANG_UNIVERSAL, tellTo);
        return false;
    }

    ItemIds ids = chat->parseItems(text);
    if (ids.size() > 1)
    {
        bot->Whisper("你一次只能选择一件物品", LANG_UNIVERSAL, tellTo);
        return false;
    }

    if (ids.empty())
    {
        uint32 money = chat->parseMoney(text);
        if (!money)
            return false;

        if (randomBot)
        {
            bot->Whisper("我不能邮寄金钱", LANG_UNIVERSAL, tellTo);
            return false;
        }

        if (bot->GetMoney() < money)
        {
            botAI->TellError("我没有足够多的金钱");
            return false;
        }

        std::ostringstream body;
        body << "你好呀, " << receiver->GetName() << ",\n";
        body << "\n";
        body << "以下是给你的金钱，请注意查收";
        body << "\n";
        body << "祝你好运,\n";
        body << bot->GetName() << "\n";

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        MailDraft draft("你要的金钱", body.str());
        draft.AddMoney(money);
        bot->SetMoney(bot->GetMoney() - money);
        draft.SendMailTo(trans, MailReceiver(receiver), MailSender(bot));

        CharacterDatabase.CommitTransaction(trans);

        std::ostringstream out;
        out << "已发送邮件给 " << receiver->GetName();
        botAI->TellMaster(out.str());
        return true;
    }

    std::ostringstream body;
    body << "你好, " << receiver->GetName() << ",\n";
    body << "\n";
    body << "以下是你需要的物品";
    body << "\n";
    body << "祝好运,\n";
    body << bot->GetName() << "\n";

    MailDraft draft("你需要的物品", body.str());
    for (ItemIds::iterator i = ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);

        std::vector<Item*> items = visitor.GetResult();
        for (Item* item : items)
        {
            if (item->IsSoulBound() || item->IsConjuredConsumable())
            {
                std::ostringstream out;
                out << "无法发送 " << ChatHelper::FormatItem(item->GetTemplate());
                bot->Whisper(out.str(), LANG_UNIVERSAL, tellTo);
                continue;
            }

            ItemTemplate const* proto = item->GetTemplate();
            if (!proto)
                continue;

            if (randomBot)
            {
                uint32 price = item->GetCount() * proto->SellPrice;
                if (!price)
                {
                    std::ostringstream out;
                    out << ChatHelper::FormatItem(item->GetTemplate()) << ": 不能被邮寄";
                    bot->Whisper(out.str(), LANG_UNIVERSAL, tellTo);
                    return false;
                }

                draft.AddCOD(price);
            }

            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

            bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
            item->DeleteFromInventoryDB(trans);
            item->SetOwnerGUID(receiver->GetGUID());
            item->SaveToDB(trans);
            draft.AddItem(item);
            draft.SendMailTo(trans, MailReceiver(receiver), MailSender(bot));

            CharacterDatabase.CommitTransaction(trans);

            std::ostringstream out;
            out << "已发送邮件给 " << receiver->GetName();
            bot->Whisper(out.str(), LANG_UNIVERSAL, tellTo);
            return true;
        }
    }

    return false;
}
