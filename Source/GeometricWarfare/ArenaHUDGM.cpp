#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/Font.h"
#include "UnrealClient.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

void AArenaHUD::EndPlay(const EEndPlayReason::Type Reason){CloseGM();Super::EndPlay(Reason);}
void AArenaHUD::CloseGM(){if(GMWidget.IsValid()&&GetWorld()&&GetWorld()->GetGameViewport())GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(GMWidget.ToSharedRef());GMWidget.Reset();if(FSlateApplication::IsInitialized())FSlateApplication::Get().SetUserFocusToGameViewport(0);}
void AArenaHUD::SelectGMUser(int32 Direction){
    auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>();if(!G)return;
    TArray<FString> Users;for(const auto& Pair:G->GetViewers())if(!Pair.Value.bDebugBot)Users.Add(Pair.Key);
    Users.Sort();if(Users.IsEmpty()){GMSelectedUser.Reset();return;}
    const int32 Old=Users.IndexOfByKey(GMSelectedUser);const int32 Next=Old<0?0:(Old+Direction+Users.Num())%Users.Num();GMSelectedUser=Users[Next];
}
FText AArenaHUD::GMSelectedLabel() const {
    const auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>();const auto* V=G?G->GetViewers().Find(GMSelectedUser):nullptr;
    return FText::FromString(V?V->Name+TEXT("  ·  SDK ID：")+V->UserId:TEXT("暂无真实入场观众；用真实账号评论“加入”后点击刷新"));
}
void AArenaHUD::ToggleGM(){
    if(GMWidget.IsValid()){CloseGM();return;}
    auto* G=GetWorld()->GetAuthGameMode<AArenaGameMode>();auto* Viewport=GetWorld()->GetGameViewport();
    if(!G||!G->IsGMEnabled()||!Viewport)return;
    CancelPointer();bSettingsOpen=bHostPanelOpen=false;SelectGMUser(0);
    const TWeakObjectPtr<AArenaHUD> Weak(this);
    const auto EscapeHandler=FOnKeyDown::CreateLambda([Weak](const FGeometry&,const FKeyEvent& Event){if(Event.GetKey()==EKeys::Escape&&Weak.IsValid()){Weak->CloseGM();return FReply::Handled();}return FReply::Unhandled();});
    const auto Font=FSlateFontInfo(TextFont,15);
    const auto Label=[&](const TCHAR* Value){return SNew(STextBlock).Text(FText::FromString(Value)).Font(Font).ColorAndOpacity(FLinearColor(.06f,.10f,.13f,1));};
    const auto Button=[&](const TCHAR* Value,TFunction<void(AArenaHUD&,AArenaGameMode&)> Action)->TSharedRef<SWidget>{
        return SNew(SButton).ContentPadding(FMargin(12,9)).OnClicked_Lambda([Weak,Action](){if(Weak.IsValid())if(auto* Game=Weak->GetWorld()->GetAuthGameMode<AArenaGameMode>())Action(*Weak,*Game);return FReply::Handled();})[SNew(STextBlock).Text(FText::FromString(Value)).Font(Font).ColorAndOpacity(FLinearColor(.96f,.97f,.96f,1))];
    };
    TSharedRef<SVerticalBox> Content=SNew(SVerticalBox);
    Content->AddSlot().AutoHeight().Padding(0,0,0,14)[Label(TEXT("GM 调试面板  ·  G 打开 / Esc 关闭"))];
    Content->AddSlot().AutoHeight().Padding(0,4)[Label(TEXT("红 / 蓝 / 灰人机 · 每次添加数量（1–100）"))];
    Content->AddSlot().AutoHeight().Padding(0,4)[SNew(SEditableTextBox).OnKeyDownHandler(EscapeHandler).Font(Font).Text(FText::AsNumber(GMBotCount)).OnTextChanged_Lambda([Weak](const FText& Text){int Count=1;if(Weak.IsValid()&&LexTryParseString(Count,*Text.ToString()))Weak->GMBotCount=FMath::Clamp(Count,1,100);})];
    Content->AddSlot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[Button(TEXT("添加红方"),[](auto& H,auto& Game){Game.AddGMBots(1,H.GMBotCount);})]
        +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[Button(TEXT("添加蓝方"),[](auto& H,auto& Game){Game.AddGMBots(2,H.GMBotCount);})]
        +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("添加灰方"),[](auto& H,auto& Game){Game.AddGMBots(0,H.GMBotCount);})]];
    Content->AddSlot().AutoHeight().Padding(0,14,0,4)[Label(TEXT("礼物目标：真实抖音号（先绑定）或 SDK OpenID（直接输入）"))];
    Content->AddSlot().AutoHeight().Padding(0,4)[SNew(SEditableTextBox).OnKeyDownHandler(EscapeHandler).Font(Font).HintText(FText::FromString(TEXT("请输入真实账号ID"))).Text(FText::FromString(GMIdentity)).OnTextChanged_Lambda([Weak](const FText& Text){if(Weak.IsValid())Weak->GMIdentity=Text.ToString().Left(256);})];
    Content->AddSlot().AutoHeight().Padding(0,4)[SNew(STextBlock).Text_Lambda([Weak](){return Weak.IsValid()?Weak->GMSelectedLabel():FText();}).Font(Font).ColorAndOpacity(FLinearColor(.08f,.15f,.2f,1)).AutoWrapText(true)];
    Content->AddSlot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[Button(TEXT("上一位"),[](auto& H,auto&){H.SelectGMUser(-1);})]
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)[Button(TEXT("下一位 / 刷新"),[](auto& H,auto&){H.SelectGMUser(1);})]
        +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("绑定输入抖音号到选中观众"),[](auto& H,auto& Game){Game.BindGMIdentity(H.GMIdentity,H.GMSelectedUser);})]];
    Content->AddSlot().AutoHeight().Padding(0,8,0,4)[Label(TEXT("礼物数量（1–100）"))];
    Content->AddSlot().AutoHeight().Padding(0,4)[SNew(SEditableTextBox).OnKeyDownHandler(EscapeHandler).Font(Font).Text(FText::FromString(GMCount)).OnTextChanged_Lambda([Weak](const FText& Text){if(Weak.IsValid())Weak->GMCount=Text.ToString().Left(3);})];
    TSharedRef<SHorizontalBox> Gifts=SNew(SHorizontalBox);
    for(const TCHAR* Name:{TEXT("仙女棒"),TEXT("能力药丸"),TEXT("魔法镜"),TEXT("甜甜圈"),TEXT("能量电池")}){const FString Gift(Name);
        Gifts->AddSlot().FillWidth(1).Padding(0,0,5,0)[Button(Name,[Gift](auto& H,auto& Game){int Count=0;if(LexTryParseString(Count,*H.GMCount))Game.SendGMGift(H.GMIdentity,Gift,Count);})];}
    Content->AddSlot().AutoHeight().Padding(0,4)[Gifts];
    Content->AddSlot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[Button(TEXT("目标阵亡（测仙女棒）"),[](auto& H,auto& Game){Game.PrepareGMGiftTarget(H.GMIdentity);})]
        +SHorizontalBox::Slot().FillWidth(1)[Button(TEXT("摧毁目标阵营基地（测药丸）"),[](auto& H,auto& Game){Game.PrepareGMGiftTarget(H.GMIdentity,true);})]];
    Content->AddSlot().AutoHeight().Padding(0,12,0,4)[Label(TEXT("模拟礼物仅影响本次运行，不执行真实送礼、扣费或平台履约。"))];
    Content->AddSlot().AutoHeight().Padding(0,4)[SNew(STextBlock).Text_Lambda([Weak](){const auto* Game=Weak.IsValid()?Weak->GetWorld()->GetAuthGameMode<AArenaGameMode>():nullptr;return FText::FromString(Game?Game->LastEvent:TEXT(""));}).Font(Font).AutoWrapText(true).ColorAndOpacity(FLinearColor(.06f,.25f,.18f,1))];
    Content->AddSlot().AutoHeight().Padding(0,14,0,0)[Button(TEXT("关闭面板"),[](auto& H,auto&){H.CloseGM();})];
    GMWidget=SNew(SOverlay)+SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.025f,.04f,.05f,.65f))]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride_Lambda([Weak](){const auto* V=Weak.IsValid()?Weak->GetWorld()->GetGameViewport():nullptr;const int W=V&&V->Viewport?V->Viewport->GetSizeXY().X:1920;return FOptionalSize(FMath::Clamp(W-32,280,760));})
        .MaxDesiredHeight_Lambda([Weak](){const auto* V=Weak.IsValid()?Weak->GetWorld()->GetGameViewport():nullptr;const int H=V&&V->Viewport?V->Viewport->GetSizeXY().Y:1080;return FOptionalSize(FMath::Max(160,H-32));})
        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(FLinearColor(.95f,.96f,.94f,1)).Padding(24)
        [SNew(SScrollBox)+SScrollBox::Slot()[Content]]]];
    Viewport->AddViewportWidgetContent(GMWidget.ToSharedRef(),100);
}
