#include "BasicFPSGame.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineGlobals.h"
#include "UnrealClient.h"
#include "RHI.h"
#include "RHIResources.h"
#include "DynamicRHI.h"
#include "RenderingThread.h"
#include "Async/Async.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/SWindow.h"
#include "GenericPlatform/GenericWindow.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <dxgi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace {
void SetCVar(const TCHAR* Name,int32 Value) { if(auto* Var=IConsoleManager::Get().FindConsoleVariable(Name))Var->Set(Value,ECVF_SetByCode); }
FIntPoint NativeDisplay(ABasicFPSController& Controller,int32& Hz) {
    FIntPoint Result=Controller.Settings()->GetScreenResolution();Hz=0;
#if PLATFORM_WINDOWS
    if(GEngine->GameViewport)if(auto Window=GEngine->GameViewport->GetWindow();Window.IsValid()&&Window->GetNativeWindow().IsValid()) {
        HWND Handle=static_cast<HWND>(Window->GetNativeWindow()->GetOSWindowHandle());
        MONITORINFOEXW Info={};Info.cbSize=sizeof(Info);
        if(GetMonitorInfoW(MonitorFromWindow(Handle,MONITOR_DEFAULTTONEAREST),reinterpret_cast<MONITORINFO*>(&Info))) {
            DEVMODEW Mode={};Mode.dmSize=sizeof(Mode);
            if(EnumDisplaySettingsW(Info.szDevice,ENUM_CURRENT_SETTINGS,&Mode)){Result=FIntPoint(Mode.dmPelsWidth,Mode.dmPelsHeight);Hz=Mode.dmDisplayFrequency;}
        }
    }
#endif
    return Result;
}
}
void UBasicFPSSettings::ApplyGraphics(bool bMenu) {
    Sensitivity=FMath::Clamp(Sensitivity,.001f,10.f);FrameCap=FMath::Clamp(FrameCap,0,1000);
    if(MSAASamples!=0&&MSAASamples!=2&&MSAASamples!=4)MSAASamples=4;
    if(GEngine){GEngine->bSmoothFrameRate=false;GEngine->bUseFixedFrameRate=false;}
    SetVSyncEnabled(false);SetDynamicResolutionEnabled(false);SetFrameRateLimit(FrameCap);
    SetCVar(TEXT("r.VSync"),0);SetCVar(TEXT("rhi.SyncInterval"),0);
    SetCVar(TEXT("r.AntiAliasingMethod"),MSAASamples?3:0);
    // In Unreal, r.MSAACount=0 means TAA fallback, not disabled MSAA.
    SetCVar(TEXT("r.MSAACount"),MSAASamples?MSAASamples:1);
    SetCVar(TEXT("r.ScreenPercentage"),100);SetCVar(TEXT("r.SecondaryScreenPercentage.GameViewport"),100);SetCVar(TEXT("r.DynamicRes.OperationMode"),0);
    SetCVar(TEXT("t.MaxFPS"),bMenu?(FrameCap?FMath::Min(FrameCap,30):30):FrameCap);
}
void ABasicFPSController::PreviewDisplay(int32 Choice) {
    if(bDisplayPending)RevertDisplay();
    ++DisplayGeneration;
    if(!GEngine->GameViewport||!GEngine->GameViewport->GetWindow().IsValid()){DisplayStatus=TEXT("게임 창을 찾지 못했어요.");return;}
    auto* S=Settings();PreviousMode=S->GetFullscreenMode();PreviousResolution=S->GetScreenResolution();
    const auto Window=GEngine->GameViewport->GetWindow();
    if(PreviousMode==EWindowMode::Windowed) {
        PreviewWindowSize=GEngine->GameViewport->Viewport->GetSizeXY();PreviewWindowPosition=Window->GetPositionInScreen();
        PreviousResolution=PreviewWindowSize;
        S->WindowWidth=PreviewWindowSize.X;S->WindowHeight=PreviewWindowSize.Y;
        S->WindowX=FMath::RoundToInt(PreviewWindowPosition.X);S->WindowY=FMath::RoundToInt(PreviewWindowPosition.Y);
        S->bHasWindowPlacement=true;
    }
    DisplayChoice=FMath::Clamp(Choice,0,2);
    const auto Mode=DisplayChoice==0?EWindowMode::Fullscreen:DisplayChoice==1?EWindowMode::WindowedFullscreen:EWindowMode::Windowed;
    if(Mode==EWindowMode::Fullscreen&&(!GDynamicRHI||FCString::Stricmp(GDynamicRHI->GetName(),TEXT("D3D11"))!=0)) {
        DisplayStatus=TEXT("독점 전체화면 시험에는 DX11 실행이 필요해요.");DisplayChoice=PreviousMode==EWindowMode::Fullscreen?0:PreviousMode==EWindowMode::WindowedFullscreen?1:2;return;
    }
    int32 Hz=0;const auto Native=NativeDisplay(*this,Hz);MonitorHz=Hz;
    S->SetScreenResolution(Mode==EWindowMode::Windowed?FIntPoint(FMath::Max(640,S->WindowWidth),FMath::Max(360,S->WindowHeight)):Native);
    SetCVar(TEXT("r.FullScreenMode"),0);S->SetFullscreenMode(Mode);S->ApplyResolutionSettings(false);
    if(Mode==EWindowMode::Windowed&&S->bHasWindowPlacement)Window->MoveWindowTo(FVector2D(S->WindowX,S->WindowY));
    bDisplayPending=true;bExclusiveConfirmed=false;ConfirmUntil=FPlatformTime::Seconds()+15;NextProbe=FPlatformTime::Seconds()+1;
    DisplayStatus=TEXT("출력 전환을 확인하고 있어요…");BuildMenu();
}
void ABasicFPSController::ConfirmDisplay() {
    if(!bDisplayPending)return;
    if(Settings()->GetFullscreenMode()==EWindowMode::Fullscreen&&!bExclusiveConfirmed){DisplayStatus=TEXT("DXGI 독점 상태를 확인하기 전에는 유지할 수 없어요.");return;}
    bDisplayPending=false;Settings()->ConfirmVideoMode();SavePreferences();DisplayStatus+=TEXT(" · 설정을 저장했어요.");BuildMenu();
}
void ABasicFPSController::RevertDisplay() {
    if(!bDisplayPending)return;bDisplayPending=false;++DisplayGeneration;
    Settings()->SetFullscreenMode(PreviousMode);Settings()->SetScreenResolution(PreviousResolution);Settings()->ApplyResolutionSettings(false);
    if(PreviousMode==EWindowMode::Windowed&&GEngine->GameViewport->GetWindow().IsValid())GEngine->GameViewport->GetWindow()->MoveWindowTo(PreviewWindowPosition);
    DisplayChoice=PreviousMode==EWindowMode::Fullscreen?0:PreviousMode==EWindowMode::WindowedFullscreen?1:2;
    DisplayStatus=TEXT("이전 화면 설정으로 되돌렸어요.");NextProbe=FPlatformTime::Seconds()+1;BuildMenu();
}
void ABasicFPSController::ProbeDisplay() {
    if(!GEngine->GameViewport||!GEngine->GameViewport->Viewport)return;
    int32 Hz=0;NativeDisplay(*this,Hz);MonitorHz=Hz;
    FViewportRHIRef Viewport=GEngine->GameViewport->Viewport->GetViewportRHI();if(!Viewport.IsValid())return;
    bProbePending=true;const int32 Generation=DisplayGeneration;const TWeakObjectPtr<ABasicFPSController> Weak(this);
    ENQUEUE_RENDER_COMMAND(BasicFPSQueryFullscreen)([Weak,Viewport,Generation](FRHICommandListImmediate&) {
        bool bSuccess=false,bExclusive=false;
#if PLATFORM_WINDOWS
        if(GDynamicRHI&&FCString::Stricmp(GDynamicRHI->GetName(),TEXT("D3D11"))==0) {
            if(auto* Swap=static_cast<IDXGISwapChain*>(Viewport->GetNativeSwapChain())) {
                BOOL NativeExclusive=FALSE;bSuccess=SUCCEEDED(Swap->GetFullscreenState(&NativeExclusive,nullptr));bExclusive=NativeExclusive!=FALSE;
            }
        }
#endif
        AsyncTask(ENamedThreads::GameThread,[Weak,bSuccess,bExclusive,Generation] {
            auto* C=Weak.Get();if(!C||C->bEndingPlay)return;C->bProbePending=false;if(C->DisplayGeneration!=Generation)return;C->bExclusiveConfirmed=bSuccess&&bExclusive;
            const auto Mode=C->Settings()->GetFullscreenMode();
            if(Mode==EWindowMode::Fullscreen) {
                C->DisplayStatus=C->bExclusiveConfirmed?TEXT("DXGI 독점 전체화면 상태를 확인했어요. Windows 최적화·MUX 상태는 별도예요."):TEXT("독점 전체화면 상태를 확인하지 못했어요.");
                if(C->bDisplayPending&&!C->bExclusiveConfirmed) {
                    C->RevertDisplay();C->DisplayStatus=TEXT("독점 전환을 확인하지 못해 이전 화면으로 복구했어요. DX11·화면 포커스를 확인해 주세요.");
                }
            }else C->DisplayStatus=Mode==EWindowMode::WindowedFullscreen?TEXT("전체화면 창모드예요."):TEXT("창모드예요.");
            UE_LOG(LogTemp,Display,TEXT("BasicFPS display: %s"),*C->DisplayStatus);
        });
    });
}
FString ABasicFPSController::DiagnosticLine() const {
    const double CPU=FPlatformTime::ToMilliseconds(GGameThreadTime);
    const uint32 GPUCycles=RHIGetGPUFrameCycles();
    const FString GPUTime=GPUCycles?FString::Printf(TEXT("%.2f ms"),FPlatformTime::ToMilliseconds(GPUCycles)):TEXT("측정값 없음");
    const auto Size=GEngine->GameViewport&&GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetSizeXY():FIntPoint::ZeroValue;
    const auto Mode=Settings()->GetFullscreenMode();
    const TCHAR* ModeText=Mode==EWindowMode::Fullscreen?(bExclusiveConfirmed?TEXT("독점 확인"):TEXT("독점 요청·미확인")):Mode==EWindowMode::WindowedFullscreen?TEXT("전체화면 창모드"):TEXT("창모드");
    return FString::Printf(TEXT("%s · %s · %d×%d %dHz · %s · CPU %.2f ms · GPU %s"),*GRHIAdapterName,GDynamicRHI?GDynamicRHI->GetName():TEXT("미확인"),Size.X,Size.Y,MonitorHz,ModeText,CPU,*GPUTime);
}
FString ABasicFPSController::MenuStatus() const {
    FString Text=Status+TEXT("\n")+DisplayStatus;
    if(bDisplayPending)Text+=FString::Printf(TEXT("\n%d초 안에 ‘화면 설정 유지’를 눌러 주세요."),FMath::Max(0,FMath::CeilToInt(ConfirmUntil-FPlatformTime::Seconds())));
    if(bRecording)Text+=TEXT("\n성능 기록 중이에요. 최대 3분 뒤 자동으로 저장해요.");
    return Text;
}
void ABasicFPSController::SavePreferences() {
    if(bDisplayPending||bSmokeTest||!GEngine||!GEngine->GameViewport)return;
    auto* S=Settings();const auto Window=GEngine->GameViewport->GetWindow();
    if(S->GetFullscreenMode()==EWindowMode::Windowed&&Window.IsValid()&&GEngine->GameViewport->Viewport) {
        const auto Resolution=GEngine->GameViewport->Viewport->GetSizeXY();const auto P=Window->GetPositionInScreen();
        S->WindowWidth=Resolution.X;S->WindowHeight=Resolution.Y;S->WindowX=FMath::RoundToInt(P.X);S->WindowY=FMath::RoundToInt(P.Y);S->bHasWindowPlacement=true;
        S->SetScreenResolution(Resolution);
    }
    S->SaveSettings();
}
void ABasicFPSController::ToggleRecording() {
    if(!bRecording) {StatsRows={TEXT("seconds,game_fps,limit,msaa,diagnostics")};StatsElapsed=0;bRecording=true;return;}
    bRecording=false;const FString Directory=FPaths::ProjectSavedDir()/TEXT("Diagnostics");IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Filename=Directory/(TEXT("performance-")+FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT(".csv"));
    const bool bSaved=FFileHelper::SaveStringToFile(FString::Join(StatsRows,TEXT("\r\n")),*Filename,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    Status=bSaved?TEXT("성능 기록을 저장했어요: ")+Filename:TEXT("성능 기록을 저장하지 못했어요. 저장 공간을 확인해 주세요.");StatsRows.Reset();
}
void ABasicFPSController::SaveScreenshot() {
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Diagnostics");IFileManager::Get().MakeDirectory(*Directory,true);
    FScreenshotRequest::RequestScreenshot(Directory/(TEXT("screen-")+FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT(".png")),true,false);
}
