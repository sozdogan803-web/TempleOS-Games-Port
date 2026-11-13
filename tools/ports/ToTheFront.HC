//Native port of ToTheFront: TempleOS compiles the AI file a player picks
//(ExeFile) and then points the player's slots at it (ExePrint).  Here both AIs
//are translated with the game, and PlayerPick points the slots directly.
//Functions defined in this file replace the game's ones of the same name.

#include "::/Apps/ToTheFront/AIs/Human.HC"
U0 TosHumanIndirect() {PlayerIndirect;}
U0 TosHumanMove()     {PlayerMove;}
U0 TosHumanDirect()   {PlayerDirect;}

#include "::/Apps/ToTheFront/AIs/SimpleAI.HC"
U0 TosSimpleIndirect() {PlayerIndirect;}
U0 TosSimpleMove()     {PlayerMove;}
U0 TosSimpleDirect()   {PlayerDirect;}

U0 PlayerPick(U8 *dirname,I64 player)
{
  I64 i;
  CDoc *doc=DocNew;
  no_warn dirname;
  DocPrint(doc,"Player %d Type\n\n$$LTBLUE$$\n",player+1);
  DocPrint(doc,"$$MU-UL,\"%-10ts\",LE=1$$ ","Human");
  DocPrint(doc,"$$MU-UL,\"%-10ts\",LE=2$$ ","SimpleAI");
  DocPrint(doc,"\n\n\n");
  while ((i=PopUpMenu(doc))<=0);
  DocDel(doc);
  if (i==1) {
    player_indirect[player]=&TosHumanIndirect;
    player_move[player]=&TosHumanMove;
    player_direct[player]=&TosHumanDirect;
  } else {
    player_indirect[player]=&TosSimpleIndirect;
    player_move[player]=&TosSimpleMove;
    player_direct[player]=&TosSimpleDirect;
  }
}
