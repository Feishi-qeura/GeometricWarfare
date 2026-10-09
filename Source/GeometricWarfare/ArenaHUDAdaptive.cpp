#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "UI/ArenaIcons.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"

namespace {
const FLinearColor Ink(.035f,.065f,.08f,1),Muted(.30f,.36f,.38f,1),Paper(.94f,.95f,.92f,1),Panel(.985f,.99f,.975f,1),Gold(.75f,.44f,.08f,1);
FLinearColor Camp(int32 Team){return Team==1?FLinearColor(.78f,.09f,.075f,1):Team==2?FLinearColor(.045f,.26f,.72f,1):FLinearColor(.34f,.40f,.41f,1);}
gwui::Icon Weapon(gw::WeaponKind K){switch(K){case gw::WeaponKind::Shotgun:return gwui::Icon::Shotgun;case gw::WeaponKind::Rifle:return gwui::Icon::Rifle;case gw::WeaponKind::Sniper:return gwui::Icon::Sniper;case gw::WeaponKind::MachineGun:return gwui::Icon::MachineGun;case gw::WeaponKind::RocketLauncher:return gwui::Icon::RocketLauncher;default:return gwui::Icon::Pistol;}}
}

void AArenaHUD::DrawAdaptiveRanks(AArenaGameMode* G,float X,float Y,float Width,float AvailableHeight) {
    const auto& M=G->GetMatch();const bool Focused=M.findFighter(G->FocusBodyId)!=nullptr;
    const int64 ScoreMax=M.leaderboard.empty()?1:FMath::Max<int64>(1,M.fighters[M.leaderboard.front()].score);
    const int32 ScoreDigits=FString::Printf(TEXT("%lld"),static_cast<long long>(ScoreMax)).Len();
    if(ScreenLayout.portrait||ViewportWidth/ViewportHeight<1.55f){
        const float U=1.f/FMath::Min(FMath::Max(ScreenLayout.scale,.1f),1.f),RowHeight=22*U,Top=Y+18*U;
        const int32 Font=FMath::CeilToInt(10*U),Title=FMath::CeilToInt(11*U);
        const float ScoreWidth=FMath::Max(88*U,(ScoreDigits*8+12)*U),ScoreX=X+Width-ScoreWidth-3*U;
        const float StatusX=ScoreX-36*U,WeaponX=StatusX-17*U;
        RankVisibleRows=FMath::Clamp(FMath::FloorToInt((AvailableHeight-18*U-(Focused?22*U:0))/RowHeight),1,2);
        RankTotalRows=FMath::Min(20,static_cast<int32>(M.leaderboard.size()));
        RankOffset=FMath::Clamp(RankOffset,0,FMath::Max(0,RankTotalRows-RankVisibleRows));
        Text(FString::Printf(TEXT("本局积分榜 · %d 人"),G->GetViewers().Num()),X,Y,Title,Ink);
        Text(FString::Printf(TEXT("%d/%d · 滚轮翻页"),RankTotalRows>0?RankOffset+1:0,RankTotalRows),X+Width-112*U,Y+U,Font,Muted);
        RankBounds=FBox2D({X,Top},{X+Width,Top+RankVisibleRows*RowHeight});
        for(int32 Row=0;Row<FMath::Min(RankVisibleRows,RankTotalRows-RankOffset);++Row){
            const int32 Index=RankOffset+Row;const auto& F=M.fighters[M.leaderboard[Index]];
            const float RY=Top+Row*RowHeight;RankRegions.Add({FBox2D({X,RY},{X+Width,RY+RowHeight}),F.id});
            Rect(X,RY,Width,RowHeight-U,FMath::Lerp(Panel,F.id==G->FocusBodyId?Camp(F.team):Muted,.08f));
            Text(FString::Printf(TEXT("%02d"),Index+1),X+3*U,RY+3*U,Font,Muted);
            if(const auto* V=G->FindViewer(F.id))Portrait(V->Avatar,X+22*U,RY+2*U,18*U);
            Rect(X+44*U,RY+3*U,2*U,16*U,Camp(F.team));
            Text(FitText(G->ViewerName(F.id),Font,FMath::Max(0.f,WeaponX-X-63*U)),X+51*U,RY+3*U,Font,F.alive?Ink:Muted);
            gwui::DrawIcon(Canvas,Weapon(F.weaponKind),{WeaponX,RY+11*U},16*U,Ink);
            Text(F.alive?TEXT("存活"):TEXT("阵亡"),StatusX,RY+3*U,Font,F.alive?Muted:Camp(1));
            ScoreBar(ScoreX,RY+2*U,ScoreWidth,18*U,F.score,ScoreMax,Camp(F.team),Font);
        }
        if(const auto* F=M.findFighter(G->FocusBodyId)){
            const float FY=Top+RankVisibleRows*RowHeight;
            Rect(X,FY,Width,22*U,Panel);
            Text(FitText(TEXT("锁定 · ")+G->ViewerName(F->id),Font,Width-212*U),X+4*U,FY+3*U,Font,Ink);
            Text(F->alive?TEXT("存活"):TEXT("等待复活"),X+Width-195*U,FY+3*U,Font,Muted);
            HealthBar(X+Width-130*U,FY+3*U,126*U,16*U,F->hp,F->maxHp,Camp(F->team),Font);
        }
        return;
    }
    Text(TEXT("本局积分榜"),X,Y,18,Ink);Text(TEXT("点击锁定 · 滚轮翻页"),X,Y+25,10,Muted);
    const bool CompactFocus=ViewportHeight<620 || AvailableHeight<313;
    const float ScoreU=1.f/FMath::Min(FMath::Max(ScreenLayout.scale,.1f),1.f),DesiredScoreWidth=FMath::Max(76*ScoreU,(ScoreDigits*8+12)*ScoreU);
    const bool ScoreSecondLine=true;
    const int32 ScoreFont=FMath::CeilToInt(10*ScoreU);
    const float RowHeight=ScoreSecondLine?32+22*ScoreU:FMath::Max(32.f,24*ScoreU),Top=Y+45,Reserve=Focused?(CompactFocus?92.f:236.f):64.f;
    RankVisibleRows=FMath::Clamp(FMath::FloorToInt((AvailableHeight-45-Reserve)/RowHeight),1,20);
    RankTotalRows=FMath::Min(20,static_cast<int32>(M.leaderboard.size()));
    RankOffset=FMath::Clamp(RankOffset,0,FMath::Max(0,RankTotalRows-RankVisibleRows));
    RankBounds=FBox2D({X,Top},{X+Width,Top+RankVisibleRows*RowHeight});
    float MX=-1,MY=-1;if(GetOwningPlayerController())GetOwningPlayerController()->GetMousePosition(MX,MY);ToLayoutPointer(MX,MY);
    int32 Hovered=-1;float HoveredY=Top;
    const float ScoreWidth=ScoreSecondLine?Width-12:DesiredScoreWidth,ScoreX=ScoreSecondLine?X+6:X+Width-ScoreWidth-4;
    const float StatusX=ScoreSecondLine?X+Width-54:ScoreX-34,WeaponX=StatusX-42;
    for(int32 Row=0;Row<FMath::Min(RankVisibleRows,RankTotalRows-RankOffset);++Row){
        const int32 Index=RankOffset+Row,FighterIndex=M.leaderboard[Index];const auto& F=M.fighters[FighterIndex];
        const float RY=Top+Row*RowHeight;const FBox2D B({X,RY},{X+Width,RY+RowHeight-1});RankRegions.Add({B,F.id});
        const bool Selected=F.id==G->FocusBodyId,Hover=B.IsInside({MX,MY});
        Rect(X,RY,Width,RowHeight-1,FMath::Lerp(Panel,Selected?Camp(F.team):Muted,Selected?.15f:Hover?.12f:Index%2?.03f:.06f));
        Text(FString::Printf(TEXT("%02d"),Index+1),X+6,RY+6,12,Muted);
        if(const auto* V=G->FindViewer(F.id))Portrait(V->Avatar,X+30,RY+5,22);
        Rect(X+57,RY+6,3,20,Camp(F.team));
        Text(FitText(G->ViewerName(F.id),12,FMath::Max(0.f,WeaponX-X-82)),X+68,RY+5,12,F.alive?Ink:Muted);
        gwui::DrawIcon(Canvas,Weapon(F.weaponKind),{WeaponX,RY+15},20,F.alive?Ink:Muted);
        if(F.temporaryWeaponRemaining>0)gwui::DrawIcon(Canvas,Weapon(F.temporaryWeaponKind),{WeaponX+20,RY+15},14,Gold);
        DrawStatusAura({X+41,RY+16},7,F.alive&&F.evolutionRemaining>0,F.alive&&M.hasBossBuff(F),G->RunningTime,false,F.alive&&F.heroBuff);
        Text(F.alive?TEXT("存活"):TEXT("阵亡"),StatusX,RY+7,11,F.alive?FLinearColor(.23f,.47f,.33f,1):Camp(1));
        ScoreBar(ScoreX,ScoreSecondLine?RY+32:RY+(RowHeight-18*ScoreU)/2,ScoreWidth,18*ScoreU,F.score,ScoreMax,Camp(F.team),ScoreFont);
        if(Hover){Hovered=FighterIndex;HoveredY=RY;}
    }
    const float Bottom=Top+RankVisibleRows*RowHeight;
    Text(RankTotalRows>RankVisibleRows?FString::Printf(TEXT("%02d–%02d / %02d · 滚轮翻页"),RankOffset+1,FMath::Min(RankOffset+RankVisibleRows,RankTotalRows),RankTotalRows):TEXT("点击角色或榜单查看详情"),X,Bottom+5,10,Muted);
    if(Focused)DrawFocusCard(G,X+6,Bottom+27,Width,CompactFocus);
    else Text(TEXT("拖动战场平移 · 滚轮缩放 · 右键解除跟随"),X,Bottom+28,10,Muted);
    if(Hovered>=0 && !IsOverlayOpen())DrawRankTooltip(G,Hovered,ScreenLayout.portrait?X+Width-262:X-270,HoveredY,262);
}

