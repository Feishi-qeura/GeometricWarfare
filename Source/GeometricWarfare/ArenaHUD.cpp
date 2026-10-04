#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "UI/ArenaIcons.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Styling/CoreStyle.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Brushes/SlateColorBrush.h"

namespace {
const FLinearColor Ink(.035f,.065f,.08f,1),Muted(.30f,.36f,.38f,1),Border(.71f,.76f,.74f,1);
const FLinearColor Paper(.94f,.95f,.92f,1),Panel(.985f,.99f,.975f,1),Gold(.75f,.44f,.08f,1);
FLinearColor TeamColor(int32 T) { return T==1?FLinearColor(.78f,.09f,.075f,1):(T==2?FLinearColor(.045f,.26f,.72f,1):FLinearColor(.34f,.40f,.41f,1)); }
FString Number(int64 Value) { return FString::Printf(TEXT("%lld"),static_cast<long long>(Value)); }
const TCHAR* WeaponLabel(gw::WeaponKind Kind) {
    switch(Kind){case gw::WeaponKind::Shotgun:return TEXT("霰弹枪");case gw::WeaponKind::Rifle:return TEXT("步枪");
    case gw::WeaponKind::Sniper:return TEXT("狙击枪");case gw::WeaponKind::MachineGun:return TEXT("机枪");
    case gw::WeaponKind::RocketLauncher:return TEXT("火箭筒");default:return TEXT("手枪");}
}
gwui::Icon WeaponIcon(gw::WeaponKind Kind) {
    switch(Kind){case gw::WeaponKind::Shotgun:return gwui::Icon::Shotgun;case gw::WeaponKind::Rifle:return gwui::Icon::Rifle;
    case gw::WeaponKind::Sniper:return gwui::Icon::Sniper;case gw::WeaponKind::MachineGun:return gwui::Icon::MachineGun;
    case gw::WeaponKind::RocketLauncher:return gwui::Icon::RocketLauncher;default:return gwui::Icon::Pistol;}
}
const FButtonStyle& ButtonStyle() {
    static const FButtonStyle Style=FButtonStyle().SetNormal(FSlateColorBrush(FLinearColor(.85f,.89f,.86f,1)))
        .SetHovered(FSlateColorBrush(FLinearColor(.74f,.83f,.77f,1))).SetPressed(FSlateColorBrush(FLinearColor(.64f,.76f,.69f,1)))
        .SetNormalPadding(FMargin(4)).SetPressedPadding(FMargin(4));
    return Style;
}
const FEditableTextBoxStyle& InputStyle() {
    static const FEditableTextBoxStyle Style=FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
        .SetBackgroundImageNormal(FSlateColorBrush(Panel)).SetBackgroundImageHovered(FSlateColorBrush(Panel))
        .SetBackgroundImageFocused(FSlateColorBrush(FLinearColor(.88f,.94f,.90f,1))).SetForegroundColor(Ink);
    return Style;
}
#if !UE_BUILD_SHIPPING
class SArenaControls : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SArenaControls) {} SLATE_ARGUMENT(TWeakObjectPtr<AArenaGameMode>,Game) SLATE_END_ARGS()
    void Construct(const FArguments& Args) {
        Game=Args._Game;
        bExpanded=FParse::Param(FCommandLine::Get(),TEXT("GWGMExpanded"));
        auto Button=[](const TCHAR* Label,TFunction<void()> Action)->TSharedRef<SWidget> {
            return SNew(SButton).ButtonStyle(&ButtonStyle()).HAlign(HAlign_Center).ForegroundColor(Ink)
                .Text(FText::FromString(Label)).OnClicked_Lambda([Action] { Action(); return FReply::Handled(); });
        };
        TSharedRef<SVerticalBox> Body=SNew(SVerticalBox);
        Body->AddSlot().AutoHeight().Padding(0,0,0,4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Identity,SEditableTextBox).Style(&InputStyle()).Text(FText::FromString(TEXT("我"))).HintText(FText::FromString(TEXT("昵称")))]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[SAssignNew(Command,SEditableTextBox).Style(&InputStyle()).Text(FText::FromString(TEXT("加入"))).OnTextCommitted_Lambda([this](const FText&,ETextCommit::Type C){if(C==ETextCommit::OnEnter)Send();})]];
        Body->AddSlot().AutoHeight().Padding(0,0,0,4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("发送评论"),[this]{Send();})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("锁定我的视角"),[this]{if(Game.IsValid())Game->FocusLocalViewer(Name());})]];
        Body->AddSlot().AutoHeight().Padding(0,0,0,4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("点赞 +5% HP"),[this]{Social(false);})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("分享 · 霰弹枪"),[this]{Social(true);})]];
        const TCHAR* Gifts[]={TEXT("仙女棒"),TEXT("能力药丸"),TEXT("魔法镜"),TEXT("甜甜圈"),TEXT("能量电池")};
        GiftBrushes.SetNum(5);
        TSharedRef<SHorizontalBox> GiftRow=SNew(SHorizontalBox);
        for(int32 Index=0;Index<5;++Index) {
            if(Index==3){Body->AddSlot().AutoHeight().Padding(0,0,0,4)[GiftRow];GiftRow=SNew(SHorizontalBox);}
            const FString Gift=Gifts[Index];auto& Brush=GiftBrushes[Index];Brush.DrawAs=ESlateBrushDrawType::Image;
            Brush.ImageSize=FVector2D(29,29);if(Game.IsValid())Brush.SetResourceObject(Game->GetGiftIcon(Gift));
            GiftRow->AddSlot().FillWidth(1).Padding(1,0)[SNew(SButton).ButtonStyle(&ButtonStyle()).HAlign(HAlign_Center)
                .OnClicked_Lambda([this,Gift]{if(Game.IsValid())Game->SimulateGift(Name(),Gift);return FReply::Handled();})
                [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SImage).Image(&GiftBrushes[Index])]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(FText::FromString(Gift)).Font(FCoreStyle::GetDefaultFontStyle("Regular",9)).ColorAndOpacity(Ink)]]];
        }
        Body->AddSlot().AutoHeight().Padding(0,0,0,4)[GiftRow];
        Body->AddSlot().AutoHeight().Padding(0,0,0,4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("+ 50 随机阵营"),[this]{if(Game.IsValid())Game->AddMockUsers(50);})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("+ 100 随机阵营"),[this]{if(Game.IsValid())Game->AddMockUsers(100);})]];
        Body->AddSlot().AutoHeight().Padding(0,0,0,4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("生成步枪兑换码"),[this]{if(Game.IsValid())Game->GenerateRifleCode();})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[SNew(SEditableTextBox).Style(&InputStyle()).IsReadOnly(true).Text_Lambda([this]{return FText::FromString(Game.IsValid()&&!Game->LastRifleCode.IsEmpty()?FString(TEXT("步枪"))+Game->LastRifleCode:TEXT(""));}).HintText(FText::FromString(TEXT("复制完整兑换评论")))]];
        Body->AddSlot().AutoHeight().Padding(0,0,0,4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("主播助红"),[this]{if(Game.IsValid())Game->HostAssist(1);})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("主播助蓝"),[this]{if(Game.IsValid())Game->HostAssist(2);})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("主播中立"),[this]{if(Game.IsValid())Game->HostAssist(0);})]];
        Body->AddSlot().AutoHeight().Padding(0,0,0,4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("暂停 / 继续"),[this]{if(Game.IsValid())Game->TogglePaused();})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("1× / 4×"),[this]{if(Game.IsValid())Game->SetDemoSpeed(Game->DemoSpeed==1?4:1);})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("全图"),[this]{if(Game.IsValid())Game->Overview();})]];
        Body->AddSlot().AutoHeight().Padding(0,0,0,4)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("重建红基地"),[this]{if(Game.IsValid())Game->DemoAction(TEXT("rebuild-red"));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("重建蓝基地"),[this]{if(Game.IsValid())Game->DemoAction(TEXT("rebuild-blue"));})]];
        Body->AddSlot().AutoHeight()[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("战斗展示"),[this]{if(Game.IsValid())Game->DemoAction(TEXT("combat-showcase"));})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4,0,0,0)[Button(TEXT("清空所有玩家 → 0"),[this]{if(Game.IsValid())Game->ResetArena();})]];
        ChildSlot[SNew(SBox).WidthOverride(276)[SNew(SBorder).Padding(6).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Paper)
            [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[SNew(SButton).ButtonStyle(&ButtonStyle()).ForegroundColor(Ink)
                .Text_Lambda([this]{return FText::FromString(bExpanded?TEXT("GM 本地模拟 · 收起"):TEXT("GM 本地模拟 · 展开"));})
                .OnClicked_Lambda([this]{bExpanded=!bExpanded;return FReply::Handled();})]
            +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[SNew(SBox).MaxDesiredHeight_Lambda([]{
                const auto* Viewport=GEngine&&GEngine->GameViewport?GEngine->GameViewport->Viewport:nullptr;
                return Viewport?FMath::Clamp(static_cast<float>(Viewport->GetSizeXY().Y)-220,120.f,400.f):400.f;
            })
                .Visibility_Lambda([this]{return bExpanded?EVisibility::Visible:EVisibility::Collapsed;})
                [SNew(SScrollBox)+SScrollBox::Slot()[Body]]]]]];
    }
