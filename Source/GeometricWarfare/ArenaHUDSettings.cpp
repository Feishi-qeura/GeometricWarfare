#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "ArenaUserSettings.h"
#include "Engine/Canvas.h"

void AArenaHUD::ToggleSettings() {CancelPointer();if(GMWidget.IsValid()){CloseGM();return;}if(bHostPanelOpen)bHostPanelOpen=false;else bSettingsOpen=!bSettingsOpen;SettingRegions.Reset();}
void AArenaHUD::ActivateSetting(int32 Action) {
    if(Action==0 || Action==6){ToggleSettings();return;}
    if(Action==10){CancelPointer();bSettingsOpen=false;bHostPanelOpen=true;SettingRegions.Reset();return;}
    if(Action==14){CancelPointer();bHostPanelOpen=false;SettingRegions.Reset();return;}
    if(Action>=11 && Action<=13){if(auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>())G->HostAssist(Action==11?1:Action==12?2:0);return;}
    auto* S=UArenaUserSettings::Get();if(!S)return;
    auto Next=[](int32 Current,const auto& Values) {
        for(int32 I=0;I<UE_ARRAY_COUNT(Values);++I)if(Values[I]==Current)return Values[(I+1)%UE_ARRAY_COUNT(Values)];
        return Values[0];
    };
    const int32 Rates[]={30,60,90,120,0},Labels[]={0,40,80,180},Numbers[]={0,32,64,128};
    switch(Action) {
    case 1:S->SetVSyncEnabled(!S->IsVSyncEnabled());break;
    case 2:S->SetFrameRateLimit(Next(FMath::RoundToInt(S->GetFrameRateLimit()),Rates));break;
    case 3:S->EffectMode=(S->EffectMode+1)%3;break;
    case 4:S->BattlefieldLabels=Next(S->BattlefieldLabels,Labels);break;
    case 5:S->FloatingNumbers=Next(S->FloatingNumbers,Numbers);break;
    case 7:S->SetVSyncEnabled(false);S->SetFrameRateLimit(60);S->EffectMode=0;S->BattlefieldLabels=180;S->FloatingNumbers=128;break;
    case 8:S->LayoutDevice=(S->LayoutDevice+1)%3;CancelPointer();break;
    case 9:S->LayoutAspect=(S->LayoutAspect+1)%6;CancelPointer();break;
    default:return;
    }
    S->ApplyPresentationSettings();
}
void AArenaHUD::DrawSettings(AArenaGameMode* Game) {
    const FLinearColor Ink(.035f,.065f,.08f,1),Muted(.30f,.36f,.38f,1),Paper(.985f,.99f,.975f,1),Accent(.045f,.26f,.72f,1);
    const float W=ViewportWidth,H=ViewportHeight;
    SettingsButtonBounds=FBox2D({W-115,16},{W-32,43});
    Rect(W-115,16,83,27,FLinearColor(.86f,.90f,.87f,1));Text(TEXT("设置  Esc"),W-106,20,11,Ink);
    HostButtonBounds=FBox2D({W-222,16},{W-124,43});
    Rect(W-222,16,98,27,FLinearColor(.93f,.87f,.73f,1));Text(TEXT("主播助战"),W-208,20,11,Ink);
    SettingRegions.Reset();if(!bSettingsOpen)return;
    auto* S=UArenaUserSettings::Get();if(!S)return;
    const float Scale=FMath::Min(1.f,FMath::Min((W-24)/560.f,(H-24)/736.f));
    const float Width=560*Scale,Height=736*Scale,X=(W-Width)/2,Y=(H-Height)/2;
    Rect(0,0,W,H,FLinearColor(.035f,.065f,.08f,.68f));Rect(X,Y,Width,Height,Paper);
    const auto Txt=[&](const FString& Value,float PX,float PY,int32 Size,FLinearColor Color){Text(Value,X+PX*Scale,Y+PY*Scale,FMath::Max(7,FMath::RoundToInt(Size*Scale)),Color);};
    Txt(TEXT("玩法设置"),28,20,24,Ink);Txt(TEXT("即时生效 · 自动保存 · 对局继续运行"),28,57,11,Muted);
    const auto Row=[&](int32 Action,float Top,const TCHAR* Label,const FString& Value){
        Txt(Label,28,Top+10,14,Ink);
        const FBox2D B({X+305*Scale,Y+Top*Scale},{X+532*Scale,Y+(Top+38)*Scale});
        Rect(B.Min.X,B.Min.Y,B.GetSize().X,B.GetSize().Y,FLinearColor(.87f,.92f,.94f,1));
        Txt(Value+TEXT("  >"),319,Top+9,13,Accent);SettingRegions.Add({B,Action});
    };
    const TCHAR* Devices[]={TEXT("自动"),TEXT("PC"),TEXT("移动端")};
    const TCHAR* Aspects[]={TEXT("跟随窗口"),TEXT("横屏 16:9"),TEXT("横屏 4:3"),TEXT("竖屏 9:16"),TEXT("横屏 20:17"),TEXT("横屏 19:9")};
    Row(8,88,TEXT("界面布局"),Devices[FMath::Clamp(S->LayoutDevice,0,2)]);
    Row(9,136,TEXT("开播画面比例"),Aspects[FMath::Clamp(S->LayoutAspect,0,5)]);
    Row(1,184,TEXT("垂直同步"),S->IsVSyncEnabled()?TEXT("开启"):TEXT("关闭"));
    Row(2,232,TEXT("帧率上限"),S->GetFrameRateLimit()==0?TEXT("不限"):FString::Printf(TEXT("%.0f FPS"),S->GetFrameRateLimit()));
    const TCHAR* Modes[]={TEXT("自动"),TEXT("完整"),TEXT("精简")};
    Row(3,280,TEXT("战场动效"),Modes[FMath::Clamp(S->EffectMode,0,2)]);
    Row(4,328,TEXT("战场姓名 / 能量条数量"),FString::FromInt(S->BattlefieldLabels));
    Row(5,376,TEXT("飘字数量"),FString::FromInt(S->FloatingNumbers));
    const auto Slider=[&](int32 Action,float Top,const TCHAR* Label,float Value){
        Txt(Label,28,Top+10,14,Ink);Txt(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Value*100)),244,Top+10,12,Accent);
        const FBox2D Hit({X+305*Scale,Y+Top*Scale},{X+532*Scale,Y+(Top+38)*Scale});
        Rect(Hit.Min.X,Hit.Min.Y+17*Scale,Hit.GetSize().X,4*Scale,FLinearColor(.78f,.84f,.84f,1));
        Rect(Hit.Min.X,Hit.Min.Y+17*Scale,Hit.GetSize().X*Value,4*Scale,Accent);
        Rect(Hit.Min.X+Hit.GetSize().X*Value-5*Scale,Hit.Min.Y+11*Scale,10*Scale,16*Scale,Accent);
        SettingRegions.Add({Hit,Action});
    };
    Slider(15,424,TEXT("BGM 音量"),S->BgmVolume);Slider(16,472,TEXT("音效音量"),S->SfxVolume);
    const auto B=S->Budget(Game->GetViewers().Num());
    Txt(FString::Printf(TEXT("当前 %d 人 · %s · 姓名/能量条 %d · 飘字 %d"),Game->GetViewers().Num(),B.reduced?TEXT("精简"):TEXT("完整"),B.labels,B.damageNumbers),28,529,11,Muted);
    Txt(TEXT("竖屏：战场在上，榜单在下；等比适配，不拉伸。"),28,556,11,Muted);
    Txt(TEXT("Spout 固定 1920×1080；不同画面比例使用留边。"),28,579,11,Muted);
    Txt(TEXT("精简动效优先保留关注观众、主播、英雄和 BOSS 提示。"),28,602,10,Muted);
    Txt(TEXT("垂直同步开启时，实际帧率也受显示器刷新率限制。"),28,625,10,Muted);
    const auto Button=[&](int32 Action,float Left,float WidthValue,const TCHAR* Label){
        const FBox2D Bnd({X+Left*Scale,Y+670*Scale},{X+(Left+WidthValue)*Scale,Y+708*Scale});
        Rect(Bnd.Min.X,Bnd.Min.Y,Bnd.GetSize().X,Bnd.GetSize().Y,FLinearColor(.86f,.90f,.87f,1));
        Txt(Label,Left+17,678,13,Ink);SettingRegions.Add({Bnd,Action});
    };
    Button(7,28,166,TEXT("恢复性能默认"));Button(6,398,134,TEXT("关闭 / Esc"));
}
void AArenaHUD::DrawHostPanel(AArenaGameMode* Game) {
    if(!bHostPanelOpen)return;
    const FLinearColor Ink(.035f,.065f,.08f,1),Muted(.30f,.36f,.38f,1),Paper(.985f,.99f,.975f,1);
    const float W=ViewportWidth,H=ViewportHeight,Scale=FMath::Min(1.f,FMath::Min((W-24)/520.f,(H-24)/330.f));
    const float X=(W-520*Scale)/2,Y=(H-330*Scale)/2;
    Rect(0,0,W,H,FLinearColor(.035f,.065f,.08f,.68f));Rect(X,Y,520*Scale,330*Scale,Paper);
    const auto Txt=[&](const FString& V,float PX,float PY,int32 Size,FLinearColor C){Text(V,X+PX*Scale,Y+PY*Scale,FMath::Max(7,FMath::RoundToInt(Size*Scale)),C);};
    Txt(TEXT("主播助战"),28,20,24,Ink);
    const int32 Team=Game->GetHostTeam();
    Txt(Team<0?TEXT("当前：未助战"):Team==1?TEXT("当前：红色阵营"):Team==2?TEXT("当前：蓝色阵营"):TEXT("当前：灰色阵营"),28,62,14,Muted);
    const bool Ready=Game->CanHostAssist();
    const FLinearColor Colors[]={FLinearColor(.78f,.09f,.075f,1),FLinearColor(.045f,.26f,.72f,1),FLinearColor(.34f,.40f,.41f,1)};
    const TCHAR* Labels[]={TEXT("助战红方"),TEXT("助战蓝方"),TEXT("助战灰方")};
    for(int32 I=0;I<3;++I){
        const float Left=28+I*160;const FBox2D B({X+Left*Scale,Y+112*Scale},{X+(Left+144)*Scale,Y+166*Scale});
        Rect(B.Min.X,B.Min.Y,B.GetSize().X,B.GetSize().Y,FMath::Lerp(Paper,Colors[I],Ready?.18f:.06f));
        Txt(Labels[I],Left+23,127,15,Ready?Colors[I]:Muted);
        if(Ready)SettingRegions.Add({B,11+I});
    }
    Txt(TEXT("五角星身份 · 自动跟随 · 切换阵营保留同一角色"),28,190,11,Muted);
    Txt(TEXT("不占观众名额，不计个人积分，不参与排行榜与评奖。"),28,213,11,Muted);
    if(!Ready)Txt(Game->GetMatch().phase==gw::Phase::Results?TEXT("结算期间无法调整主播阵营"):TEXT("等待直播对局就绪后可助战"),28,240,11,Muted);
    const FBox2D B({X+358*Scale,Y+269*Scale},{X+492*Scale,Y+307*Scale});
    Rect(B.Min.X,B.Min.Y,B.GetSize().X,B.GetSize().Y,FLinearColor(.86f,.90f,.87f,1));Txt(TEXT("关闭 / Esc"),375,277,13,Ink);SettingRegions.Add({B,14});
}