void AArenaHUD::DrawAdaptiveHUD(AArenaGameMode* G) {
    const auto& M=G->GetMatch();const float W=ViewportWidth,H=ViewportHeight;
    const bool Vertical=ScreenLayout.portrait||W/H<1.55f;const float U=1.f/FMath::Min(FMath::Max(ScreenLayout.scale,.1f),1.f);
    Rect(0,0,W,H,Paper);
    Text(TEXT("几何战争"),16,17,24,Ink);
    const int32 Seconds=FMath::Max(0,FMath::CeilToInt(M.config.battleSeconds-M.elapsed));
    const float FieldWidth=Vertical?W-32:W-320;
    const float GiftWidth=Vertical?FieldWidth:FMath::Min(400.f/FMath::Max(ScreenLayout.scale,.1f),FieldWidth*.62f);
    const float GiftHeight=DrawGiftRules(G,0,0,GiftWidth,Vertical,true);
    const float PlayWidth=Vertical?FieldWidth:FieldWidth-GiftWidth-16;
    const float StickerHeight=DrawRecruitSticker(0,0,PlayWidth,true),StickerY=H-64-StickerHeight;
    const float GiftY=Vertical?StickerY-12-GiftHeight:148.f;
    const float Center=Vertical?W/2:16+FieldWidth/2;
    Text(FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60),Center,14,29,Ink,true);
    Text(M.phase==gw::Phase::Results?TEXT("本局已结束"):M.phase==gw::Phase::Sprint?TEXT("最后冲刺 · 禁止重建"):TEXT("夺分战 · 实时加入"),Center,54,11,M.phase==gw::Phase::Sprint?Gold:Muted,true);
    const float TeamWidth=FMath::Min(320.f*U,FieldWidth*.43f);
    const float ScoreHeight=24.f/FMath::Max(ScreenLayout.scale,.1f),ScoreY=86.f,BaseY=ScoreY+ScoreHeight+4;
    const int32 ScoreFont=FMath::Max(14,FMath::CeilToInt(12.f/FMath::Max(ScreenLayout.scale,.1f)));
    const int64 ScoreMaximum=FMath::Max<int64>(1,FMath::Max<int64>(M.teamScores[1],M.teamScores[2]));
    for(int32 T=1;T<=2;++T){
        const float X=T==1?16:16+FieldWidth-TeamWidth;const auto C=Camp(T);
        Text(T==1?TEXT("红方 · 积分"):TEXT("蓝方 · 积分"),X,67,14,C);
        Text(FString::Printf(TEXT("%d / %d"),M.teamCounts[T],gw::Match::TeamCapacity(T)),X+TeamWidth-76,69,10,Muted);
        ScoreBar(X,ScoreY,TeamWidth,ScoreHeight,M.teamScores[T],ScoreMaximum,C,ScoreFont);
        HealthBar(X,BaseY,TeamWidth,12,M.bases[T].hp,M.bases[T].maxHp,C,8);
        if(M.teamBuffRemaining[T]>0)Text(FString::Printf(TEXT("BOSS 伤害/得分+20%% · %.0fs"),M.teamBuffRemaining[T]),X,BaseY+15,9,Gold);
    }
    const float TeamInfoBottom=BaseY+((M.teamBuffRemaining[1]>0||M.teamBuffRemaining[2]>0)?30.f:12.f);
    const float BossY=FMath::Max(145.f,TeamInfoBottom+22.f);
    ArenaY=M.boss.active?FMath::Max(164.f,BossY+21.f):FMath::Max(164.f,TeamInfoBottom+6.f);
    // The rules determine their own full height; the field fills every remaining pixel.
    const float RankReserve=(M.findFighter(G->FocusBodyId)?62.f:40.f)*U;
    ArenaWidth=PlayWidth;
    ArenaHeight=FMath::Max(120.f,Vertical?GiftY-ArenaY-RankReserve-12*U:StickerY-ArenaY-12);
    ArenaSide=FMath::Min(ArenaWidth,ArenaHeight);
    ArenaX=Vertical?16.f:32+GiftWidth;
    SetupArenaView(G);
    DrawBossBar(G,BossY);DrawArena(G);
    if(M.phase==gw::Phase::Results)DrawResults(G);else DrawMinimap(G);
    DrawGiftNotice(G);
    const float FieldBottom=ArenaY+ArenaHeight;
    if(Vertical){
        float RankWidth=W-32;
        if(RuleSticker && RuleSticker->GetResource() && GiftY-FieldBottom>260 && RankReserve>260){
            const float StickerH=FMath::Min(224.f,GiftY-FieldBottom-44),StickerW=StickerH*168.f/306;
            const float SX=W-16-StickerW,SY=GiftY-12-StickerH;
            FCanvasTileItem Item({SX,SY},RuleSticker->GetResource(),{StickerW,StickerH},FLinearColor::White);Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
            RankWidth-=StickerW+12;
        }
        DrawAdaptiveRanks(G,16,FieldBottom+6*U,RankWidth,RankReserve);
    }else{
        const float Right=W-296;
        Text(FString::Printf(TEXT("%d / %d 人"),G->GetViewers().Num(),gw::Match::ViewerCapacity),Right,62,19,Ink);
        Text(FString::Printf(TEXT("灰色阵营 %d / %d"),M.teamCounts[0],gw::Match::TeamCapacity(0)),Right,97,12,Muted);
        DrawAdaptiveRanks(G,Right,139,280,H-196);
    }
    DrawGiftRules(G,16,GiftY,GiftWidth,Vertical);
    DrawRecruitSticker(Vertical?16.f:32+GiftWidth,StickerY,PlayWidth);
    Line({16,H-55},{W-16,H-55},FLinearColor(.71f,.76f,.74f,1));
    Text(G->GetBridge()->IsLocalTestMode()?TEXT("加入 · 1/2 入队 · 队内数字换形 · 武器1–6"):TEXT("加入 · 1/2 入队 · y/z/c/s 换形 · 武器1–6"),16,H-46,11,Muted);
    Text(FitText(G->LastEvent,10,W-32),16,H-23,10,Muted);
}