private:
    TWeakObjectPtr<AArenaGameMode> Game;
    TSharedPtr<SEditableTextBox> Identity,Command;
    TArray<FSlateBrush> GiftBrushes;
    bool bExpanded=false;
    FString Name() const {return Identity->GetText().ToString().TrimStartAndEnd();}
    void Social(bool Share) {
        if(!Game.IsValid())return;
        const FString ViewerName=Name();
        if(ViewerName.IsEmpty()){Game->LastEvent=TEXT("先填写昵称并加入战局");return;}
        if(Share)Game->GetBridge()->SimulateShare(TEXT("local-")+ViewerName,ViewerName);
        else Game->GetBridge()->SimulateLike(TEXT("local-")+ViewerName,ViewerName,1);
    }
    void Send() {
        if(!Game.IsValid())return;
        const FString ViewerName=Name();
        if(ViewerName.IsEmpty()) { Game->LastEvent=TEXT("请输入测试昵称"); return; }
        Game->GetBridge()->SimulateComment(TEXT("local-")+ViewerName,ViewerName,Command->GetText().ToString());
    }
};
#endif
}
void AArenaHUD::BeginPlay() {
    Super::BeginPlay(); TextFont=NewObject<UFont>(this); TextFont->FontCacheType=EFontCacheType::Runtime;
    TextFont->GetMutableInternalCompositeFont()=FCompositeFont(FName(TEXT("Regular")),FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>();
#if !UE_BUILD_SHIPPING
    if(GEngine && GEngine->GameViewport && Game && !Game->GetBridge()->IsRelayMode()) {
        Controls=SNew(SOverlay).Visibility_Lambda([Game]{return Game->bShowControls&&!Game->GetBridge()->IsRelayMode()?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;})
            +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0,0,28,66)[SNew(SArenaControls).Game(Game)];
        GEngine->GameViewport->AddViewportWidgetContent(Controls.ToSharedRef(),10);
    }
#endif
}
void AArenaHUD::EndPlay(const EEndPlayReason::Type Reason) {
    if(Controls.IsValid() && GEngine && GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(Controls.ToSharedRef());
    Controls.Reset();Super::EndPlay(Reason);
}
void AArenaHUD::Text(const FString& Value,float X,float Y,int32 Size,FLinearColor Color,bool Center) {
    FCanvasTextItem Item(FVector2D(X,Y),FText::FromString(Value),FSlateFontInfo(TextFont,Size),Color);
    Item.bCentreX=Center; Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
}
FString AArenaHUD::FitText(const FString& Value,int32 Size,float MaxWidth,bool Ellipsis) const {
    if(Value.IsEmpty()||MaxWidth<=0)return TEXT("");
    const float DPI=Canvas&&Canvas->Canvas?Canvas->Canvas->GetDPIScale():1.f;
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const FSlateFontInfo Font(TextFont,Size);
    const auto Width=[&](const FString& S){return Measure->Measure(S,Font,DPI).X/FMath::Max(DPI,.01f);};
    if(Width(Value)<=MaxWidth)return Value;
    const FString Suffix=Ellipsis?TEXT("…"):TEXT("");FString Short=Value;
    while(!Short.IsEmpty()&&Width(Short+Suffix)>MaxWidth){
        Short.LeftChopInline(1);
        if(!Short.IsEmpty()&&Short[Short.Len()-1]>=0xD800&&Short[Short.Len()-1]<=0xDBFF)Short.LeftChopInline(1);
    }
    return Short+Suffix;
}
void AArenaHUD::Rect(float X,float Y,float W,float H,FLinearColor Color) { FCanvasTileItem Item(FVector2D(X,Y),FVector2D(W,H),Color);Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item); }
void AArenaHUD::Line(FVector2D A,FVector2D B,FLinearColor Color,float Width) {
    if(Width<=0 || Color.A<=0 || (B-A).IsNearlyZero())return;
    // FCanvasLineItem ignores alpha; use a quad so glows and fades blend correctly.
    const auto D=(B-A).GetSafeNormal();const FVector2D N(-D.Y*Width*.5,D.X*Width*.5);
    FCanvasTriangleItem Item(A+N,B+N,B-N,GWhiteTexture);
    FCanvasUVTri T{};T.V0_Pos=A+N;T.V1_Pos=B-N;T.V2_Pos=A-N;
    T.V0_UV=T.V1_UV=T.V2_UV=FVector2D::ZeroVector;Item.TriangleList.Add(T);
    Item.SetColor(Color);Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
}
void AArenaHUD::Polygon(const TArray<FVector2D>& P,FLinearColor Fill) {
    for(int32 i=1;i<P.Num()-1;++i) { FCanvasTriangleItem Item(P[0],P[i],P[i+1],GWhiteTexture);Item.SetColor(Fill);Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item); }
}
void AArenaHUD::Portrait(UTexture2D* Texture,float X,float Y,float Size) {
    if(!Texture || !Texture->GetResource())return;
    FCanvasTileItem Item(FVector2D(X,Y),Texture->GetResource(),FVector2D(Size,Size),FLinearColor::White);Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
}
FBox2D AArenaHUD::ClipArenaRect(const FBox2D& Bounds) const {
    if(!Bounds.bIsValid || ArenaSide<=0)return FBox2D(ForceInit);
    const FVector2D Min(FMath::Max(Bounds.Min.X,static_cast<double>(ArenaX)),FMath::Max(Bounds.Min.Y,static_cast<double>(ArenaY)));
    const FVector2D Max(FMath::Min(Bounds.Max.X,static_cast<double>(ArenaX+ArenaSide)),FMath::Min(Bounds.Max.Y,static_cast<double>(ArenaY+ArenaSide)));
    return Min.X<Max.X && Min.Y<Max.Y?FBox2D(Min,Max):FBox2D(ForceInit);
}
void AArenaHUD::ArenaRect(float X,float Y,float Width,float Height,FLinearColor Color) {
    const auto Bounds=ClipArenaRect(FBox2D({X,Y},{X+Width,Y+Height}));
    if(Bounds.bIsValid)Rect(Bounds.Min.X,Bounds.Min.Y,Bounds.GetSize().X,Bounds.GetSize().Y,Color);
}
void AArenaHUD::ArenaPortrait(UTexture2D* Texture,float X,float Y,float Size) {
    if(!Texture || !Texture->GetResource() || Size<=0)return;
    const auto Bounds=ClipArenaRect(FBox2D({X,Y},{X+Size,Y+Size}));if(!Bounds.bIsValid)return;
    FCanvasTileItem Item(Bounds.Min,Texture->GetResource(),Bounds.GetSize(),FLinearColor::White);
    Item.UV0=(Bounds.Min-FVector2D(X,Y))/Size;Item.UV1=(Bounds.Max-FVector2D(X,Y))/Size;
    Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
}
FVector2D AArenaHUD::Project(double X,double Y)const{return FVector2D(ArenaX+(X-ViewOrigin.X)*WorldScale,ArenaY+(Y-ViewOrigin.Y)*WorldScale)+ArenaShakeOffset;}
bool AArenaHUD::InArena(FVector2D P,float M)const{return P.X>=ArenaX+M && P.Y>=ArenaY+M && P.X<=ArenaX+ArenaSide-M && P.Y<=ArenaY+ArenaSide-M;}
void AArenaHUD::HealthBar(float X,float Y,float Width,float Height,double Hp,double MaxHp,FLinearColor Color,int32 FontSize) {
    const float Fraction=MaxHp>0?FMath::Clamp(Hp/MaxHp,0.0,1.0):0;
    Rect(X,Y,Width,Height,Border);Rect(X+1,Y+1,Width-2,Height-2,Panel);
    Rect(X+1,Y+1,(Width-2)*Fraction,Height-2,FMath::Lerp(Panel,Color,.42f));
    if(FontSize>0)Text(FString::Printf(TEXT("%d / %d"),FMath::CeilToInt(Hp),FMath::CeilToInt(MaxHp)),X+Width/2,Y+(Height-FontSize*1.4f)/2,FontSize,Ink,true);
}
void AArenaHUD::DrawFocusCard(AArenaGameMode* G,float X,float Y,float Width) {
    const auto& M=G->GetMatch();const bool Short=ViewportHeight<620;
    Rect(X-6,Y,Width,Short?42:166,Panel);
    const auto* F=M.findFighter(G->FocusBodyId);
    if(Short) {
        if(!F){Text(TEXT("点击榜单或角色 · 锁定视角"),X+10,Y+12,10,Muted);return;}
        const auto C=TeamColor(F->team);
        const bool Evolved=F->alive&&F->evolutionRemaining>0,Buffed=F->alive&&M.hasBossBuff(*F);
        const float PortraitSize=Evolved||Buffed?12.f:20.f;
        if(const auto* V=G->FindViewer(F->id))Portrait(V->Avatar,X+14-PortraitSize/2,Y+21-PortraitSize/2,PortraitSize);
        DrawStatusAura({X+14,Y+21},8,Evolved,Buffed,G->RunningTime,false,F->alive&&F->heroBuff);
        Text(G->ViewerName(F->id).Left(5),X+36,Y+4,10,Ink);
        const FString Equipped=F->temporaryWeaponRemaining>0?FString::Printf(TEXT("临时%s %.0fs"),WeaponLabel(F->temporaryWeaponKind),F->temporaryWeaponRemaining):WeaponLabel(F->weaponKind);
        Text(F->alive?Equipped:TEXT("阵亡 · 等待复活"),X+36,Y+23,8,F->alive?Muted:C);
        HealthBar(X+126,Y+12,Width-143,18,F->hp,F->maxHp,C,8);return;
    }
    if(!F) {
        gwui::DrawIcon(Canvas,gwui::Icon::Crosshair,{X+24,Y+35},26,Muted);
        Text(TEXT("点击榜单或角色"),X+50,Y+16,13,Ink);
        Text(TEXT("锁定视角 · 查看玩家数据"),X+50,Y+42,10,Muted);
        Text(TEXT("滚轮调整远近 · 右键解除锁定"),X+12,Y+90,10,Muted);return;
    }
    const auto C=TeamColor(F->team);
    const bool Evolved=F->alive && F->evolutionRemaining>0,Buffed=F->alive && M.hasBossBuff(*F);
    const float PortraitSize=Evolved||Buffed?22.f:30.f;
    if(const auto* V=G->FindViewer(F->id))Portrait(V->Avatar,X+23-PortraitSize/2,Y+25-PortraitSize/2,PortraitSize);
    else if(F->isHost)gwui::DrawIcon(Canvas,gwui::Icon::Score,{X+23,Y+25},29,Gold);
    DrawStatusAura({X+23,Y+25},14.f,Evolved,Buffed,G->RunningTime,false,F->alive&&F->heroBuff);
    Text(G->ViewerName(F->id).Left(10),X+48,Y+10,14,Ink);
    gwui::DrawIcon(Canvas,gwui::Icon::Crosshair,{X+Width-25,Y+22},18,C);
    gwui::DrawIcon(Canvas,gwui::Icon::Heart,{X+17,Y+57},16,C);
    HealthBar(X+34,Y+47,Width-50,20,F->hp,F->maxHp,C,10);
    gwui::DrawIcon(Canvas,gwui::Icon::Shield,{X+17,Y+79},14,FLinearColor(.10f,.52f,.62f,1));
    Text(FString::Printf(TEXT("%d"),FMath::FloorToInt(F->armor)),X+34,Y+70,10,Muted);
    if(F->heroBuff){gwui::DrawIcon(Canvas,gwui::Icon::Hero,{X+103,Y+79},14,Gold);Text(TEXT("英雄 · 同向连发剑气"),X+115,Y+70,9,Gold);}
    else if(Evolved){gwui::DrawIcon(Canvas,gwui::Icon::EvolutionBuff,{X+103,Y+79},13,FLinearColor(1,.34f,.025f,1));Text(FString::Printf(TEXT("进化 %.0fs"),F->evolutionRemaining),X+115,Y+70,10,Gold);}
    else Text(F->isHost?TEXT("主播助战 · 不参与排名"):TEXT("右键解除跟随"),X+83,Y+70,9,Muted);
    gwui::DrawIcon(Canvas,WeaponIcon(F->weaponKind),{X+19,Y+105},24,Ink);
    gwui::DrawIcon(Canvas,gwui::Icon::Bullet,{X+48,Y+105},14,Muted);
    Text(WeaponLabel(F->weaponKind),X+62,Y+91,10,Ink);
    if(F->temporaryWeaponRemaining>0){gwui::DrawIcon(Canvas,gwui::Icon::Clock,{X+137,Y+123},11,Gold);Text(FString::Printf(TEXT("临时%s %.0fs"),WeaponLabel(F->temporaryWeaponKind),F->temporaryWeaponRemaining),X+149,Y+117,8,Gold);}
    Text(FString::Printf(TEXT("%d / %d"),F->ammo,M.weaponFor(*F).magazine),X+62,Y+108,10,Muted);
    if(F->alive) {
        gwui::DrawIcon(Canvas,F->reloadRemaining>0?gwui::Icon::Reload:gwui::Icon::Crosshair,{X+150,Y+105},16,C);
        Text(F->reloadRemaining>0?FString::Printf(TEXT("%.1fs"),F->reloadRemaining):TEXT("AUTO"),X+166,Y+97,11,Muted);
    } else {
        const bool Auto=M.phase!=gw::Phase::Results && (F->isHost || (M.phase==gw::Phase::Battle && (F->team==0 || M.bases[F->team].alive)));
        Text(Auto?FString::Printf(TEXT("%.1fs 复活"),F->respawnRemaining):TEXT("等待复活"),X+137,Y+97,10,Muted);
    }
    gwui::DrawIcon(Canvas,gwui::Icon::Skull,{X+19,Y+143},18,Muted);
    Text(FString::FromInt(F->kills),X+38,Y+134,14,Ink);
    gwui::DrawIcon(Canvas,gwui::Icon::Score,{X+135,Y+143},18,Gold);
    Text(F->isHost?TEXT("助战"):Number(F->score),X+154,Y+134,15,Gold);
}
void AArenaHUD::DrawMinimap(AArenaGameMode* G) {
    const float Size=ArenaSide>=700?168.f:122.f;
    const float X=ArenaX+ArenaSide-Size-14,Y=ArenaY+ArenaSide-Size-14;
    MinimapBounds=FBox2D({X,Y},{X+Size,Y+Size});
    Rect(X-5,Y-23,Size+10,Size+28,FLinearColor(.985f,.99f,.975f,.96f));
    Text(G->FocusBodyId>=0?TEXT("已锁定 · 右键后可定位"):TEXT("全图 · 点击 / 拖动定位"),X,Y-20,9,Muted);
    if(G->RunningTime>=MinimapRefreshAt) {
        MinimapCells.fill(0);const auto& M=G->GetMatch();
        for(size_t i=0;i<M.world.bodies.size();++i)if(M.fighters[i].alive) {
            const auto& P=M.world.bodies[i].position;
            const int32 CellX=FMath::Clamp(static_cast<int32>(P.x*64/gw::World::Size),0,63);
            const int32 CellY=FMath::Clamp(static_cast<int32>(P.y*64/gw::World::Size),0,63);
            MinimapCells[CellY*64+CellX]|=1<<M.fighters[i].team;
        }
        MinimapRefreshAt=G->RunningTime+.2f;
    }
    Rect(X,Y,Size,Size,FLinearColor(.86f,.90f,.86f,1));
    for(int32 i=0;i<64*64;++i)if(const uint8 Mask=MinimapCells[i]) {
        auto C=(Mask&2)&&(Mask&4)?FLinearColor(.52f,.22f,.60f,1):TeamColor(Mask&2?1:Mask&4?2:0);
        Rect(X+(i%64)*Size/64,Y+(i/64)*Size/64,FMath::Max(1.f,Size/64-1),FMath::Max(1.f,Size/64-1),C);
    }
    for(int32 T=1;T<=2;++T) {
        const auto& B=G->GetMatch().bases[T];const FVector2D P(X+B.position.x/gw::World::Size*Size,Y+B.position.y/gw::World::Size*Size);
        gwui::DrawIcon(Canvas,gwui::Icon::Square,P,9,B.alive?TeamColor(T):Muted);
    }
    if(G->GetMatch().boss.active){const auto& B=G->GetMatch().boss;gwui::DrawIcon(Canvas,gwui::Icon::Skull,{X+B.position.x/gw::World::Size*Size,Y+B.position.y/gw::World::Size*Size},12,Gold);}
    for(const auto& Pack:G->GetMatch().evolutionPacks)if(Pack.active)gwui::DrawIcon(Canvas,gwui::Icon::Lightning,{X+Pack.position.x/gw::World::Size*Size,Y+Pack.position.y/gw::World::Size*Size},8,Gold);
    for(const auto& Crate:G->GetMatch().weaponCrates)if(Crate.active)gwui::DrawIcon(Canvas,gwui::Icon::WeaponCrate,{X+Crate.position.x/gw::World::Size*Size,Y+Crate.position.y/gw::World::Size*Size},8,Gold);
    const double Visible=gw::World::Size/G->CameraZoom;
    const FVector2D A(X+ViewOrigin.X/gw::World::Size*Size,Y+ViewOrigin.Y/gw::World::Size*Size);
    const FVector2D B=A+FVector2D(Visible/gw::World::Size*Size);
    Line(A,{B.X,A.Y},Ink,1.5f);Line({B.X,A.Y},B,Ink,1.5f);Line(B,{A.X,B.Y},Ink,1.5f);Line({A.X,B.Y},A,Ink,1.5f);
    if(const auto* Body=G->GetArena().find(G->FocusBodyId))
        gwui::DrawIcon(Canvas,gwui::Icon::Crosshair,{X+Body->position.x/gw::World::Size*Size,Y+Body->position.y/gw::World::Size*Size},12,Gold);
}
void AArenaHUD::DrawDamage(AArenaGameMode* G) {
    const auto& Slots=G->GetDamageNumbers().slots;
    for(const auto& S:Slots)if(S.active) {
        auto Position=S.position;
        if(S.targetKind==1)if(const auto* B=G->GetArena().find(S.targetId))Position=B->position;
        const bool Healing=S.kind==gw::NumberKind::Healing;
        const int32 Size=WorldScale>.26f?13:10;
        // Start on the avatar/body, then retain the existing upward fade.
        auto P=Project(Position.x,Position.y);P.Y-=Size*.6f+S.rise();if(!InArena(P,22))continue;
        auto C=Healing?FLinearColor(.16f,.88f,.36f,1):S.team==1?FLinearColor(1,.31f,.23f,1):S.team==2?FLinearColor(.27f,.65f,1,1):FLinearColor(1,.73f,.19f,1);C.A=S.alpha();
        const TCHAR* Sign=Healing?TEXT("+"):TEXT("-");
        const FString Value=FMath::IsNearlyEqual(S.amount,FMath::RoundToDouble(S.amount),.01)?
            FString::Printf(TEXT("%s%d"),Sign,FMath::RoundToInt(S.amount)):FString::Printf(TEXT("%s%.1f"),Sign,S.amount);
        const FSlateFontInfo NumberFont(TextFont,Size);
        const bool Split=std::any_of(Slots.begin(),Slots.end(),[&](const auto& Other){return Other.active && Other.targetKind==S.targetKind && Other.targetId==S.targetId && Other.kind!=S.kind;});
        if(Split) {
            // Opposite lanes touch the body center; measured widths keep even large values apart.
            const float DPI=Canvas->GetDPIScale();
            const float HalfWidth=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value,NumberFont,DPI).X/(2*DPI);
            P.X+=(Healing?1.f:-1.f)*(HalfWidth+3.f);
            if(!InArena(P,FMath::Max(22.f,HalfWidth+2.f)))continue;
        }
        FCanvasTextItem Item(P,FText::FromString(Value),NumberFont,C);
        Item.bOutlined=true;Item.OutlineColor=FLinearColor(.025f,.045f,.05f,S.alpha());
        Item.bCentreX=true;Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
    }
}
void AArenaHUD::DrawArena(AArenaGameMode* Game) {
    const auto& M=Game->GetMatch();
    UpdateArenaShake(Game);
    Rect(ArenaX+5,ArenaY+7,ArenaSide,ArenaSide,FLinearColor(.6f,.66f,.62f,.18f));
    Rect(ArenaX,ArenaY,ArenaSide,ArenaSide,Panel);
    for(int i=0;i<=32;++i) {
        auto P=Project(i*250,ViewOrigin.Y),Q=Project(ViewOrigin.X,i*250);
        if(InArena({P.X,ArenaY+1}))Line({P.X,ArenaY},{P.X,ArenaY+ArenaSide},FLinearColor(.80f,.84f,.80f,.25f));
        if(InArena({ArenaX+1,Q.Y}))Line({ArenaX,Q.Y},{ArenaX+ArenaSide,Q.Y},FLinearColor(.80f,.84f,.80f,.25f));
    }
    DrawCombatGround(Game);
    for(const auto& O:M.orbs)if(O.active) {
        const auto P=Project(O.position.x,O.position.y);
        const float R=FMath::Clamp(WorldScale*(O.natural?7.f:11.f),1.6f,5.f);
        if(!ClipArenaRect(FBox2D(P-FVector2D(R),P+FVector2D(R))).bIsValid)continue;
        const FLinearColor C=O.natural?FLinearColor(.12f,.52f,.28f,1):Gold;
        if(WorldScale<.26f) ArenaRect(P.X-R,P.Y-R,R*2,R*2,C);
        else {TArray<FVector2D> Ball;for(int32 i=0;i<12;++i)Ball.Add(P+FVector2D(FMath::Cos(i*PI/6),FMath::Sin(i*PI/6))*R);ArenaPolygon(Ball,C);}
    }
    for(const auto& N:M.npcs)if(N.active) {
        const auto P=Project(N.position.x,N.position.y);
        const float R=FMath::Max(32*WorldScale,4.f);
        if(!ClipArenaRect(FBox2D(P-FVector2D(R),P+FVector2D(R))).bIsValid)continue;
        TArray<FVector2D> V;for(int i=0;i<6;++i)V.Add(P+FVector2D(FMath::Cos(i*PI/3),FMath::Sin(i*PI/3))*R);
        ArenaPolygon(V,FMath::Lerp(FLinearColor(.64f,.59f,.40f,1),FLinearColor::White,FMath::Clamp(static_cast<float>(N.hitFlash/.16),0.f,1.f)));for(int i=0;i<6;++i)ArenaLine(V[i],V[(i+1)%6],Gold,N.hitFlash>0?2.5f:1.5f);
        if(WorldScale>.3f && InArena({P.X-30,P.Y+R+5}) && InArena({P.X+30,P.Y+R+17}))HealthBar(P.X-30,P.Y+R+5,60,12,N.hp,N.maxHp,Gold,8);
    }
    for(int T=1;T<=2;++T) {
        const auto& B=M.bases[T];const auto P=Project(B.position.x,B.position.y);
        const float R=FMath::Clamp(100*WorldScale,17.f,58.f);const auto C=TeamColor(T);
        if(!ClipArenaRect(FBox2D(P-FVector2D(R),P+FVector2D(R))).bIsValid)continue;
        ArenaRect(P.X-R,P.Y-R,R*2,R*2,B.alive?FMath::Lerp(Panel,C,.15f):FLinearColor(.75f,.77f,.74f,1));
        if(B.alive){ArenaRect(P.X-R*.5f,P.Y-R*.5f,R,R,C);ArenaLine(P+FVector2D(-R,-R),P+FVector2D(R,-R),C,3);}
        else{ArenaLine(P+FVector2D(-R,-R),P+FVector2D(R,R),Muted,2);ArenaLine(P+FVector2D(R,-R),P+FVector2D(-R,R),Muted,2);}
        if(InArena({P.X-36,P.Y+R+7})&&InArena({P.X+36,P.Y+R+23}))Text(T==1?TEXT("红方基地"):TEXT("蓝方基地"),P.X,P.Y+R+7,11,C,true);
        if(WorldScale>.26f && InArena({P.X-48,P.Y-R-20})&&InArena({P.X+48,P.Y-R-4}))HealthBar(P.X-48,P.Y-R-20,96,16,B.hp,B.maxHp,C,9);
        else {ArenaRect(P.X-R,P.Y-R-8,R*2,3,Border);ArenaRect(P.X-R,P.Y-R-8,R*2*B.hp/B.maxHp,3,C);}
    }
    int32 Labels=0;
    for(size_t Index=0;Index<M.world.bodies.size();++Index) {
        const auto& B=M.world.bodies[Index];const auto& F=M.fighters[Index];
        if(!F.alive)continue;
        const auto P=Project(B.position.x,B.position.y);
        const FLinearColor C=TeamColor(F.team);const float Scale=WorldScale*B.scale;
        // A visible body, barrel or aura survives even when its center crosses
        // a viewport edge. The primitives below clip their actual geometry.
        const float Reach=FMath::Max(5.f,90*Scale);
        if(!ClipArenaRect(FBox2D(P-FVector2D(Reach),P+FVector2D(Reach))).bIsValid)continue;
        const bool Focused=B.id==Game->FocusBodyId;
        if(WorldScale<.26f && !F.isHost && !Focused && !F.heroBuff) {
            const float R=FMath::Clamp(20*Scale,1.3f,3.f);
            DrawStatusAura(P,R+1.3f,F.evolutionRemaining>0,M.hasBossBuff(F),Game->RunningTime,true);
            ArenaRect(P.X-R,P.Y-R,R*2,R*2,F.hitFlash>0?Gold:C);continue;
        }
        DrawStatusAura(P,FMath::Max(4.f,36*Scale),F.evolutionRemaining>0,M.hasBossBuff(F),Game->RunningTime,true,F.heroBuff);
        TArray<FVector2D> Points;
        if(F.isHost){for(int32 i=0;i<10;++i){const float A=-PI/2+i*PI/5+B.angle;Points.Add(P+FVector2D(FMath::Cos(A),FMath::Sin(A))*(i%2?10.f:22.f)*Scale);}}
        else if(B.shape==gw::Shape::Circle) {for(int i=0;i<12;++i)Points.Add(P+FVector2D(FMath::Cos(i*PI/6),FMath::Sin(i*PI/6))*22*Scale);}
        else for(auto V:gw::vertices(B))Points.Add(Project(V.x,V.y));
        FBox2D BodyBounds(ForceInit);for(const auto Point:Points)BodyBounds+=Point;
        const bool BodyVisible=ClipArenaRect(BodyBounds).bIsValid;
        const auto BaseFill=FMath::Lerp(Panel,C,F.isHost?.32f:.16f);
        const auto Fill=FMath::Lerp(F.heroBuff?FMath::Lerp(BaseFill,Gold,.20f):BaseFill,FLinearColor::White,FMath::Clamp(static_cast<float>(F.hitFlash/.18),0.f,1.f));
        if(F.isHost){for(int32 i=0;i<10;++i)ArenaPolygon({P,Points[i],Points[(i+1)%10]},Fill);}else ArenaPolygon(Points,Fill);
        for(int32 i=0;i<Points.Num();++i)ArenaLine(Points[i],Points[(i+1)%Points.Num()],F.hitFlash>0?Gold:C,F.hitFlash>0?2.8f:1.5f);
        if(F.hitFlash>0)ArenaRing(P,(28+(1-F.hitFlash/.18)*16)*Scale,FLinearColor(C.R,C.G,C.B,F.hitFlash/.18*.5),1.5f,16);
        const FVector2D Aim(FMath::Cos(F.aimAngle),FMath::Sin(F.aimAngle));
        const FVector2D Side(-Aim.Y,Aim.X);const float Recoil=FMath::Max(0.,F.shotRemaining-(M.weaponFor(F).fireInterval-.10))*45;
        const float Barrel=F.weaponKind==gw::WeaponKind::Pistol?44.f:F.weaponKind==gw::WeaponKind::Sniper?76.f:F.weaponKind==gw::WeaponKind::MachineGun?65.f:56.f;
        ArenaLine(P+Aim*(17-Recoil)*Scale,P+Aim*(Barrel-Recoil)*Scale,Ink,FMath::Max(2.f,(F.weaponKind==gw::WeaponKind::RocketLauncher?12.f:5.f)*WorldScale));
        if(F.weaponKind==gw::WeaponKind::Shotgun)ArenaLine(P+Aim*20*Scale+Side*4*WorldScale,P+Aim*Barrel*Scale+Side*4*WorldScale,Ink,FMath::Max(1.f,2*WorldScale));
        if(F.weaponKind==gw::WeaponKind::Rifle)ArenaLine(P+Aim*28*Scale,P+Aim*23*Scale+Side*9*WorldScale,Ink,FMath::Max(2.f,4*WorldScale));
        if(F.weaponKind==gw::WeaponKind::Sniper){ArenaLine(P+Aim*32*Scale-Side*7*Scale,P+Aim*48*Scale-Side*7*Scale,Ink,FMath::Max(2.f,5*WorldScale));ArenaLine(P+Aim*68*Scale-Side*4*Scale,P+Aim*68*Scale+Side*4*Scale,Ink,2);}
        if(F.weaponKind==gw::WeaponKind::MachineGun){ArenaLine(P+Aim*21*Scale+Side*8*Scale,P+Aim*46*Scale+Side*8*Scale,Ink,FMath::Max(2.f,7*WorldScale));ArenaLine(P+Aim*52*Scale-Side*4*Scale,P+Aim*Barrel*Scale-Side*4*Scale,Ink,FMath::Max(1.f,2*WorldScale));}
        if(F.weaponKind==gw::WeaponKind::RocketLauncher)ArenaLine(P+Aim*Barrel*Scale-Side*9*Scale,P+Aim*Barrel*Scale+Side*9*Scale,Ink,FMath::Max(2.f,4*WorldScale));
        const auto* V=Game->FindViewer(B.id);
        if(V){const float R=(B.shape==gw::Shape::Triangle?8.f:12.f)*WorldScale;ArenaPortrait(V->Avatar,P.X-R,P.Y-R,R*2);}
        else if(F.isHost && InArena(P,8*Scale))gwui::DrawIcon(Canvas,gwui::Icon::Score,P,14*Scale,Gold);
        const float Width=FMath::Max(F.isHost?74.f:56.f,66*WorldScale),BarX=FMath::Clamp(P.X-Width/2,ArenaX+2,ArenaX+ArenaSide-Width-2),BarY=P.Y+34*Scale;
        if(BodyVisible && BarY>=ArenaY && BarY+20<ArenaY+ArenaSide){HealthBar(BarX,BarY,Width,13,F.hp,F.maxHp,C,8);if(F.armor>0){Rect(BarX,BarY+15,Width,3,Border);Rect(BarX,BarY+15,Width*FMath::Clamp(F.armor/FMath::Max(F.maxArmor,1.),0.,1.),3,FLinearColor(.12f,.55f,.64f,1));}}
        const FVector2D LabelPoint(P.X,P.Y-36*Scale-11);
        const bool ShowLabel=(V||F.isHost) && (Labels<180||Focused) && (WorldScale>.38f||Focused||F.isHost) && LabelPoint.X>=ArenaX+55 && LabelPoint.X<=ArenaX+ArenaSide-55 && LabelPoint.Y>=ArenaY+2 && LabelPoint.Y+17<=ArenaY+ArenaSide;
        if(ShowLabel) {Text(F.isHost?TEXT("主播 · 助战"):V->Name.Left(7),LabelPoint.X,LabelPoint.Y,11,F.isHost?Gold:Ink,true);++Labels;}
        if(Focused) {
            // Screen-space arrow stays six pixels above the name at every zoom.
            if(ShowLabel && LabelPoint.Y-14>=ArenaY+2)ArenaPolygon({LabelPoint+FVector2D(-6,-14),LabelPoint+FVector2D(6,-14),LabelPoint+FVector2D(0,-6)},Gold);
            if(InArena({P.X-28,BarY+22})&&InArena({P.X+28,BarY+38}))Text(TEXT("正在跟随"),P.X,BarY+22,10,Gold,true);
        }
    }
    DrawGunfire(Game);DrawCombatActors(Game);
    Line({ArenaX,ArenaY},{ArenaX+ArenaSide,ArenaY},Border,2);Line({ArenaX,ArenaY},{ArenaX,ArenaY+ArenaSide},Border,2);
    Line({ArenaX+ArenaSide,ArenaY},{ArenaX+ArenaSide,ArenaY+ArenaSide},Border,2);Line({ArenaX,ArenaY+ArenaSide},{ArenaX+ArenaSide,ArenaY+ArenaSide},Border,2);
    DrawDamage(Game);
    ArenaShakeOffset=FVector2D::ZeroVector;
    if(Game->IsSimulationPaused())Text(TEXT("已暂停"),ArenaX+ArenaSide/2,ArenaY+20,24,Ink,true);
}
void AArenaHUD::DrawResults(AArenaGameMode* Game) {
    const auto& M=Game->GetMatch();
    const float S=FMath::Clamp(ArenaSide/850.f,.55f,1.f);
    const float X=ArenaX+32*S,Y=ArenaY+ArenaSide*.11f,W=ArenaSide-64*S;
    if(ResultsRound!=M.round){ResultsRound=M.round;ResultsSeenAt=Game->RunningTime;}
    const float Age=FMath::Max(0.f,Game->RunningTime-ResultsSeenAt);
    Rect(ArenaX,ArenaY,ArenaSide,ArenaSide,FLinearColor(.025f,.045f,.05f,.88f));
    const auto C=TeamColor(M.winnerTeam);
    Text(FString::Printf(TEXT("ROUND %02d  /  本局战报"),M.round),X+W/2,Y,FMath::Max(10,FMath::RoundToInt(14*S)),FLinearColor(.72f,.78f,.73f,1),true);
    Text(M.winnerTeam==1?TEXT("红方获胜"):TEXT("蓝方获胜"),X+W/2,Y+36*S,FMath::RoundToInt(46*S),C,true);
    Text(Number(M.teamScores[1])+TEXT("  :  ")+Number(M.teamScores[2]),X+W/2,Y+110*S,FMath::RoundToInt(30*S),Panel,true);
    const float Gap=24*S,CardY=Y+170*S,CardW=(W-Gap)/2;
    auto Award=[&](const TCHAR* Title,const TCHAR* Reason,int32 Id,float Left) {
        Rect(Left,CardY,CardW,200*S,FLinearColor(.075f,.105f,.12f,1));
        Text(Title,Left+CardW/2,CardY+17*S,FMath::RoundToInt(24*S),Gold,true);
        const auto* V=Game->FindViewer(Id);if(V)Portrait(V->Avatar,Left+CardW/2-23*S,CardY+58*S,46*S);
        Text(Game->ViewerName(Id).Left(10),Left+CardW/2,CardY+115*S,FMath::RoundToInt(19*S),Panel,true);
        Text(Reason,Left+CardW/2,CardY+149*S,FMath::Max(8,FMath::RoundToInt(11*S)),FLinearColor(.64f,.72f,.70f,1),true);
        if(const auto* F=M.findFighter(Id))Text(Number(F->score)+FString::Printf(TEXT(" 分  ·  %d 击败"),F->kills),Left+CardW/2,CardY+174*S,FMath::Max(8,FMath::RoundToInt(12*S)),Panel,true);
    };
    Award(TEXT("MVP"),TEXT("胜利方最出色 · 积分最高"),M.mvpId,X);
    Award(TEXT("FMVP"),TEXT("失败方最出色 · 积分最高"),M.fmvpId,X+CardW+Gap);
    const auto Honor=[&](const TCHAR* Title,const TCHAR* Description,int32 Id,float Left,float Delay,bool Damage) {
        const float T=FMath::Clamp((Age-Delay)/.45f,0.f,1.f),Ease=T*T*(3-2*T);
        if(Ease<=0)return;
        const float Top=CardY+218*S+(1-Ease)*18*S,Center=Left+CardW/2;
        const auto Tint=[Ease](FLinearColor Color){Color.A*=Ease;return Color;};
        const auto* F=M.findFighter(Id);const auto* V=Game->FindViewer(Id);
        const FLinearColor Accent=Damage?FLinearColor(.32f,.73f,.76f,1):FLinearColor(.94f,.69f,.28f,1);
        Rect(Left,Top,CardW,153*S,Tint(FLinearColor(.075f,.105f,.12f,1)));
        Rect(Left,Top,CardW*Ease,2*S,Tint(Accent));
        Text(Title,Center,Top+12*S,FMath::Max(12,FMath::RoundToInt(18*S)),Tint(Accent),true);
        if(F) {
            const float PortraitSize=35*S,PX=Left+21*S,PY=Top+49*S;
            if(V&&V->Avatar&&V->Avatar->GetResource()) {
                FCanvasTileItem Item(FVector2D(PX,PY),V->Avatar->GetResource(),FVector2D(PortraitSize),Tint(FLinearColor::White));
                Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
            }
            const float NameX=PX+PortraitSize+11*S;
            const int32 NameSize=FMath::Max(10,FMath::RoundToInt(15*S));
            Text(FitText(Game->ViewerName(Id),NameSize,Left+CardW-NameX-16*S),NameX,PY-2*S,NameSize,Tint(Panel));
            const FLinearColor Camp=FMath::Lerp(TeamColor(F->team),Panel,.4f);
            Text(F->team==1?TEXT("红色阵营"):F->team==2?TEXT("蓝色阵营"):TEXT("灰色阵营"),NameX,PY+20*S,FMath::Max(8,FMath::RoundToInt(11*S)),Tint(Camp));
            const FString Value=Damage?FString::Printf(TEXT("%.0f  承伤"),F->damageTaken):FString::Printf(TEXT("%d  击杀"),F->kills);
            Text(Value,Center,Top+98*S,FMath::Max(14,FMath::RoundToInt(23*S)),Tint(Panel),true);
        } else Text(TEXT("暂无有效战绩"),Center,Top+67*S,FMath::Max(10,FMath::RoundToInt(14*S)),Tint(FLinearColor(.64f,.72f,.70f,1)),true);
        Text(Description,Center,Top+133*S,FMath::Max(8,FMath::RoundToInt(10*S)),Tint(FLinearColor(.64f,.72f,.70f,1)),true);
    };
    Honor(TEXT("击杀最多"),TEXT("全场观众 · 含灰色阵营"),M.mostKillsId,X,.15f,false);
    Honor(TEXT("承伤最多"),TEXT("实际生命损失 + 护甲吸收"),M.mostDamageTakenId,X+CardW+Gap,.30f,true);
    Text(FString::Printf(TEXT("下一局将在 %02d 秒后开始"),FMath::CeilToInt(M.intermissionRemaining)),X+W/2,CardY+404*S,FMath::RoundToInt(21*S),Panel,true);
    Text(TEXT("身份与阵营保留  ·  积分清零  ·  基地恢复"),X+W/2,CardY+440*S,FMath::Max(8,FMath::RoundToInt(12*S)),FLinearColor(.66f,.73f,.70f,1),true);
}
void AArenaHUD::DrawGiftNotice(AArenaGameMode* Game) {
    const auto& Notice=Game->GetGiftNotice();if(!Notice.Active || Notice.Age>=2)return;
    const float Enter=FMath::Clamp(Notice.Age/.25f,0.f,1.f),Ease=1-FMath::Pow(1-Enter,3.f);
    const float Alpha=Ease*FMath::Clamp((2-Notice.Age)/.30f,0.f,1.f);
    const float Width=FMath::Min(460.f,ArenaSide-24),X=ArenaX+(ArenaSide-Width)/2,Y=ArenaY+24-(1-Ease)*14;
    const bool Small=Width<330;const float IconSize=Small?32.f:44.f,TextX=X+IconSize+26;
    const auto Tint=[Alpha](FLinearColor Color,float Opacity){Color.A=Alpha*Opacity;return Color;};
    Rect(X-5,Y-5,Width+10,78,Tint(Gold,.055f));Rect(X-2,Y-2,Width+4,72,Tint(Gold,.10f));
    Rect(X,Y,Width,68,Tint(Panel,.96f));Rect(X,Y,3,68,Tint(Gold,.8f));
    if(auto* Icon=Game->GetGiftIcon(Notice.GiftName);Icon && Icon->GetResource()) {
        FCanvasTileItem Item(FVector2D(X+13,Y+(68-IconSize)/2),Icon->GetResource(),FVector2D(IconSize),Tint(FLinearColor::White,1));
        Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
    }
    Text(Notice.ViewerName.Left(Small?4:10)+TEXT(" 送出 ")+Notice.GiftName,TextX,Y+12,Small?9:12,Tint(Ink,1));
    Text(TEXT("永久解锁 · ")+Notice.WeaponName,TextX,Y+37,Small?9:12,Tint(Gold,1));
}
void AArenaHUD::DrawRankTooltip(AArenaGameMode* Game,int32 FighterIndex,float X,float Y,float Width) {
    const auto& M=Game->GetMatch();if(FighterIndex<0||FighterIndex>=static_cast<int32>(M.fighters.size()))return;
    const auto& F=M.fighters[FighterIndex];
    const FString FullName=Game->ViewerName(F.id),NameFirst=FitText(FullName,11,Width-24,false);
    const bool Wrapped=NameFirst.Len()<FullName.Len();const float Height=Wrapped?245:226;
    X=FMath::Clamp(X,8.f,FMath::Max(8.f,ViewportWidth-Width-8));Y=FMath::Clamp(Y,155.f,FMath::Max(155.f,ViewportHeight-Height-58));
    Rect(X+3,Y+4,Width,Height,FLinearColor(.08f,.13f,.15f,.10f));Rect(X,Y,Width,Height,Panel);
    Rect(X,Y,3,Height,TeamColor(F.team));
    Text(NameFirst,X+12,Y+10,11,Ink);
    if(Wrapped){Text(FullName.Mid(NameFirst.Len()),X+12,Y+29,11,Ink);Y+=19;}
    gwui::DrawIcon(Canvas,WeaponIcon(F.weaponKind),{X+22,Y+50},24,Ink);
    Text(WeaponLabel(F.weaponKind),X+42,Y+36,11,Ink);
    Text(FString::Printf(TEXT("弹药 %d/%d · %s"),F.ammo,M.weaponFor(F).magazine,F.alive?TEXT("存活"):TEXT("阵亡")),X+42,Y+54,9,Muted);
    const FString DamageLabel=F.weaponKind==gw::WeaponKind::Shotgun?TEXT("15–25 发 · 每发 3–5"):FString::Printf(TEXT("基础伤害 %.0f"),M.weaponFor(F).damage);
    Text(FString::Printf(TEXT("武器移速 %.0f%% · "),gw::Match::weaponMovementMultiplier(F.weaponKind)*100)+DamageLabel,X+12,Y+76,9,Muted);
    FString WeaponDetail=FString::Printf(TEXT("射程 %.0f · 换弹 %.0fs"),M.weaponFor(F).range,M.weaponFor(F).reloadTime);
    if(F.weaponKind==gw::WeaponKind::Sniper)WeaponDetail=TEXT("射程 4800 · 瞄准 1–2s · 远距 +50%");
    else if(F.weaponKind==gw::WeaponKind::Shotgun)WeaponDetail=TEXT("扇形 60° · 5 轮弹匣 · 换弹 3s");
    else if(F.weaponKind==gw::WeaponKind::RocketLauncher)WeaponDetail=TEXT("爆炸伤害：中心 1000 / 边缘 100");
    Text(WeaponDetail,X+12,Y+94,9,Muted);
    Line({X+12,Y+115},{X+Width-12,Y+115},Border);
    const auto Status=[&](gwui::Icon Icon,FLinearColor Color,float Row,const FString& Label){gwui::DrawIcon(Canvas,Icon,{X+20,Y+Row+8},14,Color);Text(Label,X+35,Y+Row,9,Color);};
    const bool BossBuff=M.hasBossBuff(F),Evolved=F.alive&&F.evolutionRemaining>0,Hero=F.heroBuff;
    Status(gwui::Icon::BossBuff,BossBuff?FLinearColor(.57f,.17f,.95f,1):Muted,123,BossBuff?FString::Printf(TEXT("BOSS强化 · 伤害/得分+20%% · %.0fs"),M.teamBuffRemaining[F.team]):TEXT("BOSS强化 · 未获得"));
    Status(gwui::Icon::EvolutionBuff,Evolved?FLinearColor(.92f,.31f,.025f,1):Muted,146,Evolved?FString::Printf(TEXT("进化强化 · 剩余 %.0fs"),F.evolutionRemaining):TEXT("进化强化 · 未获得"));
    Status(gwui::Icon::Hero,Hero?Gold:Muted,169,Hero?TEXT("六道同向连发 · 间隔0.2秒"):TEXT("英雄强化 · 冲刺开始时积分前十"));
    Status(gwui::Icon::Clock,F.temporaryWeaponRemaining>0?Gold:Muted,192,F.temporaryWeaponRemaining>0?FString::Printf(TEXT("临时%s · 剩余 %.0fs"),WeaponLabel(F.temporaryWeaponKind),F.temporaryWeaponRemaining):TEXT("当前装备为永久解锁武器"));
}
void AArenaHUD::DrawHUD() {
    Super::DrawHUD();auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>();if(!Canvas||!G)return;
    const double Start=FPlatformTime::Seconds();const auto& M=G->GetMatch();
    if(M.phase!=gw::Phase::Results)ResultsRound=-1;
    const float W=Canvas->SizeX,H=Canvas->SizeY,Right=W-304;
    ViewportWidth=W;ViewportHeight=H;RankRegions.Reset();RankBounds=FBox2D(ForceInit);MinimapBounds=FBox2D(ForceInit);
    const float LeftWidth=W>=1180 && H>=900?220.f:0.f;
    ArenaSide=FMath::Max(200.f,FMath::Min(H-226,W-LeftWidth-368));
    ArenaX=32+LeftWidth+(W-LeftWidth-368-ArenaSide)/2;ArenaY=148;
    gw::ArenaView View(G->CameraZoom,{G->CameraCenter.X,G->CameraCenter.Y});
    G->CameraZoom=View.zoom;G->CameraCenter={View.center.x,View.center.y};
    const double Visible=View.visibleSize();WorldScale=ArenaSide/Visible;
    ViewOrigin=G->CameraCenter-FVector2D(Visible/2,Visible/2);
    Rect(0,0,W,H,Paper);
    const bool Compact=LeftWidth==0;
    Text(TEXT("几何战争"),32,22,Compact?18:29,Ink);if(!Compact)Text(TEXT("FEISHI  /  GEOMETRIC WARFARE"),34,62,10,Muted);
    const int32 Sec=FMath::Max(0,FMath::CeilToInt(M.config.battleSeconds-M.elapsed));
    Text(FString::Printf(TEXT("%02d:%02d"),Sec/60,Sec%60),ArenaX+ArenaSide/2,16,40,Ink,true);
    const FString PhaseLabel=M.phase==gw::Phase::Results?TEXT("本局已结束"):M.phase==gw::Phase::Sprint?(Compact?TEXT("最后冲刺 · 禁止重建"):TEXT("最后冲刺 · 基地不可重建")):FString::Printf(TEXT("%d 分钟夺分战"),FMath::CeilToInt(M.config.battleSeconds/60));
    Text(PhaseLabel,ArenaX+ArenaSide/2,70,Compact?9:12,M.phase==gw::Phase::Sprint?Gold:Muted,true);
    for(int T=1;T<=2;++T){
        const float X=T==1?ArenaX:ArenaX+ArenaSide-174;const auto C=TeamColor(T);
        Text(T==1?TEXT("RED"):TEXT("BLUE"),X,Compact?65:19,12,C);
        gwui::DrawIcon(Canvas,gwui::Icon::Score,{X+9,Compact?101.f:58.f},16,C);
        Text(Number(M.teamScores[T]),X+25,Compact?78:39,Compact?23:27,C);
        if(Compact) {
            gwui::DrawIcon(Canvas,gwui::Icon::People,{X+69,74},14,C);
            Text(FString::Printf(TEXT("%d / 2000"),M.teamCounts[T]),X+83,67,10,Muted);
            HealthBar(X,M.teamBuffRemaining[T]>0?115:123,174,15,M.bases[T].hp,M.bases[T].maxHp,C,8);
        } else {
            gwui::DrawIcon(Canvas,gwui::Icon::Shield,{X+9,96},16,C);
            HealthBar(X+24,88,150,16,M.bases[T].hp,M.bases[T].maxHp,C,9);
            gwui::DrawIcon(Canvas,gwui::Icon::People,{X+9,123},16,C);
            Text(FString::Printf(TEXT("%d / 2000"),M.teamCounts[T]),X+24,115,11,Muted);
            if(!M.bases[T].alive)Text(TEXT("已摧毁"),X+120,116,9,C);
        }
        if(M.teamBuffRemaining[T]>0){
            const FLinearColor Purple(.53f,.17f,.81f,1);Rect(X,130,174,17,FLinearColor(.66f,.39f,.9f,.13f));
            DrawStatusAura({X+9,138},4.8f,false,true,G->RunningTime,false);
            Text(FString::Printf(TEXT("BOSS 伤害/得分+20%%  %.0fs"),M.teamBuffRemaining[T]),X+20,131,8,Purple);
        }
    }
    DrawBossBar(G);
    Text(G->GetBridge()->IsRelayMode()?TEXT("ADAPTER"):TEXT("LOCAL DEMO"),Right,25,11,Muted);
    gwui::DrawIcon(Canvas,gwui::Icon::People,{Right+10,65},22,Ink);
    Text(FString::Printf(TEXT("%d / 5000"),G->GetViewers().Num()),Right+31,48,23,Ink);
    gwui::DrawIcon(Canvas,gwui::Icon::Circle,{Right+10,109},18,TeamColor(0));
    Text(FString::Printf(TEXT("灰色 %d / 1000"),M.teamCounts[0]),Right+29,99,13,Muted);
    if(M.teamBuffRemaining[0]>0){
        const FLinearColor Purple(.53f,.17f,.81f,1);Rect(Right,123,272,17,FLinearColor(.66f,.39f,.9f,.13f));
        DrawStatusAura({Right+9,131},4.8f,false,true,G->RunningTime,false);
        Text(FString::Printf(TEXT("灰方 BOSS 强化 · 伤害/得分+20%%  %.0fs"),M.teamBuffRemaining[0]),Right+20,124,8,Purple);
    }
    if(M.fighters.size()>static_cast<size_t>(G->GetViewers().Num())){gwui::DrawIcon(Canvas,gwui::Icon::Score,{Right+196,111},13,Gold);Text(TEXT("主播助战"),Right+208,102,9,Gold);}
    DrawArena(G);
    if(LeftWidth>0){
        Text(TEXT("加入战局"),32,149,19,Ink);
        Text(TEXT("评论「加入」· 随机生成角色"),32,185,11,Muted);
        Rect(32,217,80,28,FMath::Lerp(Panel,TeamColor(1),.12f));Rect(124,217,80,28,FMath::Lerp(Panel,TeamColor(2),.12f));
        Text(TEXT("1  红方"),44,220,13,TeamColor(1));Text(TEXT("2  蓝方"),136,220,13,TeamColor(2));
        Text(TEXT("可直接入队 · 队内再发数字换形"),32,258,10,Muted);
        const gwui::Icon ShapeIcons[]={gwui::Icon::Circle,gwui::Icon::Square,gwui::Icon::Rectangle,gwui::Icon::Triangle};
        const int Hp[]={300,250,200,300};
        for(int i=0;i<4;++i){
            const float Y=292+i*66;
            Rect(32,Y-7,189,62,Panel);
            Text(FString::FromInt(i+1),40,Y,12,Muted);
            gwui::DrawIcon(Canvas,ShapeIcons[i],{74,Y+9},26,Ink);
            gwui::DrawIcon(Canvas,gwui::Icon::Heart,{111,Y+9},14,TeamColor(1));
            Text(FString::Printf(TEXT("生命 %d"),Hp[i]),123,Y,11,Ink);
            if(i==0){Text(TEXT("承伤减半"),42,Y+23,9,Muted);Text(TEXT("换弹时间减半"),112,Y+23,9,Muted);}
            if(i==1){Text(TEXT("资源 / NPC 积分 2 倍"),42,Y+21,9,Muted);Text(TEXT("中立伤害 +150%"),42,Y+37,9,Muted);}
            if(i==2){Text(TEXT("射程 / 精度加成"),42,Y+21,9,Muted);Text(TEXT("瞄准加快"),42,Y+37,9,Muted);}
            if(i==3){Text(TEXT("伤害 +50%"),42,Y+23,9,Muted);Text(TEXT("接触伤害 15"),125,Y+23,9,Muted);}
        }
        Line({32,559},{221,559},Border);
        gwui::DrawIcon(Canvas,gwui::Icon::Score,{42,583},18,Gold);Text(TEXT("夺分 · 守护 · 冲刺"),60,571,13,Ink);
        gwui::DrawIcon(Canvas,gwui::Icon::Circle,{41,614},12,FLinearColor(.12f,.52f,.28f,1));Text(TEXT("+2"),55,605,11,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::Square,{114,614},13,Gold);Text(TEXT("NPC +25"),130,605,11,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::Skull,{41,637},16,Muted);Text(TEXT("击败敌人，夺取 20% 积分"),60,628,10,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::Clock,{41,661},16,Muted);Text(FString::Printf(TEXT("末段 %d 秒 · 基地崩塌"),FMath::RoundToInt(M.config.battleSeconds-M.config.sprintSeconds)),60,652,10,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::Heart,{41,685},14,TeamColor(1));Text(TEXT("点赞 +5% 生命"),60,676,10,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::Shotgun,{41,709},17,Ink);Text(TEXT("分享解锁霰弹枪"),60,700,10,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::EvolutionBuff,{41,733},17,FLinearColor(.92f,.31f,.025f,1));Text(TEXT("进化 / 宝箱 · 击破或触碰获取"),60,724,9,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::WeaponCrate,{41,757},17,Gold);Text(FString::Printf(TEXT("进化 40 秒 · 宝箱武器 %.0f 秒"),gw::TemporaryWeaponLifetime),60,748,9,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::Hero,{41,781},17,Gold);Text(TEXT("冲刺前十成为英雄 · 同向连发剑气"),60,772,9,Muted);
        const float Bottom=H-94,TitleY=FMath::Max(806.f,Bottom-198);
        Text(FString::Printf(TEXT("实时加入 · 待播 %d"),static_cast<int32>(G->GetFeed().pending())),32,TitleY,11,Ink);
        for(const auto& S:G->GetFeed().slots)if(S.active){const float Y=Bottom-S.age*44;if(Y<TitleY+24)continue;auto C=Muted;C.A=FMath::Pow(FMath::Clamp(1.f-static_cast<float>(S.age/gw::ScrollFeed<FString>::Lifetime),0.f,1.f),1.4f);Text(S.text.Left(18),32,Y,11,C);}
    }
    if(Compact && M.phase!=gw::Phase::Results) {
        const float Bottom=ArenaY+ArenaSide-16;
        Rect(ArenaX+8,Bottom-118,190,126,FLinearColor(.94f,.95f,.92f,.88f));
        Text(FString::Printf(TEXT("实时加入 · 待播 %d"),static_cast<int32>(G->GetFeed().pending())),ArenaX+16,Bottom-113,9,Ink);
        for(const auto& S:G->GetFeed().slots)if(S.active && S.age<2.15){auto C=Ink;C.A=FMath::Pow(1.f-static_cast<float>(S.age/2.15),1.4f);Text(S.text.Left(18),ArenaX+16,Bottom-S.age*39,9,C);}
    }
    gwui::DrawIcon(Canvas,gwui::Icon::Trophy,{Right+10,159},21,Gold);
    Text(TEXT("本局积分榜"),Right+30,145,18,Ink);Text(TEXT("点击锁定视角"),Right+157,152,10,Muted);
    Text(TEXT("昵称"),Right+55,174,8,Muted);Text(TEXT("装备 / 增益"),Right+118,174,8,Muted);
    Text(TEXT("状态"),Right+201,174,8,Muted);Text(TEXT("积分"),Right+232,174,8,Muted);
    constexpr float RowHeight=24,RankY=189;
    // Keep the focus card and GM toggle separate; 1080p shows all twenty.
    RankVisibleRows=FMath::Clamp(FMath::FloorToInt((H-(H<620?456:580))/RowHeight),1,20);
    RankTotalRows=FMath::Min(20,static_cast<int32>(M.leaderboard.size()));
    RankOffset=FMath::Clamp(RankOffset,0,FMath::Max(0,RankTotalRows-RankVisibleRows));
    RankBounds=FBox2D({Right-6,RankY-3},{Right+266,RankY+RankVisibleRows*RowHeight});
    float MouseX=-1,MouseY=-1;if(GetOwningPlayerController())GetOwningPlayerController()->GetMousePosition(MouseX,MouseY);
    int32 HoveredFighter=-1;float HoveredY=RankY;
    for(int32 Row=0;Row<FMath::Min(RankVisibleRows,RankTotalRows-RankOffset);++Row){
        const int32 Index=RankOffset+Row;
        const auto& F=M.fighters[M.leaderboard[Index]];const auto* V=G->FindViewer(F.id);const float Y=RankY+Row*RowHeight;
        const FBox2D Bounds({Right-6,Y-2},{Right+266,Y+21});RankRegions.Add({Bounds,F.id});
        const bool Selected=F.id==G->FocusBodyId,Hover=Bounds.IsInside({MouseX,MouseY});
        if(Hover){HoveredFighter=M.leaderboard[Index];HoveredY=Y;}
        if(Selected||Hover||Index%2==0)Rect(Right-6,Y-2,272,23,FMath::Lerp(Panel,Selected?TeamColor(F.team):Border,Selected?.17f:(Hover?.38f:.15f)));
        if(Selected)Rect(Right-6,Y-2,2,23,TeamColor(F.team));
        Text(FString::Printf(TEXT("%02d"),Index+1),Right,Y+1,10,Muted);
        const bool Evolved=F.alive && F.evolutionRemaining>0,Buffed=F.alive && M.hasBossBuff(F);
        const float PortraitSize=Evolved||Buffed?11.f:18.f;
        if(V)Portrait(V->Avatar,Right+32-PortraitSize/2,Y+10-PortraitSize/2,PortraitSize);
        DrawStatusAura({Right+32,Y+10},6.8f,Evolved,Buffed,G->RunningTime,false);
        Rect(Right+45,Y+2,3,16,TeamColor(F.team));
        const FString FullName=V?V->Name:TEXT("观众"),ShortName=FitText(FullName,9,60);
        Text(ShortName,Right+55,Y+3,9,F.alive?Ink:Muted);
        gwui::DrawIcon(Canvas,WeaponIcon(F.weaponKind),{Right+126,Y+10},17,F.alive?Ink:Muted);
        if(Buffed)gwui::DrawIcon(Canvas,gwui::Icon::BossBuff,{Right+144,Y+10},12,FLinearColor(.57f,.17f,.95f,1));
        if(Evolved)gwui::DrawIcon(Canvas,gwui::Icon::EvolutionBuff,{Right+160,Y+10},12,FLinearColor(.96f,.32f,.025f,1));
        if(F.heroBuff)gwui::DrawIcon(Canvas,gwui::Icon::Hero,{Right+176,Y+10},12,Gold);
        if(F.temporaryWeaponRemaining>0)gwui::DrawIcon(Canvas,gwui::Icon::Clock,{Right+190,Y+10},10,Gold);
        const auto StatusColor=F.alive?FLinearColor(.23f,.47f,.33f,1):FLinearColor(.68f,.16f,.14f,1);
        if(!F.alive)Rect(Right+198,Y+2,23,16,FLinearColor(.92f,.77f,.72f,.6f));
        Text(F.alive?TEXT("存活"):TEXT("阵亡"),Right+201,Y+3,8,StatusColor);
        const FString Score=Number(F.score);const int32 ScoreSize=Score.Len()>6?7:9;
        Text(Score,Right+241,Y+2,ScoreSize,TeamColor(F.team),true);
    }
    if(RankTotalRows>RankVisibleRows)Text(FString::Printf(TEXT("滚轮查看  %02d–%02d / %02d"),RankOffset+1,FMath::Min(RankOffset+RankVisibleRows,RankTotalRows),RankTotalRows),Right,RankY+RankVisibleRows*RowHeight+1,9,Muted);
    else Text(TEXT("悬停查看完整昵称、武器与增益说明"),Right,RankY+RankVisibleRows*RowHeight+1,8,Muted);
    DrawFocusCard(G,Right,RankY+RankVisibleRows*RowHeight+17,272);
#if !UE_BUILD_SHIPPING
    if(!G->bShowControls && !G->GetBridge()->IsRelayMode())Text(TEXT("F1  打开 GM 本地模拟"),Right,H-96,10,Muted);
#endif
    Text(FString::Printf(TEXT("8000 × 8000  ·  %.1f×  ·  滚轮缩放 / 右键解锁 / 自由视角拖动"),G->CameraZoom),ArenaX,ArenaY+ArenaSide+13,Compact?8:10,Muted);
    if(M.phase==gw::Phase::Results)DrawResults(G);else DrawMinimap(G);
    if(Compact && M.phase==gw::Phase::Results) {
        const float Bottom=ArenaY+70;
        for(const auto& S:G->GetFeed().slots)if(S.active && S.age<.9){auto C=Panel;C.A=1.f-static_cast<float>(S.age/.9);Text(S.text.Left(24),ArenaX+20,Bottom-S.age*45,11,C);}
    }
    DrawGiftNotice(G);
    if(HoveredFighter>=0)DrawRankTooltip(G,HoveredFighter,Right-276,HoveredY-18,262);
    Line({32,H-48},{W-32,H-48},Border);
    Text((G->bRecording?G->DemoCaption:G->LastEvent).Left(Compact?41:64),32,H-34,Compact?9:11,Muted);
    Text(G->bRecording?FString::Printf(TEXT("第 %d 局  ·  演示速度 %d×"),M.round,FMath::RoundToInt(G->DemoSpeed)):FString::Printf(TEXT("%.0f FPS   SIM %.1f ms   %d×"),G->FrameMs>0?1000/G->FrameMs:0,G->SimulationMs,FMath::RoundToInt(G->DemoSpeed)),Right,H-34,10,Muted);
    G->RenderMs=(FPlatformTime::Seconds()-Start)*1000;
}
