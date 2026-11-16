#include "BasicFPSGame.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Styling/CoreStyle.h"

class SBasicFPSMenu : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SBasicFPSMenu) {} SLATE_ARGUMENT(TWeakObjectPtr<ABasicFPSController>,Owner) SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Owner=Args._Owner;ChildSlot[Args._Content.Widget]; }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override {
        if(Event.GetKey()==EKeys::Escape) {if(Owner.IsValid())Owner->ToggleMenu();return FReply::Handled();}
        return SCompoundWidget::OnKeyDown(Geometry,Event);
    }
private:
    TWeakObjectPtr<ABasicFPSController> Owner;
};

void ABasicFPSController::BuildMenu() {
    if(!bMenuOpen||bEndingPlay||bSmokeTest||!GEngine->GameViewport)return;
    if(Menu.IsValid())GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
    auto Label=[](const FString& Text) {return SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",15));};
    auto Button=[&](const FString& Text,TFunction<FReply()> Action) {return SNew(SButton).ContentPadding(FMargin(12,10)).OnClicked_Lambda(MoveTemp(Action))[Label(Text)];};
    auto Column=SNew(SVerticalBox);
    auto Add=[&](TSharedRef<SWidget> Widget,float Gap=9) {Column->AddSlot().AutoHeight().Padding(0,0,0,Gap)[Widget];};
    Add(SNew(STextBlock).Text(FText::FromString(TEXT("BASIC FPS · 언리얼 시험판"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",24)));
    Add(Label(TEXT("DX11 · MSAA · Windows 실기 확인이 필요한 시험판이에요.\nWASD 이동 · Shift 걷기 · 왼쪽 클릭 사격 · R 재장전 · Tab 점수판 · Esc 메뉴")));
    Add(SNew(STextBlock).Text_Lambda([this]{return FText::FromString(FString::Printf(TEXT("%.0f FPS · %s"),FPS,*DiagnosticLine()));})
        .AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Regular",13)));
    Add(Label(TEXT("닉네임")),4);
    Add(SNew(SEditableTextBox).Text_Lambda([this]{return FText::FromString(Settings()->Nickname);})
        .IsEnabled_Lambda([this]{return !bDisplayPending;})
        .OnTextChanged_Lambda([this](const FText& Text){Settings()->Nickname=Text.ToString().Left(16);}));
    Add(Label(TEXT("마우스 감도")),4);
    Add(SNew(SSpinBox<float>).MinValue(.001f).MaxValue(10.f).MinSliderValue(.001f).MaxSliderValue(1.f).Delta(.001f)
        .Value_Lambda([this]{return Settings()->Sensitivity;}).IsEnabled_Lambda([this]{return !bDisplayPending;})
        .OnValueChanged_Lambda([this](float Value){Settings()->Sensitivity=Value;}));
    if(ModeOptions.IsEmpty()) {ModeOptions={MakeShared<FString>(TEXT("독점 전체화면")),MakeShared<FString>(TEXT("전체화면 창모드")),MakeShared<FString>(TEXT("창모드"))};}
    SelectedMode=ModeOptions[FMath::Clamp(DisplayChoice,0,2)];
    Add(Label(TEXT("화면 모드")),4);
    Add(SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&ModeOptions).InitiallySelectedItem(SelectedMode)
        .IsEnabled_Lambda([this]{return !bDisplayPending;})
        .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item){return SNew(STextBlock).Text(FText::FromString(*Item));})
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item,ESelectInfo::Type){SelectedMode=Item;DisplayChoice=ModeOptions.IndexOfByKey(Item);})
        [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(SelectedMode.IsValid()?*SelectedMode:TEXT("창모드"));})]);
    Add(Button(TEXT("화면 모드 적용"),[this]{if(!bDisplayPending)PreviewDisplay(DisplayChoice);return FReply::Handled();}));
    if(bDisplayPending) {
        Add(Button(TEXT("화면 설정 유지"),[this]{ConfirmDisplay();return FReply::Handled();}));
        Add(Button(TEXT("이전 화면으로 복구"),[this]{RevertDisplay();return FReply::Handled();}));
    }
    if(AAOptions.IsEmpty())AAOptions={MakeShared<FString>(TEXT("끔")),MakeShared<FString>(TEXT("MSAA 2배")),MakeShared<FString>(TEXT("MSAA 4배"))};
    SelectedAA=AAOptions[Settings()->MSAASamples==0?0:Settings()->MSAASamples==2?1:2];
    Add(Label(TEXT("안티앨리어싱")),4);
    Add(SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&AAOptions).InitiallySelectedItem(SelectedAA)
        .IsEnabled_Lambda([this]{return !bDisplayPending;})
        .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item){return SNew(STextBlock).Text(FText::FromString(*Item));})
        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item,ESelectInfo::Type){SelectedAA=Item;const int I=AAOptions.IndexOfByKey(Item);Settings()->MSAASamples=I==0?0:I==1?2:4;Settings()->ApplyGraphics(bMenuOpen);})
        [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(SelectedAA.IsValid()?*SelectedAA:TEXT("MSAA 4배"));})]);
    Add(Label(TEXT("FPS 제한 · 0은 무제한이에요. 메뉴는 최대 30FPS로 동작해요.")),4);
    Add(SNew(SSpinBox<int32>).MinValue(0).MaxValue(1000).Delta(1).Value_Lambda([this]{return Settings()->FrameCap;})
        .IsEnabled_Lambda([this]{return !bDisplayPending;})
        .OnValueChanged_Lambda([this](int32 Value){Settings()->FrameCap=Value;Settings()->ApplyGraphics(bMenuOpen);}));
    Add(Button(TEXT("설정 저장"),[this]{if(!bDisplayPending){SavePreferences();Status=TEXT("이 PC에 설정을 저장했어요.");}return FReply::Handled();}));
    Add(Label(TEXT("서버 주소")),4);
    Add(SNew(SEditableTextBox).Text_Lambda([this]{return FText::FromString(Address);}).OnTextChanged_Lambda([this](const FText& Text){Address=Text.ToString().Left(2048);}));
    Add(Button(TEXT("서버 다시 연결"),[this]{if(!bDisplayPending)Connect();return FReply::Handled();}));
    Add(Button(TEXT("입장 / 계속하기"),[this]{Join();return FReply::Handled();}));
    Add(Label(TEXT("연습 봇 · 방을 연 PC에서만 변경돼요.")),4);
    auto Bots=SNew(SHorizontalBox);
    for(int Count:{0,1,3,6})Bots->AddSlot().FillWidth(1).Padding(0,0,6,0)[Button(FString::Printf(TEXT("%d명"),Count),[this,Count]{SetBots(Count);return FReply::Handled();})];Add(Bots);
    Add(Button(bRecording?TEXT("성능 기록 저장 / 중지"):TEXT("성능 기록 시작 · 최대 3분"),[this]{ToggleRecording();BuildMenu();return FReply::Handled();}));
    Add(Button(TEXT("현재 화면 저장"),[this]{SaveScreenshot();return FReply::Handled();}));
    Add(SNew(STextBlock).Text_Lambda([this]{return FText::FromString(MenuStatus());}).AutoWrapText(true));
    Add(Label(TEXT("독점 전체화면·외장 GPU 사용·자동 MUX 전환은 서로 다른 상태예요.\n게임 프레임 수는 모니터에 실제 표시된 장수를 뜻하지 않아요.")));
    Add(Button(TEXT("게임 종료"),[this]{ConsoleCommand(TEXT("quit"));return FReply::Handled();}));
    Menu=SNew(SBasicFPSMenu).Owner(this)[SNew(SOverlay)
        +SOverlay::Slot()[SNew(SBorder).BorderBackgroundColor(FLinearColor(.01,.02,.035,.92))]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Fill).Padding(20)
        [SNew(SBox).WidthOverride(620)[SNew(SBorder).Padding(22).BorderBackgroundColor(FLinearColor(.035,.065,.1,1))
        [SNew(SScrollBox)+SScrollBox::Slot()[Column]]]]];
    GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(),100);
    if(FSlateApplication::IsInitialized())FSlateApplication::Get().SetKeyboardFocus(Menu,EFocusCause::SetDirectly);
}
