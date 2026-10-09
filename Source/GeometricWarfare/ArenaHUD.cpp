#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "UI/ArenaCompactPresentation.h"
#include "ArenaUserSettings.h"
#include "UI/ArenaIcons.h"
#include "UI/ArenaHealthDisplay.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Math/ScaleMatrix.h"
#include "Math/TranslationMatrix.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "ImageUtils.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"

namespace {
const FLinearColor Ink(.035f,.065f,.08f,1),Muted(.30f,.36f,.38f,1),Border(.71f,.76f,.74f,1);
const FLinearColor Paper(.94f,.95f,.92f,1),Panel(.985f,.99f,.975f,1),Gold(.75f,.44f,.08f,1);
FLinearColor TeamColor(int32 T) { return T==1?FLinearColor(.78f,.09f,.075f,1):(T==2?FLinearColor(.045f,.26f,.72f,1):FLinearColor(.34f,.40f,.41f,1)); }
FString Number(int64 Value) { return FString::Printf(TEXT("%lld"),static_cast<long long>(Value)); }
FString ConnectionLabel(const FString& Status) {
    if(Status==TEXT("LOCAL_TEST"))return TEXT("本地测试");
    if(Status==TEXT("SDK_CONNECTED"))return TEXT("已连接");
    if(Status==TEXT("LAUNCH_TOKEN_MISSING"))return TEXT("等待伴侣启动");
    if(Status==TEXT("SDK_GIFT_MAPPING_MISSING"))return TEXT("礼物映射缺失");
    if(Status==TEXT("SDK_UNAVAILABLE"))return TEXT("平台适配器未就绪");
    if(Status==TEXT("SDK_CONNECTING"))return TEXT("连接中");
    if(Status==TEXT("SDK_HOST_MISSING"))return TEXT("平台宿主未安装");
    if(Status.Contains(TEXT("DISCONNECTED")))return TEXT("连接已断开");
    if(Status.Contains(TEXT("TIMEOUT")))return TEXT("连接超时");
    if(Status.Contains(TEXT("FAILED")) || Status.Contains(TEXT("ERROR")) || Status.Contains(TEXT("EXITED")))return TEXT("平台连接失败");
    if(Status==TEXT("SDK_EVENT_BACKPRESSURE"))return TEXT("消息队列繁忙");
    if(Status.StartsWith(TEXT("DEV_TEST_PROTOCOL")))return Status.EndsWith(TEXT("CONNECTED"))?TEXT("开发协议已连接"):TEXT("开发协议连接中");
    return TEXT("等待平台就绪");
}
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
}
void AArenaHUD::BeginPlay() {
    Super::BeginPlay(); TextFont=NewObject<UFont>(this); TextFont->FontCacheType=EFontCacheType::Runtime;
    if(auto* S=UArenaUserSettings::Get()){S->LoadSettings();S->ValidateSettings();S->ApplyNonResolutionSettings();}
#if !UE_BUILD_SHIPPING
    bSettingsOpen=FParse::Param(FCommandLine::Get(),TEXT("GWSettingsPreview"));
    bHostPanelOpen=FParse::Param(FCommandLine::Get(),TEXT("GWHostPanelPreview"));
#endif
    TextFont->GetMutableInternalCompositeFont()=FCompositeFont(FName(TEXT("Regular")),FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    if(FParse::Param(FCommandLine::Get(),TEXT("GWGMPreview")))ToggleGM();
    FString RuleStickerPath;
    if(FParse::Value(FCommandLine::Get(),TEXT("GWRuleSticker="),RuleStickerPath)) {
        RuleSticker=FImageUtils::ImportFileAsTexture2D(RuleStickerPath);
    }

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
    if(!Bounds.bIsValid || ArenaW()<=0 || ArenaH()<=0)return FBox2D(ForceInit);
    const FVector2D Min(FMath::Max(Bounds.Min.X,static_cast<double>(ArenaX)),FMath::Max(Bounds.Min.Y,static_cast<double>(ArenaY)));
    const FVector2D Max(FMath::Min(Bounds.Max.X,static_cast<double>(ArenaX+ArenaW())),FMath::Min(Bounds.Max.Y,static_cast<double>(ArenaY+ArenaH())));
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
bool AArenaHUD::InArena(FVector2D P,float M)const{return P.X>=ArenaX+M && P.Y>=ArenaY+M && P.X<=ArenaX+ArenaW()-M && P.Y<=ArenaY+ArenaH()-M;}
void AArenaHUD::SetupArenaView(AArenaGameMode* Game) {
    ArenaWidth=FMath::Max(1.f,ArenaW());ArenaHeight=FMath::Max(1.f,ArenaH());ArenaSide=FMath::Min(ArenaWidth,ArenaHeight);
    gw::ArenaView View(Game->CameraZoom,{Game->CameraCenter.X,Game->CameraCenter.Y},ArenaWidth/ArenaHeight);
    Game->CameraZoom=View.zoom;Game->CameraCenter={View.center.x,View.center.y};
    const auto Extent=View.visibleExtent();WorldScale=ArenaWidth/Extent.x;
    ViewOrigin=Game->CameraCenter-FVector2D(Extent.x*.5,Extent.y*.5);
}
void AArenaHUD::HealthBar(float X,float Y,float Width,float Height,double Hp,double MaxHp,FLinearColor Color,int32 FontSize) {
    const float Fraction=MaxHp>0?FMath::Clamp(Hp/MaxHp,0.0,1.0):0;
    Rect(X,Y,Width,Height,Border);Rect(X+1,Y+1,Width-2,Height-2,Panel);
    Rect(X+1,Y+1,(Width-2)*Fraction,Height-2,FMath::Lerp(Panel,Color,.42f));
    if(FontSize>0) {
        const auto Current=gwui::HealthDisplay(Hp),Maximum=gwui::HealthDisplay(MaxHp);
        Text(FString(UTF8_TO_TCHAR(Current.c_str()))+TEXT(" / ")+UTF8_TO_TCHAR(Maximum.c_str()),X+Width/2,Y+(Height-FontSize*1.4f)/2,FontSize,Ink,true);
    }
}
void AArenaHUD::ScoreBar(float X,float Y,float Width,float Height,int64 Score,int64 MaxScore,FLinearColor Color,int32 FontSize) {
    const float U=1.f/FMath::Max(ScreenLayout.scale,.1f),Padding=2*U;
    const double Fraction=FMath::Clamp(static_cast<double>(Score)/static_cast<double>(FMath::Max<int64>(1,MaxScore)),0.0,1.0);
    Rect(X,Y,Width,Height,FMath::Lerp(Panel,Color,.55f));
    Rect(X+U,Y+U,Width-2*U,Height-2*U,FLinearColor(.87f,.90f,.91f,1));
    Rect(X+U,Y+U,(Width-2*U)*Fraction,Height-2*U,FMath::Lerp(Panel,Color,.48f));
    const FString Value=FString::Printf(TEXT("%lld"),static_cast<long long>(Score));
    const float DPI=Canvas&&Canvas->Canvas?FMath::Max(Canvas->Canvas->GetDPIScale(),.01f):1.f;
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    FVector2D Extent=Measure->Measure(Value,FSlateFontInfo(TextFont,FontSize),DPI)/DPI;
    // Keep the full integer even for an exceptional 19-digit score in a narrow cell.
    while(FontSize>1&&(Extent.X>Width-2*Padding||Extent.Y>Height-2*Padding)){
        --FontSize;Extent=Measure->Measure(Value,FSlateFontInfo(TextFont,FontSize),DPI)/DPI;
    }
    Text(Value,X+(Width-Extent.X)/2,Y+(Height-Extent.Y)/2,FontSize,Ink);
}
float AArenaHUD::DrawRecruitSticker(float X,float Y,float Width,bool MeasureOnly) {
    const float Scale=FMath::Max(ScreenLayout.scale,.1f);
    const bool Compact=ScreenLayout.mobile||ScreenLayout.portrait;
    const float Padding=(Compact?6.f:10.f)/FMath::Min(Scale,1.f),Gap=(Compact?2.f:5.f)/FMath::Min(Scale,1.f);
    const int32 TitleSize=FMath::Max(14,FMath::CeilToInt(13.f/Scale));
    const int32 BodySize=FMath::Max(11,FMath::CeilToInt(10.f/Scale));
    struct FStickerLine {FString Value;float Top;int32 Size;FLinearColor Color;};
    TArray<FStickerLine,TInlineAllocator<12>> Lines;
    float Cursor=Padding;
    const auto Add=[&](const TCHAR* Value,int32 Size,FLinearColor Color){
        FString Remaining(Value);
        while(!Remaining.IsEmpty()){
            FString Part=FitText(Remaining,Size,Width-2*Padding,false);
            if(Part.IsEmpty())Part=Remaining.Left(1);
            Lines.Add({Part,Cursor,Size,Color});Cursor+=Size*1.65f;
            Remaining=Remaining.RightChop(Part.Len()).TrimStart();
        }
        Cursor+=Gap;
    };
    const FLinearColor Accent(.045f,.26f,.58f,1);
    Add(TEXT("一键摇人 · 召集好友"),TitleSize,Accent);
    Add(TEXT("小摇杆 → 召集 → 发起召集"),BodySize,Ink);
    Add(TEXT("邀请好友来直播间，一起加入战局"),BodySize,Muted);
    const float Height=Cursor-Gap+Padding;
    if(!MeasureOnly){
        Rect(X,Y,Width,Height,FLinearColor(.88f,.93f,.95f,1));
        Rect(X,Y,3.f/FMath::Min(Scale,1.f),Height,Accent);
        for(const auto& L:Lines)Text(L.Value,X+Padding,Y+L.Top,L.Size,L.Color);
    }
    return Height;
}
float AArenaHUD::DrawGiftRules(AArenaGameMode* Game,float X,float Y,float Width,bool Grid,bool MeasureOnly) {
    const float Scale=FMath::Max(ScreenLayout.scale,.1f),U=1.f/FMath::Min(Scale,1.f);
    const FLinearColor RuleText(.13f,.18f,.20f,1);
    const float Padding=4*U,Gap=4*U,IconSize=FMath::Max(28.f,24.f/Scale);
    const int32 TitleSize=FMath::Max(12,FMath::CeilToInt(12.f/Scale)),BodySize=FMath::Max(10,FMath::CeilToInt(10.f/Scale));
    const float TitleLine=TitleSize*1.55f,BodyLine=BodySize*1.45f;
    const TCHAR* Names[]={TEXT("仙女棒"),TEXT("能力药丸"),TEXT("魔法镜"),TEXT("甜甜圈"),TEXT("能量电池")};
    const TCHAR* Effects[]={
        TEXT("阵亡：复活\n存活：基础生命上限+30/个"),
        TEXT("基地已毁：重建\n基地仍在：生命+1000/个"),
        TEXT("狙击枪·巴雷特"),TEXT("机枪·加特林"),TEXT("火箭筒")};
    struct FRuleCard {float Left=0,Top=0,Width=0,Height=0;bool InlineWeapon=false;TArray<FString> Lines;};
    FRuleCard Cards[5];
    const auto Wrap=[&](const FString& Value,float Available){
        TArray<FString> Lines,Paragraphs;Value.ParseIntoArrayLines(Paragraphs,false);
        for(FString Remaining:Paragraphs)while(!Remaining.IsEmpty()){
            FString Part=FitText(Remaining,BodySize,Available,false);if(Part.IsEmpty())Part=Remaining.Left(1);
            Lines.Add(Part);Remaining=Remaining.RightChop(Part.Len()).TrimStart();
        }
        return Lines;
    };
    float Cursor=TitleLine+Gap;
    const auto Row=[&](int32 First,int32 Count){
        const float CardWidth=(Width-Gap*(Count-1))/Count;float Height=0;
        for(int32 I=First;I<First+Count;++I){
            auto& Card=Cards[I];Card.Left=(I-First)*(CardWidth+Gap);Card.Top=Cursor;Card.Width=CardWidth;
            Card.InlineWeapon=I>=2&&!Grid&&CardWidth*Scale>=240;
            Card.Lines=Wrap(Effects[I],Card.InlineWeapon?CardWidth-2*Padding-IconSize-Gap-TitleSize*5.5f:CardWidth-2*Padding);
            Card.Height=2*Padding+(Card.InlineWeapon?FMath::Max(IconSize,Card.Lines.Num()*BodyLine):FMath::Max(IconSize,TitleLine)+2*U+Card.Lines.Num()*BodyLine);Height=FMath::Max(Height,Card.Height);
        }
        for(int32 I=First;I<First+Count;++I)Cards[I].Height=Height;
        Cursor+=Height+Gap;
    };
    if(Grid){Row(0,2);Row(2,3);}else for(int32 I=0;I<5;++I)Row(I,1);
    const auto Notes=Wrap(TEXT("仙女棒强化死亡清零；计入进化/英雄上限。\n仙女棒/药丸需入场；仙女棒结算不可用。\n药丸仅红蓝队，冲刺/结算不可用。\n批量首个复活/重建，余量按存活/仍在强化。\n武器永久解锁左手；重复赠送不补弹。"),Width-2*Padding);
    const float NotesTop=Cursor+2*U,Height=NotesTop+Notes.Num()*BodyLine+Padding;
    if(!MeasureOnly){
        Text(TEXT("礼物互动 · 图标对应效果"),X,Y,TitleSize,Ink);
        for(int32 I=0;I<5;++I){
            const auto& Card=Cards[I];const float CX=X+Card.Left,CY=Y+Card.Top,TX=CX+Padding+IconSize+Gap;
            Rect(CX,CY,Card.Width,Card.Height,FLinearColor(.985f,.99f,.975f,1));
            if(auto* Icon=Game->GetGiftIcon(Names[I]);Icon&&Icon->GetResource()){
                const float Ratio=static_cast<float>(Icon->GetSizeX())/FMath::Max(1,Icon->GetSizeY());
                const float IW=Ratio>=1?IconSize:IconSize*Ratio,IH=Ratio>=1?IconSize/Ratio:IconSize;
                FCanvasTileItem Item({CX+Padding+(IconSize-IW)/2,CY+Padding+(IconSize-IH)/2},Icon->GetResource(),{IW,IH},FLinearColor::White);
                Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
            }
            Text(Names[I],TX,CY+Padding+(IconSize-TitleLine)/2,TitleSize,Ink);
            const float BodyX=Card.InlineWeapon?TX+TitleSize*5.5f:CX+Padding;
            const float BodyY=CY+Padding+(Card.InlineWeapon?(IconSize-BodyLine)/2:FMath::Max(IconSize,TitleLine)+2*U);
            for(int32 L=0;L<Card.Lines.Num();++L)Text(Card.Lines[L],BodyX,BodyY+L*BodyLine,BodySize,RuleText);
        }
        for(int32 I=0;I<Notes.Num();++I)Text(Notes[I],X+Padding,Y+NotesTop+I*BodyLine,BodySize,RuleText);
    }
    return Height;
}
void AArenaHUD::DrawFocusCard(AArenaGameMode* G,float X,float Y,float Width,bool Compact) {
    const auto& M=G->GetMatch();const bool Short=Compact||ViewportHeight<620;
    Rect(X-6,Y,Width,Short?62:206,Panel);
    const auto* F=M.findFighter(G->FocusBodyId);
    if(Short) {
        if(!F){Text(TEXT("点击榜单或角色 · 锁定视角"),X+10,Y+12,10,Muted);return;}
        const auto C=TeamColor(F->team);
        const bool Evolved=F->alive&&F->evolutionRemaining>0,Buffed=F->alive&&M.hasBossBuff(*F);
        const float PortraitSize=Evolved||Buffed?12.f:20.f;
        if(const auto* V=G->FindViewer(F->id))Portrait(V->Avatar,X+14-PortraitSize/2,Y+21-PortraitSize/2,PortraitSize);
        DrawStatusAura({X+14,Y+21},8,Evolved,Buffed,G->RunningTime,false,F->alive&&F->heroBuff);
        Text(G->ViewerName(F->id).Left(5),X+36,Y+4,10,Ink);
        const FString Equipped=FString(TEXT("左手 · "))+WeaponLabel(F->weaponKind);
        Text(F->alive?Equipped:TEXT("阵亡 · 等待复活"),X+36,Y+23,8,F->alive?Muted:C);
        Text(F->temporaryWeaponRemaining>0?FString::Printf(TEXT("右手 · %s %.0fs"),WeaponLabel(F->temporaryWeaponKind),F->temporaryWeaponRemaining):TEXT("右手 · 未拾取"),X+36,Y+42,8,Gold);
        HealthBar(X+126,Y+12,Width-143,18,F->hp,F->maxHp,C,8);return;
    }
    if(!F) {
        gwui::DrawIcon(Canvas,gwui::Icon::Crosshair,{X+24,Y+35},26,Muted);
        Text(TEXT("点击榜单或角色"),X+50,Y+16,13,Ink);
        Text(TEXT("锁定视角 · 查看观众数据"),X+50,Y+42,10,Muted);
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
    Text(FString(TEXT("左手 · "))+WeaponLabel(F->weaponKind),X+48,Y+91,10,Ink);
    Text(FString::Printf(TEXT("%d / %d"),F->ammo,M.weaponFor(*F).magazine),X+62,Y+108,10,Muted);
    if(F->alive) {
        gwui::DrawIcon(Canvas,F->reloadRemaining>0?gwui::Icon::Reload:gwui::Icon::Crosshair,{X+150,Y+105},16,C);
        Text(F->reloadRemaining>0?FString::Printf(TEXT("%.1fs"),F->reloadRemaining):TEXT("AUTO"),X+166,Y+97,11,Muted);
    } else {
        const bool Auto=M.phase!=gw::Phase::Results && (F->isHost || (M.phase==gw::Phase::Battle && (F->team==0 || M.bases[F->team].alive)));
        Text(Auto?FString::Printf(TEXT("%.1fs 复活"),F->respawnRemaining):TEXT("等待复活"),X+137,Y+97,10,Muted);
    }
    if(F->temporaryWeaponRemaining>0) {
        gwui::DrawIcon(Canvas,WeaponIcon(F->temporaryWeaponKind),{X+19,Y+146},24,Gold);
        Text(FString::Printf(TEXT("右手 · %s · %.0fs"),WeaponLabel(F->temporaryWeaponKind),F->temporaryWeaponRemaining),X+48,Y+132,10,Gold);
        Text(FString::Printf(TEXT("%d / %d"),F->rightWeapon.ammo,M.weaponFor(*F,true).magazine),X+62,Y+149,10,Muted);
        Text(F->rightWeapon.reloadRemaining>0?FString::Printf(TEXT("换弹 %.1fs"),F->rightWeapon.reloadRemaining):TEXT("AUTO"),X+150,Y+149,10,Muted);
    } else Text(TEXT("右手 · 未拾取武器箱"),X+12,Y+137,10,Muted);
    gwui::DrawIcon(Canvas,gwui::Icon::Skull,{X+19,Y+183},18,Muted);
    Text(FString::FromInt(F->kills),X+38,Y+174,14,Ink);
    gwui::DrawIcon(Canvas,gwui::Icon::Score,{X+135,Y+183},18,Gold);
    Text(F->isHost?TEXT("助战"):Number(F->score),X+154,Y+174,15,Gold);
}
void AArenaHUD::DrawMinimap(AArenaGameMode* G) {
    if(gwui::IsFullMapOverview(G->CameraZoom)&&FMath::IsNearlyEqual(ArenaW(),ArenaH(),1.f)){MinimapBounds=FBox2D(ForceInit);return;}
    const float Size=FMath::Min(ArenaSide>=700?168.f:122.f,FMath::Min(ArenaW(),ArenaH())*.24f);
    const float X=ArenaX+ArenaW()-Size-14,Y=ArenaY+ArenaH()-Size-14;
    MinimapBounds=FBox2D({X,Y},{X+Size,Y+Size});
    const float PixelScale=FMath::Max(ScreenLayout.scale,.1f);
    const int32 CaptionFont=FMath::CeilToInt(9/PixelScale);
    const float CaptionHeight=CaptionFont*1.55f+4/PixelScale;
    Rect(X-5,Y-CaptionHeight,Size+10,Size+CaptionHeight+5,FLinearColor(.985f,.99f,.975f,.96f));
    Text(FitText(G->FocusBodyId>=0?TEXT("锁定"):TEXT("全图"),CaptionFont,Size),X,Y-CaptionHeight+2/PixelScale,CaptionFont,Muted);
    if(G->RunningTime>=MinimapRefreshAt) {
        MinimapCells.fill(0);const auto& M=G->GetMatch();
        for(size_t i=0;i<M.world.bodies.size();++i)if(M.fighters[i].alive) {
            const auto& P=M.world.bodies[i].position;
            const int32 CellX=FMath::Clamp(static_cast<int32>(P.x*64/gw::World::Size),0,63);
            const int32 CellY=FMath::Clamp(static_cast<int32>(P.y*64/gw::World::Size),0,63);
            MinimapCells[CellY*64+CellX]|=1<<M.fighters[i].team;
        }
        MinimapRefreshAt=G->RunningTime+Presentation.minimapInterval;
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
    const FVector2D A(X+ViewOrigin.X/gw::World::Size*Size,Y+ViewOrigin.Y/gw::World::Size*Size);
    const FVector2D B=A+FVector2D(ArenaW()/WorldScale,ArenaH()/WorldScale)/gw::World::Size*Size;
    Line(A,{B.X,A.Y},Ink,1.5f);Line({B.X,A.Y},B,Ink,1.5f);Line(B,{A.X,B.Y},Ink,1.5f);Line({A.X,B.Y},A,Ink,1.5f);
    if(const auto* Body=G->GetArena().find(G->FocusBodyId))
        gwui::DrawIcon(Canvas,gwui::Icon::Crosshair,{X+Body->position.x/gw::World::Size*Size,Y+Body->position.y/gw::World::Size*Size},12,Gold);
}
void AArenaHUD::DrawDamage(AArenaGameMode* G) {
    const auto& Slots=G->GetDamageNumbers().slots;
    int32 Drawn=0;
    // Focus/host feedback survives a zero cosmetic budget and is drawn first.
    for(int32 Pass=0;Pass<2;++Pass)for(const auto& S:Slots)if(S.active) {
        const auto* Fighter=S.targetKind==1?G->GetMatch().findFighter(S.targetId):nullptr;
        const bool Priority=Fighter && (S.targetId==G->FocusBodyId || Fighter->isHost);
        if(Priority!=(Pass==0) || (!Priority && Drawn>=Presentation.damageNumbers))continue;
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
        Item.bCentreX=true;Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);++Drawn;
    }
}
void AArenaHUD::DrawArena(AArenaGameMode* Game) {
    const auto& M=Game->GetMatch();
    // Detail thresholds use final pixels, even when the mobile composition is
    // drawn in a smaller logical coordinate system. Simulation size is unchanged.
    const float PixelScale=WorldScale*ScreenLayout.scale;
    UpdateArenaShake(Game);
    Rect(ArenaX+5,ArenaY+7,ArenaW(),ArenaH(),FLinearColor(.6f,.66f,.62f,.18f));
    Rect(ArenaX,ArenaY,ArenaW(),ArenaH(),Panel);
    for(int i=0;i<=32;++i) {
        auto P=Project(i*250,ViewOrigin.Y),Q=Project(ViewOrigin.X,i*250);
        if(InArena({P.X,ArenaY+1}))Line({P.X,ArenaY},{P.X,ArenaY+ArenaH()},FLinearColor(.80f,.84f,.80f,.25f));
        if(InArena({ArenaX+1,Q.Y}))Line({ArenaX,Q.Y},{ArenaX+ArenaW(),Q.Y},FLinearColor(.80f,.84f,.80f,.25f));
    }
    DrawCombatGround(Game);
    for(const auto& O:M.orbs)if(O.active) {
        const auto P=Project(O.position.x,O.position.y);
        const float R=FMath::Clamp(WorldScale*(O.natural?7.f:11.f),1.6f,5.f);
        if(!ClipArenaRect(FBox2D(P-FVector2D(R),P+FVector2D(R))).bIsValid)continue;
        const FLinearColor C=O.natural?FLinearColor(.12f,.52f,.28f,1):Gold;
        if(PixelScale<.26f) ArenaRect(P.X-R,P.Y-R,R*2,R*2,C);
        else {TArray<FVector2D> Ball;for(int32 i=0;i<12;++i)Ball.Add(P+FVector2D(FMath::Cos(i*PI/6),FMath::Sin(i*PI/6))*R);ArenaPolygon(Ball,C);}
    }
    for(const auto& N:M.npcs)if(N.active) {
        const auto P=Project(N.position.x,N.position.y);
        const float R=FMath::Max(32*WorldScale,4.f);
        if(!ClipArenaRect(FBox2D(P-FVector2D(R),P+FVector2D(R))).bIsValid)continue;
        TArray<FVector2D> V;for(int i=0;i<6;++i)V.Add(P+FVector2D(FMath::Cos(i*PI/3),FMath::Sin(i*PI/3))*R);
        ArenaPolygon(V,FMath::Lerp(FLinearColor(.64f,.59f,.40f,1),FLinearColor::White,FMath::Clamp(static_cast<float>(N.hitFlash/.16),0.f,1.f)));for(int i=0;i<6;++i)ArenaLine(V[i],V[(i+1)%6],Gold,N.hitFlash>0?2.5f:1.5f);
        if(!gwui::IsFullMapOverview(Game->CameraZoom) && PixelScale>.3f && InArena({P.X-30,P.Y+R+5}) && InArena({P.X+30,P.Y+R+17}))HealthBar(P.X-30,P.Y+R+5,60,12,N.hp,N.maxHp,Gold,8);
    }
    for(int T=1;T<=2;++T) {
        const auto& B=M.bases[T];const auto P=Project(B.position.x,B.position.y);
        const float R=FMath::Clamp(100*WorldScale,17.f,58.f);const auto C=TeamColor(T);
        if(!ClipArenaRect(FBox2D(P-FVector2D(R),P+FVector2D(R))).bIsValid)continue;
        ArenaRect(P.X-R,P.Y-R,R*2,R*2,B.alive?FMath::Lerp(Panel,C,.15f):FLinearColor(.75f,.77f,.74f,1));
        if(B.alive){ArenaRect(P.X-R*.5f,P.Y-R*.5f,R,R,C);ArenaLine(P+FVector2D(-R,-R),P+FVector2D(R,-R),C,3);}
        else{ArenaLine(P+FVector2D(-R,-R),P+FVector2D(R,R),Muted,2);ArenaLine(P+FVector2D(R,-R),P+FVector2D(-R,R),Muted,2);}
        if(InArena({P.X-36,P.Y+R+7})&&InArena({P.X+36,P.Y+R+23}))Text(T==1?TEXT("红方基地"):TEXT("蓝方基地"),P.X,P.Y+R+7,11,C,true);
        if(PixelScale>.26f && InArena({P.X-48,P.Y-R-20})&&InArena({P.X+48,P.Y-R-4}))HealthBar(P.X-48,P.Y-R-20,96,16,B.hp,B.maxHp,C,9);
        else {ArenaRect(P.X-R,P.Y-R-8,R*2,3,Border);ArenaRect(P.X-R,P.Y-R-8,R*2*B.hp/B.maxHp,3,C);}
    }
    int32 Labels=0,Bars=0;
    for(size_t Index=0;Index<M.world.bodies.size();++Index) {
        const auto& B=M.world.bodies[Index];const auto& F=M.fighters[Index];
        if(!F.alive)continue;
        const auto P=Project(B.position.x,B.position.y);
        const FLinearColor C=TeamColor(F.team);const float Scale=WorldScale*B.scale;
        // A visible body, barrel or aura survives even when its center crosses
        // a viewport edge. The primitives below clip their actual geometry.
        const float Reach=FMath::Max(5.f,90*Scale);
        if(!ClipArenaRect(FBox2D(P-FVector2D(Reach),P+FVector2D(Reach))).bIsValid)continue;
        DrawHealingEffect(F,P,Scale);
        const bool Focused=B.id==Game->FocusBodyId;
        const bool Priority=Focused || F.isHost || F.heroBuff;
        const auto Detail=gwui::ResolveCompactActorDetail(PixelScale,Game->CameraZoom,Priority);
        if(Detail.markerOnly) {
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
        const auto BaseFill=FMath::Lerp(Panel,C,F.isHost?.32f:gwui::IsFullMapOverview(Game->CameraZoom)?.48f:.16f);
        const auto Fill=FMath::Lerp(F.heroBuff?FMath::Lerp(BaseFill,Gold,.20f):BaseFill,FLinearColor::White,FMath::Clamp(static_cast<float>(F.hitFlash/.18),0.f,1.f));
        if(F.isHost){for(int32 i=0;i<10;++i)ArenaPolygon({P,Points[i],Points[(i+1)%10]},Fill);}else ArenaPolygon(Points,Fill);
        for(int32 i=0;i<Points.Num();++i)ArenaLine(Points[i],Points[(i+1)%Points.Num()],F.hitFlash>0?Gold:C,F.hitFlash>0?2.8f:1.5f);
        if(F.hitFlash>0 && (!Presentation.reduced || Focused || F.isHost || F.heroBuff))ArenaRing(P,(28+(1-F.hitFlash/.18)*16)*Scale,FLinearColor(C.R,C.G,C.B,F.hitFlash/.18*.5),1.5f,16);
        DrawFighterWeapons(F,P,Scale,M);
        const auto* V=Game->FindViewer(B.id);
        if(V && (Priority || !gwui::IsFullMapOverview(Game->CameraZoom))){const float R=(B.shape==gw::Shape::Triangle?8.f:12.f)*WorldScale;ArenaPortrait(V->Avatar,P.X-R,P.Y-R,R*2);}
        else if(F.isHost && InArena(P,8*Scale))gwui::DrawIcon(Canvas,gwui::Icon::Score,P,14*Scale,Gold);
        const float Width=FMath::Max(F.isHost?74.f:56.f,66*WorldScale),BarX=FMath::Clamp(P.X-Width/2,ArenaX+2,ArenaX+ArenaW()-Width-2),BarY=P.Y+34*Scale;
        if(Detail.showBars && BodyVisible && (Bars<Presentation.labels || Priority) && BarY>=ArenaY && BarY+20<ArenaY+ArenaH()){HealthBar(BarX,BarY,Width,13,F.hp,F.maxHp,C,8);if(F.armor>0){Rect(BarX,BarY+15,Width,3,Border);Rect(BarX,BarY+15,Width*FMath::Clamp(F.armor/FMath::Max(F.maxArmor,1.),0.,1.),3,FLinearColor(.12f,.55f,.64f,1));}if(!Priority)++Bars;}
        const FVector2D LabelPoint(P.X,P.Y-36*Scale-11);
        const bool ShowLabel=(V||F.isHost) && (Labels<Presentation.labels||Priority) && Detail.showLabel && LabelPoint.X>=ArenaX+55 && LabelPoint.X<=ArenaX+ArenaW()-55 && LabelPoint.Y>=ArenaY+2 && LabelPoint.Y+17<=ArenaY+ArenaH();
        if(ShowLabel) {Text(F.isHost?TEXT("主播 · 助战"):V->Name.Left(7),LabelPoint.X,LabelPoint.Y,11,F.isHost?Gold:Ink,true);if(!Priority)++Labels;}
        if(Focused) {
            // Screen-space arrow stays six pixels above the name at every zoom.
            if(ShowLabel && LabelPoint.Y-14>=ArenaY+2)ArenaPolygon({LabelPoint+FVector2D(-6,-14),LabelPoint+FVector2D(6,-14),LabelPoint+FVector2D(0,-6)},Gold);
            if(InArena({P.X-28,BarY+22})&&InArena({P.X+28,BarY+38}))Text(TEXT("正在跟随"),P.X,BarY+22,10,Gold,true);
        }
    }
    DrawGunfire(Game);DrawCombatActors(Game);
    Line({ArenaX,ArenaY},{ArenaX+ArenaW(),ArenaY},Border,2);Line({ArenaX,ArenaY},{ArenaX,ArenaY+ArenaH()},Border,2);
    Line({ArenaX+ArenaW(),ArenaY},{ArenaX+ArenaW(),ArenaY+ArenaH()},Border,2);Line({ArenaX,ArenaY+ArenaH()},{ArenaX+ArenaW(),ArenaY+ArenaH()},Border,2);
    DrawDamage(Game);
    ArenaShakeOffset=FVector2D::ZeroVector;
    if(Game->IsSimulationPaused())Text(TEXT("已暂停"),ArenaX+ArenaW()/2,ArenaY+20,24,Ink,true);
}
void AArenaHUD::DrawResults(AArenaGameMode* Game) {
    const auto& M=Game->GetMatch();
    const float PixelScale=FMath::Max(ScreenLayout.scale,.1f);
    if(ArenaW()*PixelScale<600 || ArenaH()*PixelScale<500) {
        // Keep award identities and values readable inside the battlefield;
        // shrinking the desktop report also shrinks Chinese text below legibility.
        const float U=1.f/PixelScale;
        const int32 TitleFont=FMath::CeilToInt(18*U),BodyFont=FMath::CeilToInt(10*U);
        const float TitleLine=TitleFont*1.5f,BodyLine=BodyFont*1.4f;
        const bool Columns=ArenaH()*PixelScale<160;
        const float Padding=(Columns?2.f:4.f)*U,Gap=6*U;
        const float HeaderHeight=TitleLine+(Columns?0:BodyLine+3*U);
        const float RowHeight=Columns?2*BodyLine+2*U:BodyLine+6*U;
        const float ContentHeight=2*Padding+HeaderHeight+(Columns?2:4)*RowHeight+BodyLine+4*U;
        const float X=ArenaX+8*U,Y=ArenaY+FMath::Max(0.f,(ArenaH()-ContentHeight)*.5f),W=ArenaW()-16*U;
        if(ResultsRound!=M.round){ResultsRound=M.round;ResultsSeenAt=Game->RunningTime;}
        Rect(ArenaX,ArenaY,ArenaW(),ArenaH(),FLinearColor(.025f,.045f,.05f,.94f));
        Text(M.winnerTeam==1?TEXT("红方获胜"):TEXT("蓝方获胜"),X+W/2,Y+Padding,TitleFont,TeamColor(M.winnerTeam),true);
        if(!Columns)Text(FitText(TEXT("本局战报 · ")+Number(M.teamScores[1])+TEXT(" : ")+Number(M.teamScores[2]),BodyFont,W),X+W/2,Y+Padding+TitleLine,BodyFont,Panel,true);
        const float Top=Y+Padding+HeaderHeight,CellWidth=Columns?(W-Gap)/2:W;
        const float DPI=Canvas->Canvas->GetDPIScale();
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const float LabelWidth=FMath::Max(48*U,Measure->Measure(TEXT("承伤最多"),FSlateFontInfo(TextFont,BodyFont),DPI).X/FMath::Max(.01f,DPI)+10*U);
        const auto AwardRow=[&](const TCHAR* Label,int32 Id,int32 Kind,int32 Index) {
            const float Left=X+(Columns?Index%2*(CellWidth+Gap):0),RY=Top+(Columns?Index/2:Index)*RowHeight;
            const auto* F=M.findFighter(Id);
            const FString Value=!F?TEXT("暂无战绩"):Kind==2?FString::Printf(TEXT("%.0f 承伤"),F->damageTaken):Kind==1?FString::Printf(TEXT("%d 次击败"),F->kills):Number(F->score)+TEXT(" 分");
            const FLinearColor Accent=Kind==2?FLinearColor(.32f,.73f,.76f,1):Gold;
            Rect(Left,RY,CellWidth,RowHeight-U,FLinearColor(.075f,.105f,.12f,1));
            const float NameX=Left+LabelWidth;
            Text(Label,Left+4*U,RY+(Columns?1.f:3.f)*U,BodyFont,Accent);
            if(Columns) {
                Text(FitText(Game->ViewerName(Id),BodyFont,CellWidth-LabelWidth-4*U),NameX,RY+U,BodyFont,Panel);
                Text(FitText(Value,BodyFont,CellWidth-8*U),Left+4*U,RY+BodyLine+U,BodyFont,Panel);
            } else {
                const float ValueWidth=Measure->Measure(Value,FSlateFontInfo(TextFont,BodyFont),DPI).X/FMath::Max(.01f,DPI);
                const float ValueX=Left+CellWidth-4*U-ValueWidth;
                Text(FitText(Game->ViewerName(Id),BodyFont,FMath::Max(0.f,ValueX-NameX-6*U)),NameX,RY+3*U,BodyFont,Panel);
                Text(Value,ValueX,RY+3*U,BodyFont,Panel);
            }
        };
        AwardRow(TEXT("MVP"),M.mvpId,0,0);AwardRow(TEXT("FMVP"),M.fmvpId,0,1);
        AwardRow(TEXT("击杀最多"),M.mostKillsId,1,2);AwardRow(TEXT("承伤最多"),M.mostDamageTakenId,2,3);
        const float FooterY=Top+(Columns?2:4)*RowHeight+2*U;
        Text(FString::Printf(TEXT("下一局 %02d 秒后开始"),FMath::CeilToInt(M.intermissionRemaining)),X+W/2,FooterY,BodyFont,Panel,true);
        return;
    }
    const float S=FMath::Min3(ArenaW()/850.f,ArenaH()/660.f,1.f);
    // Small arenas must scale the complete report, including its minimum font sizes.
    const auto ResultsFont=[S](int32 Size,int32 Minimum=0){return FMath::Max(1,FMath::Max(FMath::RoundToInt(Size*S),FMath::FloorToInt(Minimum*FMath::Min(1.f,S/.55f))));};
    const int32 FooterSize=ResultsFont(12,8);
    const float ContentHeight=610*S+FooterSize*1.65f;
    const float X=ArenaX+32*S,Y=ArenaY+(ArenaH()-ContentHeight)*.5f,W=ArenaW()-64*S;
    if(ResultsRound!=M.round){ResultsRound=M.round;ResultsSeenAt=Game->RunningTime;}
    const float Age=FMath::Max(0.f,Game->RunningTime-ResultsSeenAt);
    Rect(ArenaX,ArenaY,ArenaW(),ArenaH(),FLinearColor(.025f,.045f,.05f,.88f));
    const auto C=TeamColor(M.winnerTeam);
    Text(FString::Printf(TEXT("ROUND %02d  /  本局战报"),M.round),X+W/2,Y,ResultsFont(14,10),FLinearColor(.72f,.78f,.73f,1),true);
    Text(M.winnerTeam==1?TEXT("红方获胜"):TEXT("蓝方获胜"),X+W/2,Y+36*S,ResultsFont(46),C,true);
    Text(Number(M.teamScores[1])+TEXT("  :  ")+Number(M.teamScores[2]),X+W/2,Y+110*S,ResultsFont(30),Panel,true);
    const float Gap=24*S,CardY=Y+170*S,CardW=(W-Gap)/2;
    auto Award=[&](const TCHAR* Title,const TCHAR* Reason,int32 Id,float Left) {
        Rect(Left,CardY,CardW,200*S,FLinearColor(.075f,.105f,.12f,1));
        Text(Title,Left+CardW/2,CardY+17*S,ResultsFont(24),Gold,true);
        const auto* V=Game->FindViewer(Id);if(V)Portrait(V->Avatar,Left+CardW/2-23*S,CardY+58*S,46*S);
        Text(Game->ViewerName(Id).Left(10),Left+CardW/2,CardY+115*S,ResultsFont(19),Panel,true);
        Text(Reason,Left+CardW/2,CardY+149*S,ResultsFont(11,8),FLinearColor(.64f,.72f,.70f,1),true);
        if(const auto* F=M.findFighter(Id))Text(Number(F->score)+FString::Printf(TEXT(" 分  ·  %d 击败"),F->kills),Left+CardW/2,CardY+174*S,ResultsFont(12,8),Panel,true);
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
        Text(Title,Center,Top+12*S,ResultsFont(18,12),Tint(Accent),true);
        if(F) {
            const float PortraitSize=35*S,PX=Left+21*S,PY=Top+49*S;
            if(V&&V->Avatar&&V->Avatar->GetResource()) {
                FCanvasTileItem Item(FVector2D(PX,PY),V->Avatar->GetResource(),FVector2D(PortraitSize),Tint(FLinearColor::White));
                Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
            }
            const float NameX=PX+PortraitSize+11*S;
            const int32 NameSize=ResultsFont(15,10);
            Text(FitText(Game->ViewerName(Id),NameSize,Left+CardW-NameX-16*S),NameX,PY-2*S,NameSize,Tint(Panel));
            const FLinearColor Camp=FMath::Lerp(TeamColor(F->team),Panel,.4f);
            Text(F->team==1?TEXT("红色阵营"):F->team==2?TEXT("蓝色阵营"):TEXT("灰色阵营"),NameX,PY+20*S,ResultsFont(11,8),Tint(Camp));
            const FString Value=Damage?FString::Printf(TEXT("%.0f  承伤"),F->damageTaken):FString::Printf(TEXT("%d  击杀"),F->kills);
            Text(Value,Center,Top+98*S,ResultsFont(23,14),Tint(Panel),true);
        } else Text(TEXT("暂无有效战绩"),Center,Top+67*S,ResultsFont(14,10),Tint(FLinearColor(.64f,.72f,.70f,1)),true);
        Text(Description,Center,Top+133*S,ResultsFont(10,8),Tint(FLinearColor(.64f,.72f,.70f,1)),true);
    };
    Honor(TEXT("击杀最多"),TEXT("全场观众 · 含灰色阵营"),M.mostKillsId,X,.15f,false);
    Honor(TEXT("承伤最多"),TEXT("实际生命损失 + 护甲吸收"),M.mostDamageTakenId,X+CardW+Gap,.30f,true);
    Text(FString::Printf(TEXT("下一局将在 %02d 秒后开始"),FMath::CeilToInt(M.intermissionRemaining)),X+W/2,CardY+404*S,ResultsFont(21),Panel,true);
    Text(TEXT("身份与阵营保留  ·  积分清零  ·  基地恢复"),X+W/2,CardY+440*S,FooterSize,FLinearColor(.66f,.73f,.70f,1),true);
}
void AArenaHUD::DrawNoticeCard(AArenaGameMode* Game,const FGiftNotice& N,float X,float Y,float W,float S,bool Gift) {
    const float Lifetime=Gift?2.8f:3.2f,Enter=FMath::Clamp(N.Age/.22f,0.f,1.f);
    const float Alpha=Enter*FMath::Clamp((Lifetime-N.Age)/.4f,0.f,1.f);
    const auto Tint=[Alpha](FLinearColor C,float A=1.f){C.A=Alpha*A;return C;};
    const float PixelScale=FMath::Max(ScreenLayout.scale,.1f);
    const bool Compact=ScreenLayout.mobile||ScreenLayout.portrait||ArenaW()*PixelScale<620||ArenaH()*PixelScale<360;
    if(Compact){
        const float U=1/PixelScale,Padding=6*U,Inner=W-2*Padding;
        const FLinearColor White(.96f,.98f,1,1),Accent=N.Visual==EArenaNoticeVisual::Boss?FLinearColor(.8f,.48f,1,1):N.Elaborate?FLinearColor(1,.74f,.22f,1):FLinearColor(.35f,.82f,.98f,1);
        const FString Count=FString::Printf(TEXT(" ×%s%lld"),N.CountCapped?TEXT("≥"):TEXT(""),N.Count);
        FString Title=(Gift?N.GiftName:N.Visual==EArenaNoticeVisual::TemporaryWeapon?TEXT("右手 · ")+FString(WeaponLabel(N.WeaponKind)):N.WeaponName)+Count;
        FString Detail=N.Detail;
        // Compact phrases preserve the actual outcome and any lifetime/eligibility
        // condition. Gift names and counts are measured separately from nicknames.
        if(!N.Success){Detail.RemoveFromStart(TEXT("未触发："));Detail=TEXT("未触发 · ")+Detail;}
        else if(Gift&&N.Visual==EArenaNoticeVisual::Weapon){
            const FString Scope=N.Detail.StartsWith(TEXT("GM测试"))?TEXT("本次测试"):N.bIsTestData?TEXT("本会话"):TEXT("永久");
            Detail=FString(WeaponLabel(N.WeaponKind))+TEXT(" · ")+Scope+TEXT("左手");
            if(N.Detail.Contains(TEXT("加入后")))Detail+=TEXT("\n加入后可装备");
            else if(N.Detail.Contains(TEXT("下一局")))Detail+=TEXT("\n下一局可装备");
            else if(N.Detail.Contains(TEXT("装备到左手")))Detail+=TEXT(" · 已装备");
            if(!N.bIsTestData&&!N.Detail.StartsWith(TEXT("GM测试")))Detail+=TEXT("\n重复送礼不补弹");
            if(N.Detail.Contains(TEXT("存档重试")))Detail+=TEXT(" · 存档重试中");
        }else if(Gift&&N.Visual==EArenaNoticeVisual::Revive){
            if(N.Detail.Contains(TEXT("本人复活成功")))Detail=TEXT("本人复活成功");
            else if(N.Detail.StartsWith(TEXT("首件复活")))Detail=TEXT("首件复活；余件基础生命+30\n阵亡清零");
            else if(N.Detail.StartsWith(TEXT("阵亡先复活")))Detail=TEXT("阵亡先复活；余件基础+30\n阵亡清零");
            else Detail=TEXT("每件基础生命+30\n进化加倍；英雄保留加成\n阵亡清零");
        }else if(Gift&&N.Visual==EArenaNoticeVisual::Base){
            if(N.Detail.StartsWith(TEXT("基地重建成功")))Detail=TEXT("基地重建；余件生命+1000");
            else if(N.Detail.StartsWith(TEXT("基地强化成功")))Detail=TEXT("每件基地当前/上限+1000");
            else Detail=TEXT("基地重建/强化成功\n按送出时基地状态生效");
        }else if(N.Visual==EArenaNoticeVisual::TemporaryWeapon)Detail=TEXT("临时60秒\n阵亡或到期消失");
        else if(N.Visual==EArenaNoticeVisual::Evolution)Detail=TEXT("40秒：生命强化\n护甲 + 穿透剑气");
        else if(N.Visual==EArenaNoticeVisual::Boss)Detail=TEXT("当时存活队员\n伤害/新积分+20% 60秒\n进化40秒");
        const float DPI=Canvas->Canvas->GetDPIScale();const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const auto TextWidth=[&](const FString& Value,int32 Font){return Measure->Measure(Value,FSlateFontInfo(TextFont,Font),DPI).X/FMath::Max(.01f,DPI);};
        const int32 NameFont=FMath::CeilToInt(9*U),Small=FMath::CeilToInt(9*U);
        int32 TitleFont=FMath::CeilToInt(10*U);
        while(TitleFont>Small&&TextWidth(Title,TitleFont)>Inner) --TitleFont;
        const auto Wrap=[&](const FString& Value,int32 Font){
            TArray<FString> Lines,Paragraphs;Value.ParseIntoArray(Paragraphs,TEXT("\n"),false);
            for(FString Remaining:Paragraphs){while(!Remaining.IsEmpty()){
                FString LineText=FitText(Remaining,Font,Inner,false);
                if(LineText.IsEmpty())LineText=Remaining.Left(Remaining.Len()>1&&Remaining[0]>=0xD800&&Remaining[0]<=0xDBFF?2:1);
                Lines.Add(LineText);Remaining.RightChopInline(LineText.Len());
            }}return Lines;
        };
        const auto Titles=Wrap(Title,TitleFont),Details=Wrap(Detail,Small);
        const float NameRow=16*U,TitleRow=13*U,DetailRow=12*U;
        const float Height=Padding+NameRow+TitleRow*Titles.Num()+DetailRow*Details.Num()+4*U;
        Rect(X-U,Y-U,W+2*U,Height+2*U,Tint(Accent,.9f));Rect(X,Y,W,Height,Tint(FLinearColor(.025f,.045f,.075f,1),.96f));
        if(auto* Avatar=Game->GetNoticeAvatar(N.UserId);Avatar&&Avatar->GetResource()){
            FCanvasTileItem Item({X+Padding,Y+3*U},Avatar->GetResource(),{14*U,14*U},Tint(FLinearColor::White));Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
        }else gwui::DrawIcon(Canvas,gwui::Icon::People,{X+Padding+7*U,Y+10*U},13*U,Tint(Accent));
        Text(FitText(N.ViewerName,NameFont,Inner-20*U),X+Padding+19*U,Y+3*U,NameFont,Tint(White));
        float TY=Y+NameRow;
        for(const auto& LineText:Titles){Text(LineText,X+Padding,TY,TitleFont,Tint(Accent));TY+=TitleRow;}
        for(const auto& LineText:Details){Text(LineText,X+Padding,TY,Small,Tint(N.Success?White:FLinearColor(1,.65f,.49f,1)));TY+=DetailRow;}
        return;
    }
    if(N.Visual==EArenaNoticeVisual::TemporaryWeapon && ScreenLayout.scale<.85f) {
        const float U=1/FMath::Max(ScreenLayout.scale,.1f),Height=58*U;
        const FLinearColor Accent(1,.74f,.22f,1);
        Rect(X-U,Y-U,W+2*U,Height+2*U,Tint(Accent));Rect(X,Y,W,Height,Tint(FLinearColor(.025f,.045f,.075f,1),.96f));
        if(auto* Avatar=Game->GetNoticeAvatar(N.UserId);Avatar&&Avatar->GetResource()) {
            FCanvasTileItem Item({X+7*U,Y+8*U},Avatar->GetResource(),{26*U,26*U},Tint(FLinearColor::White));Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
        }
        gwui::DrawIcon(Canvas,WeaponIcon(N.WeaponKind),{X+20*U,Y+46*U},18*U,Tint(Accent));
        const int32 Font=FMath::CeilToInt(12*U),Small=FMath::CeilToInt(10*U);
        const FString Action=FString(TEXT("右手获得"))+WeaponLabel(N.WeaponKind);
        const float DPI=Canvas->Canvas->GetDPIScale(),TextWidth=W-45*U;
        const float ActionWidth=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Action,FSlateFontInfo(TextFont,Font),DPI).X/FMath::Max(.01f,DPI);
        const FString Name=FitText(N.ViewerName,Font,FMath::Min(60*U,FMath::Max(0.f,TextWidth-ActionWidth-5*U)));
        Text(FitText(Name.IsEmpty()?Action:Name+TEXT(" ")+Action,Font,TextWidth),X+39*U,Y+7*U,Font,Tint(Accent));
        const FString Count=N.Count>1?FString::Printf(TEXT(" ×%s%lld"),N.CountCapped?TEXT("≥"):TEXT(""),N.Count):TEXT("");
        Text(FitText(TEXT("临时60秒 · 阵亡或到期消失")+Count,Small,TextWidth),X+39*U,Y+32*U,Small,Tint(FLinearColor(.96f,.98f,1,1)));
        return;
    }
    const FLinearColor White(.96f,.98f,1,1),Purple(.80f,.48f,1,1),BrightGold(1,.74f,.22f,1);
    const auto Accent=N.Visual==EArenaNoticeVisual::Boss?Purple:N.Elaborate?BrightGold:FLinearColor(.35f,.82f,.98f,1);
    const float Height=72*S;
    if(N.Elaborate)for(int32 I=3;I>=1;--I)Rect(X-I*3*S,Y-I*3*S,W+I*6*S,Height+I*6*S,Tint(Accent,.03f*(4-I)));
    Rect(X-S,Y-S,W+2*S,Height+2*S,Tint(Accent,.9f));Rect(X,Y,W,Height,Tint(FLinearColor(.025f,.045f,.075f,1),.96f));
    Rect(X,Y,3*S,Height,Tint(Accent));
    const float ImageSize=34*S,AvatarSize=26*S;float TX=X+48*S;
    if(Gift) {
        if(auto* Icon=Game->GetGiftIcon(N.GiftName);Icon && Icon->GetResource()) {
            FCanvasTileItem Item({X+9*S,Y+17*S},Icon->GetResource(),{ImageSize,ImageSize},Tint(FLinearColor::White));Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
        }
    }
    const float AX=Gift?TX:X+10*S;
    if(auto* Avatar=Game->GetNoticeAvatar(N.UserId);Avatar && Avatar->GetResource()) {
        FCanvasTileItem Item({AX,Y+8*S},Avatar->GetResource(),{AvatarSize,AvatarSize},Tint(FLinearColor::White));Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
    }else gwui::DrawIcon(Canvas,N.Visual==EArenaNoticeVisual::Boss?gwui::Icon::Boss:gwui::Icon::People,{AX+AvatarSize/2,Y+8*S+AvatarSize/2},AvatarSize,Tint(Accent));
    TX=AX+34*S;
    const int32 Font=FMath::Max(8,FMath::RoundToInt(13*S)),Small=FMath::Max(7,FMath::RoundToInt(11*S));
    const auto NameColor=N.Visual==EArenaNoticeVisual::Boss?(N.Team==1?FLinearColor(1,.30f,.29f,1):N.Team==2?FLinearColor(.36f,.66f,1,1):White):White;
    const FString Count=N.Count>1?FString::Printf(TEXT(" ×%s%lld"),N.CountCapped?TEXT("≥"):TEXT(""),N.Count):TEXT("");
    if(Gift)Text(FitText(N.ViewerName,Font,FMath::Max(30.f*S,W-270*S))+TEXT(" 送出了 ")+N.GiftName+Count,TX,Y+9*S,Font,Tint(White));
    else {
        Text(FitText(N.ViewerName,Font,W-148*S)+TEXT(" 获得了"),TX,Y+9*S,Font,Tint(NameColor));
    }
    gwui::Icon Result=WeaponIcon(N.WeaponKind);
    switch(N.Visual){case EArenaNoticeVisual::Revive:Result=gwui::Icon::Revive;break;case EArenaNoticeVisual::Base:Result=gwui::Icon::Base;break;
        case EArenaNoticeVisual::Evolution:Result=gwui::Icon::EvolutionBuff;break;case EArenaNoticeVisual::Boss:Result=gwui::Icon::Boss;break;case EArenaNoticeVisual::Hero:Result=gwui::Icon::Hero;break;default:break;}
    const float ResultX=Gift?AX:X+10*S;
    gwui::DrawIcon(Canvas,Result,{ResultX+13*S,Y+51*S},24*S,Tint(N.Success?Accent:FLinearColor(.7f,.73f,.78f,1)));
    const FString ResultText=N.Success?(Gift?TEXT("获得了 "):TEXT(""))+N.WeaponName:TEXT("未触发");
    const FString ResultLabel=FitText(ResultText,Font,250*S);
    Text(ResultLabel,ResultX+30*S,Y+39*S,Font,Tint(N.Success?Accent:FLinearColor(1,.65f,.49f,1)));
    const float DPI=Canvas->Canvas->GetDPIScale();
    const float ResultWidth=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(ResultLabel,FSlateFontInfo(TextFont,Font),DPI).X/FMath::Max(.01f,DPI);
    const float DetailX=ResultX+30*S+ResultWidth+14*S;
    Text(FitText(N.Detail,Small,X+W-DetailX-10*S),DetailX,Y+41*S,Small,Tint(White,.9f));
    if(N.Elaborate)for(int32 I=0;I<5;++I){const float A=Game->RunningTime*1.4f+I*1.256f;
        gwui::DrawIcon(Canvas,gwui::Icon::Score,{X+W-15*S+FMath::Sin(A)*7*S,Y+14*S+I*9*S},5*S,Tint(BrightGold,.4f));}
}
void AArenaHUD::DrawGiftNotice(AArenaGameMode* Game) {
    const float PixelScale=FMath::Max(ScreenLayout.scale,.1f);
    const float FieldWidth=ArenaW(),FieldHeight=ArenaH();
    const bool Compact=ScreenLayout.mobile||ScreenLayout.portrait||FieldWidth*PixelScale<620||FieldHeight*PixelScale<360;
    Game->SetNoticeCompactLayout(Compact);
    if(Compact){
        const float U=1/PixelScale,Inset=6*U,Gap=6*U,Available=FMath::Max(50*U,FieldWidth-2*Inset);
        const auto& Gifts=Game->GetGiftNotices();const auto& Rewards=Game->GetRewardNotices();
        const bool HasGift=Gifts.ContainsByPredicate([](const FGiftNotice& N){return N.Active;}),HasReward=Rewards.ContainsByPredicate([](const FGiftNotice& N){return N.Active;});
        const float CardWidth=HasGift&&HasReward?(Available-Gap)/2:FMath::Min(360*U,Available);
        const float Left=ArenaX+Inset,Top=ArenaY+Inset;
        for(const auto& N:Gifts)if(N.Active)DrawNoticeCard(Game,N,Left,Top,CardWidth,U,true);
        for(const auto& N:Rewards)if(N.Active)DrawNoticeCard(Game,N,HasGift?Left+CardWidth+Gap:ArenaX+FieldWidth-Inset-CardWidth,Top,CardWidth,U,false);
        const int32 Font=FMath::CeilToInt(9*U);
        const float JoinRight=MinimapBounds.bIsValid?FMath::Min(ArenaX+FieldWidth-Inset,static_cast<float>(MinimapBounds.Min.X)-Gap):ArenaX+FieldWidth-Inset;
        const float JoinWidth=FMath::Max(50*U,JoinRight-Left),TextWidth=JoinWidth-29*U,DPI=Canvas->Canvas->GetDPIScale();
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        for(const auto& N:Game->GetJoinNotices())if(N.Active){
            const float Alpha=FMath::Min(1.f,N.Age/.2f)*FMath::Clamp((5.5f-N.Age)/.5f,0.f,1.f);
            const auto Tint=[Alpha](FLinearColor C){C.A*=Alpha;return C;};
            const FLinearColor Camp=N.Team==1?FLinearColor(1,.36f,.34f,1):N.Team==2?FLinearColor(.39f,.70f,1,1):FLinearColor(.76f,.81f,.86f,1);
            FString Action=N.Detail;
            if(Action.StartsWith(TEXT("击败了 "))){
                const int32 Suffix=Action.Find(TEXT(" · "));
                const FString Victim=Suffix==INDEX_NONE?Action.Mid(4):Action.Mid(4,Suffix-4);
                Action=TEXT("击败了 ")+FitText(Victim,Font,90*U)+(Suffix==INDEX_NONE?TEXT(""):Action.Mid(Suffix));
            }
            if(N.Count>1)Action+=FString::Printf(TEXT(" ×%s%lld"),N.CountCapped?TEXT("≥"):TEXT(""),N.Count);
            const float ActionWidth=Measure->Measure(Action,FSlateFontInfo(TextFont,Font),DPI).X/FMath::Max(.01f,DPI);
            const FString Name=FitText(N.ViewerName,Font,FMath::Min(100*U,FMath::Max(0.f,TextWidth-ActionWidth-6*U)));
            FString Remaining=Name.IsEmpty()?Action:Name+TEXT(" ")+Action;TArray<FString> Lines;
            while(!Remaining.IsEmpty()){
                FString LineText=FitText(Remaining,Font,TextWidth,false);
                if(LineText.IsEmpty())LineText=Remaining.Left(Remaining.Len()>1&&Remaining[0]>=0xD800&&Remaining[0]<=0xDBFF?2:1);
                Lines.Add(LineText);Remaining.RightChopInline(LineText.Len());
            }
            const float Height=FMath::Max(24*U,8*U+Lines.Num()*12*U),Y=ArenaY+FieldHeight-Inset-Height;
            Rect(Left-U,Y-U,JoinWidth+2*U,Height+2*U,Tint(Camp));Rect(Left,Y,JoinWidth,Height,Tint(FLinearColor(.025f,.04f,.07f,.96f)));
            if(auto* Avatar=Game->GetNoticeAvatar(N.UserId);Avatar&&Avatar->GetResource()){
                FCanvasTileItem Item({Left+4*U,Y+4*U},Avatar->GetResource(),{16*U,16*U},Tint(FLinearColor::White));Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
            }else gwui::DrawIcon(Canvas,gwui::Icon::People,{Left+12*U,Y+12*U},15*U,Tint(Camp));
            float TY=Y+4*U;for(const auto& LineText:Lines){Text(LineText,Left+25*U,TY,Font,Tint(Camp));TY+=12*U;}
        }
        return;
    }
    const float S=FMath::Clamp(ArenaSide/760.f,.38f,1.f),W=FMath::Min(640*S,FieldWidth-16),X=ArenaX+(FieldWidth-W)/2;
    const float Top=ArenaY+12*S,Row=79*S;
    int32 LastGift=-1;
    const auto& Gifts=Game->GetGiftNotices();
    for(int32 I=0;I<Gifts.Num();++I)if(Gifts[I].Active){DrawNoticeCard(Game,Gifts[I],X,Top+I*Row,W,S,true);LastGift=I;}
    const float RewardTop=Top+(LastGift<0?0:(LastGift+1)*Row+8*S);
    const auto& Rewards=Game->GetRewardNotices();
    float RewardY=RewardTop;
    for(int32 I=0;I<Rewards.Num();++I)if(Rewards[I].Active){DrawNoticeCard(Game,Rewards[I],X,RewardY,W,S,false);RewardY+=Rewards[I].Visual==EArenaNoticeVisual::TemporaryWeapon&&ScreenLayout.scale<.85f?64/FMath::Max(ScreenLayout.scale,.1f):Row;}
    const auto& Joins=Game->GetJoinNotices();
    const float BS=FMath::Max(S,.75f/PixelScale);
    const float BW=FMath::Min(FMath::Max(390*S,260.f/PixelScale),FieldWidth-16),BH=32*BS;
    const float BandY=ArenaY+FieldHeight-3*(BH+6*BS)-14*BS-(MinimapBounds.bIsValid&&ArenaSide>=320?170*S:0);
    for(int32 I=0;I<Joins.Num();++I)if(Joins[I].Active){const auto& N=Joins[I];
        const float Progress=FMath::Clamp(N.Age/5.5f,0.f,1.f),Alpha=FMath::Min(1.f,N.Age/.2f)*FMath::Clamp((5.5f-N.Age)/.5f,0.f,1.f);
        const float BX=ArenaX+8+(FieldWidth-BW-16)*(1-Progress),BY=BandY+I*(BH+6*BS);
        const auto Tint=[Alpha](FLinearColor C){C.A*=Alpha;return C;};
        const FLinearColor Camp=N.Team==1?FLinearColor(1,.36f,.34f,1):N.Team==2?FLinearColor(.39f,.70f,1,1):FLinearColor(.76f,.81f,.86f,1);
        Rect(BX-BS,BY-BS,BW+2*BS,BH+2*BS,Tint(Camp));Rect(BX,BY,BW,BH,Tint(FLinearColor(.025f,.04f,.07f,.96f)));
        if(auto* Avatar=Game->GetNoticeAvatar(N.UserId);Avatar && Avatar->GetResource()){
            FCanvasTileItem Item({BX+4*BS,BY+4*BS},Avatar->GetResource(),{24*BS,24*BS},Tint(FLinearColor::White));Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
        }else gwui::DrawIcon(Canvas,gwui::Icon::People,{BX+16*BS,BY+16*BS},22*BS,Tint(Camp));
        const auto ActionIcon=N.Visual==EArenaNoticeVisual::WeaponSwitch?WeaponIcon(N.WeaponKind):N.Visual==EArenaNoticeVisual::TeamSelected?gwui::Icon::Shield:gwui::Icon::People;
        gwui::DrawIcon(Canvas,ActionIcon,{BX+41*BS,BY+16*BS},18*BS,Tint(Camp));
        const int32 Font=FMath::Max(FMath::RoundToInt(12*BS),FMath::CeilToInt(12.f/PixelScale));
        const FString Action=N.Detail+(N.Count>1?FString::Printf(TEXT(" ×%s%lld"),N.CountCapped?TEXT("≥"):TEXT(""),N.Count):TEXT(""));
        const float TextWidth=BW-62*BS,DPI=Canvas->Canvas->GetDPIScale();
        const float ActionWidth=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Action,FSlateFontInfo(TextFont,Font),DPI).X/FMath::Max(.01f,DPI);
        const FString Name=FitText(N.ViewerName,Font,FMath::Min(90*BS,FMath::Max(0.f,TextWidth-ActionWidth-8*BS)));
        const FString Label=Name.IsEmpty()?Action:Name+TEXT(" ")+Action;
        Text(FitText(Label,Font,TextWidth),BX+55*BS,BY+7*BS,Font,Tint(Camp));
    }
}
void AArenaHUD::DrawRankTooltip(AArenaGameMode* Game,int32 FighterIndex,float X,float Y,float Width) {
    const auto& M=Game->GetMatch();if(FighterIndex<0||FighterIndex>=static_cast<int32>(M.fighters.size()))return;
    const auto& F=M.fighters[FighterIndex];
    const FString FullName=Game->ViewerName(F.id),NameFirst=FitText(FullName,11,Width-24,false);
    const bool Wrapped=NameFirst.Len()<FullName.Len();const float Height=Wrapped?268:249;
    X=FMath::Clamp(X,8.f,FMath::Max(8.f,ViewportWidth-Width-8));Y=FMath::Clamp(Y,155.f,FMath::Max(155.f,ViewportHeight-Height-58));
    Rect(X+3,Y+4,Width,Height,FLinearColor(.08f,.13f,.15f,.10f));Rect(X,Y,Width,Height,Panel);
    Rect(X,Y,3,Height,TeamColor(F.team));
    Text(NameFirst,X+12,Y+10,11,Ink);
    if(Wrapped){Text(FullName.Mid(NameFirst.Len()),X+12,Y+29,11,Ink);Y+=19;}
    gwui::DrawIcon(Canvas,WeaponIcon(F.weaponKind),{X+22,Y+50},24,Ink);
    Text(FString(TEXT("左手 · "))+WeaponLabel(F.weaponKind),X+42,Y+36,11,Ink);
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
    Status(gwui::Icon::Clock,F.temporaryWeaponRemaining>0?Gold:Muted,192,F.temporaryWeaponRemaining>0?FString::Printf(TEXT("右手%s · 剩余 %.0fs"),WeaponLabel(F.temporaryWeaponKind),F.temporaryWeaponRemaining):TEXT("左手永久武器 · 右手未拾取"));
    if(F.temporaryWeaponRemaining>0)Status(WeaponIcon(F.temporaryWeaponKind),Gold,215,FString::Printf(TEXT("右手弹药 %d/%d · %s"),F.rightWeapon.ammo,M.weaponFor(F,true).magazine,F.rightWeapon.reloadRemaining>0?*FString::Printf(TEXT("换弹 %.1fs"),F.rightWeapon.reloadRemaining):TEXT("自动开火")));
}
void AArenaHUD::DrawHUD() {
    Super::DrawHUD();auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>();if(!Canvas||!G)return;
    const double Start=FPlatformTime::Seconds();const auto& M=G->GetMatch();
    if(auto* S=UArenaUserSettings::Get())Presentation=S->Budget(G->GetViewers().Num());
    if(M.phase!=gw::Phase::Results)ResultsRound=-1;
    const auto* S=UArenaUserSettings::Get();
    const auto Layout=gwui::ResolveScreenLayout(Canvas->SizeX,Canvas->SizeY,S?S->LayoutDevice:0,S?S->LayoutAspect:0);
    if(!ScreenLayout.logicalSize.Equals(Layout.logicalSize) || !ScreenLayout.offset.Equals(Layout.offset) || ScreenLayout.scale!=Layout.scale)CancelPointer();
    ScreenLayout=Layout;ViewportWidth=Layout.logicalSize.X;ViewportHeight=Layout.logicalSize.Y;
    RankRegions.Reset();RankBounds=FBox2D(ForceInit);MinimapBounds=FBox2D(ForceInit);
    Rect(0,0,Canvas->SizeX,Canvas->SizeY,Paper);
    Canvas->Canvas->PushRelativeTransform(FScaleMatrix(FVector(Layout.scale,Layout.scale,1))*FTranslationMatrix(FVector(Layout.offset.X,Layout.offset.Y,0)));
    if(Layout.mobile || Layout.portrait)DrawAdaptiveHUD(G);else DrawDesktopHUD(G);
    DrawSettings(G);DrawHostPanel(G);
    Canvas->Canvas->PopTransform();
    G->RenderMs=(FPlatformTime::Seconds()-Start)*1000;
    G->FlushRenderedLiveEvents();
}
void AArenaHUD::DrawDesktopHUD(AArenaGameMode* G) {
    const auto& M=G->GetMatch();
    const float W=ViewportWidth,H=ViewportHeight,Right=W-304;
    ViewportWidth=W;ViewportHeight=H;RankRegions.Reset();RankBounds=FBox2D(ForceInit);MinimapBounds=FBox2D(ForceInit);
    const bool GiftSidebar=W>=1600&&H>=900;
    const float SidebarWidth=FMath::Max(280.f,300.f/FMath::Max(ScreenLayout.scale,.1f));
    const float LeftWidth=GiftSidebar?240.f+SidebarWidth:W>=1180&&H>=900?220.f:0.f;
    const float GiftWidth=GiftSidebar?SidebarWidth:W-LeftWidth-368;
    const float GiftHeight=DrawGiftRules(G,0,0,GiftWidth,!GiftSidebar,true);
    const float SideStickerHeight=LeftWidth>0?DrawRecruitSticker(0,0,212,true):0;
    const bool SideSticker=GiftSidebar && SideStickerHeight<=H-74-806;
    const float StickerWidth=SideSticker?212.f:W-LeftWidth-368;
    const float StickerHeight=SideSticker?SideStickerHeight:DrawRecruitSticker(0,0,StickerWidth,true);
    const float BottomRulesHeight=(GiftSidebar?0:GiftHeight+12)+(SideSticker?0:StickerHeight+28);
    ArenaWidth=FMath::Max(160.f,W-LeftWidth-368);
    ArenaHeight=FMath::Max(160.f,H-226-BottomRulesHeight);
    ArenaSide=FMath::Min(ArenaWidth,ArenaHeight);
    ArenaX=32+LeftWidth;ArenaY=148;
    SetupArenaView(G);
    Rect(0,0,W,H,Paper);
    const bool Compact=LeftWidth==0;
    Text(TEXT("几何战争"),32,22,Compact?18:29,Ink);if(!Compact)Text(TEXT("FEISHI  /  GEOMETRIC WARFARE"),34,62,10,Muted);
    const int32 Sec=FMath::Max(0,FMath::CeilToInt(M.config.battleSeconds-M.elapsed));
    Text(FString::Printf(TEXT("%02d:%02d"),Sec/60,Sec%60),ArenaX+ArenaWidth/2,16,40,Ink,true);
    const FString PhaseLabel=M.phase==gw::Phase::Results?TEXT("本局已结束"):M.phase==gw::Phase::Sprint?(Compact?TEXT("最后冲刺 · 禁止重建"):TEXT("最后冲刺 · 基地不可重建")):FString::Printf(TEXT("%d 分钟夺分战"),FMath::CeilToInt(M.config.battleSeconds/60));
    Text(PhaseLabel,ArenaX+ArenaWidth/2,70,Compact?9:12,M.phase==gw::Phase::Sprint?Gold:Muted,true);
    const float ScoreScale=FMath::Max(ScreenLayout.scale,.1f);
    const float TeamWidth=FMath::Min(ArenaWidth*.4f,FMath::Max(240.f,160.f/ScoreScale));
    const float ScoreHeight=28.f/ScoreScale,ScoreY=80-ScoreHeight;
    const int32 ScoreFont=FMath::Max(16,FMath::CeilToInt(12.f/ScoreScale));
    const int64 ScoreMaximum=FMath::Max<int64>(1,FMath::Max<int64>(M.teamScores[1],M.teamScores[2]));
    for(int T=1;T<=2;++T){
        const float X=T==1?ArenaX:ArenaX+ArenaWidth-TeamWidth;const auto C=TeamColor(T);
        Text(T==1?TEXT("RED · 积分"):TEXT("BLUE · 积分"),X,ScoreY-18,12,C);
        ScoreBar(X,ScoreY,TeamWidth,ScoreHeight,M.teamScores[T],ScoreMaximum,C,ScoreFont);
        if(Compact) {
            Text(FString::Printf(TEXT("%d / %d"),M.teamCounts[T],gw::Match::TeamCapacity(T)),X+TeamWidth-80,ScoreY-18,10,Muted);
            HealthBar(X,M.teamBuffRemaining[T]>0?115:123,TeamWidth,15,M.bases[T].hp,M.bases[T].maxHp,C,8);
        } else {
            gwui::DrawIcon(Canvas,gwui::Icon::Shield,{X+9,96},16,C);
            HealthBar(X+24,88,TeamWidth-24,16,M.bases[T].hp,M.bases[T].maxHp,C,9);
            gwui::DrawIcon(Canvas,gwui::Icon::People,{X+9,123},16,C);
            Text(FString::Printf(TEXT("%d / %d"),M.teamCounts[T],gw::Match::TeamCapacity(T)),X+24,115,11,Muted);
            if(!M.bases[T].alive)Text(TEXT("已摧毁"),X+120,116,9,C);
        }
        if(M.teamBuffRemaining[T]>0){
            const FLinearColor Purple(.53f,.17f,.81f,1);Rect(X,130,174,17,FLinearColor(.66f,.39f,.9f,.13f));
            DrawStatusAura({X+9,138},4.8f,false,true,G->RunningTime,false);
            Text(FString::Printf(TEXT("BOSS 伤害/得分+20%%  %.0fs"),M.teamBuffRemaining[T]),X+20,131,8,Purple);
        }
    }
    DrawBossBar(G);
    Text(FitText(ConnectionLabel(G->GetBridge()->ConnectionStatus),8,76),Right,25,8,Muted);
    gwui::DrawIcon(Canvas,gwui::Icon::People,{Right+10,65},22,Ink);
    Text(FString::Printf(TEXT("%d / %d"),G->GetViewers().Num(),gw::Match::ViewerCapacity),Right+31,48,23,Ink);
    gwui::DrawIcon(Canvas,gwui::Icon::Circle,{Right+10,109},18,TeamColor(0));
    Text(FString::Printf(TEXT("灰色 %d / %d"),M.teamCounts[0],gw::Match::TeamCapacity(0)),Right+29,99,13,Muted);
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
        Text(G->GetBridge()->IsLocalTestMode()?TEXT("可直接入队 · 队内再发数字换形"):TEXT("可直接入队 · 队内发 y/z/c/s 换形"),32,258,10,Muted);
        const gwui::Icon ShapeIcons[]={gwui::Icon::Circle,gwui::Icon::Square,gwui::Icon::Rectangle,gwui::Icon::Triangle};
        const TCHAR* ShapeCommands[]={TEXT("y"),TEXT("z"),TEXT("c"),TEXT("s")};
        const int Hp[]={300,250,200,300};
        for(int i=0;i<4;++i){
            const float Y=292+i*66;
            Rect(32,Y-7,189,62,Panel);
            Text(G->GetBridge()->IsLocalTestMode()?FString::FromInt(i+1):FString(ShapeCommands[i]),40,Y,12,Muted);
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
        gwui::DrawIcon(Canvas,G->GetBridge()->IsLocalTestMode()?gwui::Icon::Shotgun:gwui::Icon::Rifle,{41,709},17,Ink);
        Text(TEXT("个人点赞1000解锁霰弹枪 · 关注解锁步枪"),60,700,8,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::EvolutionBuff,{41,733},17,FLinearColor(.92f,.31f,.025f,1));Text(TEXT("进化 / 宝箱 · 击破或触碰获取"),60,724,9,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::WeaponCrate,{41,757},17,Gold);Text(FString::Printf(TEXT("进化 40 秒 · 宝箱武器 %.0f 秒"),gw::TemporaryWeaponLifetime),60,748,9,Muted);
        gwui::DrawIcon(Canvas,gwui::Icon::Hero,{41,781},17,Gold);Text(TEXT("冲刺前十成为英雄 · 同向连发剑气"),60,772,9,Muted);
        if(!SideSticker){
            const float Bottom=H-94,TitleY=FMath::Max(806.f,Bottom-198);
            Text(FString::Printf(TEXT("实时加入 · 待播 %d"),static_cast<int32>(G->GetFeed().pending())),32,TitleY,11,Ink);
            for(const auto& S:G->GetFeed().slots)if(S.active){const float Y=Bottom-S.age*44;if(Y<TitleY+24)continue;auto C=Muted;C.A=FMath::Pow(FMath::Clamp(1.f-static_cast<float>(S.age/gw::ScrollFeed<FString>::Lifetime),0.f,1.f),1.4f);Text(S.text.Left(18),32,Y,11,C);}
        }
    }
    if(Compact && M.phase!=gw::Phase::Results) {
        const float Bottom=ArenaY+ArenaHeight-16;
        Rect(ArenaX+8,Bottom-118,190,126,FLinearColor(.94f,.95f,.92f,.88f));
        Text(FString::Printf(TEXT("实时加入 · 待播 %d"),static_cast<int32>(G->GetFeed().pending())),ArenaX+16,Bottom-113,9,Ink);
        for(const auto& S:G->GetFeed().slots)if(S.active && S.age<2.15){auto C=Ink;C.A=FMath::Pow(1.f-static_cast<float>(S.age/2.15),1.4f);Text(S.text.Left(18),ArenaX+16,Bottom-S.age*39,9,C);}
    }
    gwui::DrawIcon(Canvas,gwui::Icon::Trophy,{Right+10,159},21,Gold);
    Text(TEXT("本局积分榜"),Right+30,145,18,Ink);Text(TEXT("点击锁定视角"),Right+157,152,10,Muted);
    const int64 RankScoreMax=M.leaderboard.empty()?1:FMath::Max<int64>(1,M.fighters[M.leaderboard.front()].score);
    const float RankScoreU=1.f/FMath::Min(FMath::Max(ScreenLayout.scale,.1f),1.f);
    const int32 RankScoreFont=FMath::CeilToInt(10*RankScoreU),RankScoreDigits=Number(RankScoreMax).Len();
    const float RankScoreWidth=FMath::Max(76*RankScoreU,(RankScoreDigits*8+12)*RankScoreU);
    const bool RankScoreSecondLine=true;
    Text(TEXT("昵称"),Right+55,174,8,Muted);Text(TEXT("装备 / 增益"),Right+(RankScoreSecondLine?118:88),174,8,Muted);
    Text(TEXT("状态"),Right+(RankScoreSecondLine?201:162),174,8,Muted);
    Text(TEXT("积分"),Right+232,174,8,Muted);
    const float RowHeight=RankScoreSecondLine?24+21*RankScoreU:24,RankY=189;
    // Reserve space for the focus card; 1080p shows all twenty ranks.
    RankVisibleRows=FMath::Clamp(FMath::FloorToInt((H-(H<620?476:620))/RowHeight),1,20);
    RankTotalRows=FMath::Min(20,static_cast<int32>(M.leaderboard.size()));
    RankOffset=FMath::Clamp(RankOffset,0,FMath::Max(0,RankTotalRows-RankVisibleRows));
    RankBounds=FBox2D({Right-6,RankY-3},{Right+266,RankY+RankVisibleRows*RowHeight});
    float MouseX=-1,MouseY=-1;if(GetOwningPlayerController())GetOwningPlayerController()->GetMousePosition(MouseX,MouseY);
    ToLayoutPointer(MouseX,MouseY);
    int32 HoveredFighter=-1;float HoveredY=RankY;
    for(int32 Row=0;Row<FMath::Min(RankVisibleRows,RankTotalRows-RankOffset);++Row){
        const int32 Index=RankOffset+Row;
        const auto& F=M.fighters[M.leaderboard[Index]];const auto* V=G->FindViewer(F.id);const float Y=RankY+Row*RowHeight;
        const FBox2D Bounds({Right-6,Y-2},{Right+266,Y+RowHeight-3});RankRegions.Add({Bounds,F.id});
        const bool Selected=F.id==G->FocusBodyId,Hover=Bounds.IsInside({MouseX,MouseY});
        if(Hover){HoveredFighter=M.leaderboard[Index];HoveredY=Y;}
        if(Selected||Hover||Index%2==0)Rect(Right-6,Y-2,272,RowHeight-1,FMath::Lerp(Panel,Selected?TeamColor(F.team):Border,Selected?.17f:(Hover?.38f:.15f)));
        if(Selected)Rect(Right-6,Y-2,2,RowHeight-1,TeamColor(F.team));
        Text(FString::Printf(TEXT("%02d"),Index+1),Right,Y+1,10,Muted);
        const bool Evolved=F.alive && F.evolutionRemaining>0,Buffed=F.alive && M.hasBossBuff(F);
        const float PortraitSize=Evolved||Buffed?11.f:18.f;
        if(V)Portrait(V->Avatar,Right+32-PortraitSize/2,Y+10-PortraitSize/2,PortraitSize);
        DrawStatusAura({Right+32,Y+10},6.8f,Evolved,Buffed,G->RunningTime,false);
        Rect(Right+45,Y+2,3,16,TeamColor(F.team));
        const FString FullName=V?V->Name:TEXT("观众"),ShortName=FitText(FullName,9,RankScoreSecondLine?60:30);
        Text(ShortName,Right+55,Y+3,9,F.alive?Ink:Muted);
        const float EquipmentShift=RankScoreSecondLine?0:32;
        gwui::DrawIcon(Canvas,WeaponIcon(F.weaponKind),{Right+126-EquipmentShift,Y+10},17,F.alive?Ink:Muted);
        if(Buffed)gwui::DrawIcon(Canvas,gwui::Icon::BossBuff,{Right+144-EquipmentShift,Y+10},12,FLinearColor(.57f,.17f,.95f,1));
        if(Evolved)gwui::DrawIcon(Canvas,gwui::Icon::EvolutionBuff,{Right+160-EquipmentShift,Y+10},12,FLinearColor(.96f,.32f,.025f,1));
        if(F.heroBuff)gwui::DrawIcon(Canvas,gwui::Icon::Hero,{Right+176-EquipmentShift,Y+10},12,Gold);
        if(F.temporaryWeaponRemaining>0)gwui::DrawIcon(Canvas,WeaponIcon(F.temporaryWeaponKind),{Right+190-EquipmentShift,Y+10},12,Gold);
        const auto StatusColor=F.alive?FLinearColor(.23f,.47f,.33f,1):FLinearColor(.68f,.16f,.14f,1);
        const float StatusX=Right+(RankScoreSecondLine?201:166);
        if(!F.alive)Rect(StatusX-3,Y+2,23,16,FLinearColor(.92f,.77f,.72f,.6f));
        Text(F.alive?TEXT("存活"):TEXT("阵亡"),StatusX,Y+3,8,StatusColor);
        ScoreBar(RankScoreSecondLine?Right-2:Right+266-RankScoreWidth,RankScoreSecondLine?Y+23:Y+1,RankScoreSecondLine?268:RankScoreWidth,18*RankScoreU,F.score,RankScoreMax,TeamColor(F.team),RankScoreFont);
    }
    if(RankTotalRows>RankVisibleRows)Text(FString::Printf(TEXT("滚轮查看  %02d–%02d / %02d"),RankOffset+1,FMath::Min(RankOffset+RankVisibleRows,RankTotalRows),RankTotalRows),Right,RankY+RankVisibleRows*RowHeight+1,9,Muted);
    else Text(TEXT("悬停查看完整昵称、武器与增益说明"),Right,RankY+RankVisibleRows*RowHeight+1,8,Muted);
    DrawFocusCard(G,Right,RankY+RankVisibleRows*RowHeight+17,272);
    Text(FString::Printf(TEXT("%.0f × %.0f  ·  %.1f×  ·  滚轮缩放 / 右键解锁 / 自由视角拖动"),gw::World::Size,gw::World::Size,G->CameraZoom),ArenaX,ArenaY+ArenaHeight+13,Compact?8:10,Muted);
    if(M.phase==gw::Phase::Results)DrawResults(G);else DrawMinimap(G);
    if(Compact && M.phase==gw::Phase::Results) {
        const float Bottom=ArenaY+70;
        for(const auto& S:G->GetFeed().slots)if(S.active && S.age<.9){auto C=Panel;C.A=1.f-static_cast<float>(S.age/.9);Text(S.text.Left(24),ArenaX+20,Bottom-S.age*45,11,C);}
    }
    DrawGiftNotice(G);
    if(HoveredFighter>=0 && !IsOverlayOpen())DrawRankTooltip(G,HoveredFighter,Right-276,HoveredY-18,262);
    DrawGiftRules(G,GiftSidebar?252.f:32+LeftWidth,GiftSidebar?148.f:H-76-StickerHeight-GiftHeight,GiftWidth,!GiftSidebar);
    DrawRecruitSticker(SideSticker?24.f:32+LeftWidth,SideSticker?806.f:H-64-StickerHeight,StickerWidth);
    Line({32,H-48},{W-32,H-48},Border);
    Text((G->bRecording?G->DemoCaption:G->LastEvent).Left(Compact?41:64),32,H-34,Compact?9:11,Muted);
    Text(G->bRecording?FString::Printf(TEXT("第 %d 局  ·  演示速度 %d×"),M.round,FMath::RoundToInt(G->DemoSpeed)):FString::Printf(TEXT("%.0f FPS   SIM %.1f ms   %d×"),G->FrameMs>0?1000/G->FrameMs:0,G->SimulationMs,FMath::RoundToInt(G->DemoSpeed)),Right,H-34,10,Muted);
}
