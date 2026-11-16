#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "GameFramework/GameUserSettings.h"
#include "Simulation.h"
#include "BasicFPSGame.generated.h"

class IWebSocket;
class SWidget;
class UInstancedStaticMeshComponent;
class UCameraComponent;
class UStaticMeshComponent;

UCLASS(Config=GameUserSettings)
class BASICFPS_API UBasicFPSSettings : public UGameUserSettings {
    GENERATED_BODY()
public:
    UPROPERTY(Config) FString Nickname=TEXT("Player");
    UPROPERTY(Config) float Sensitivity=0.350f;
    UPROPERTY(Config) int32 MSAASamples=4;
    UPROPERTY(Config) int32 FrameCap=0;
    UPROPERTY(Config) int32 WindowWidth=1280;
    UPROPERTY(Config) int32 WindowHeight=800;
    UPROPERTY(Config) int32 WindowX=-1;
    UPROPERTY(Config) int32 WindowY=-1;
    UPROPERTY(Config) bool bHasWindowPlacement=false;
    void ApplyGraphics(bool bMenu);
};

UCLASS()
class BASICFPS_API ABasicFPSGeometry : public AActor {
    GENERATED_BODY()
public:
    ABasicFPSGeometry();
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Mesh;
    void Color(const FLinearColor& Value);
    void Box(const FVector& Position,const FVector& Size);
};

UCLASS()
class BASICFPS_API ABasicFPSPawn : public APawn {
    GENERATED_BODY()
public:
    ABasicFPSPawn();
    virtual void BeginPlay() override;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<USceneComponent> WeaponRoot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Flash;
    void AnimateWeapon(double Now,double ReloadEnd,double LastShot,bool bPlaying);
};

struct FBasicFPSSnapshot { double Time=0,X=0,Z=0,Yaw=0;int Epoch=0; };
struct FBasicFPSRemote {
    FString Name;
    int HP=100,Kills=0,Deaths=0,Epoch=0;
    double X=0,Z=0;
    TArray<FBasicFPSSnapshot> Samples;
    TWeakObjectPtr<ABasicFPSGeometry> Body,Edges;
};

UCLASS()
class BASICFPS_API ABasicFPSController : public APlayerController {
    GENERATED_BODY()
public:
    ABasicFPSController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void SetupInputComponent() override;
    virtual void Tick(float DeltaSeconds) override;
    void ToggleMenu();
    void SetMenu(bool bOpen);
    void BuildMenu();
    void Connect();
    void HandleMessage(const FString& Message);
    void FlushInputs(const TArray<BasicFPS::Input>& Inputs);
    void SendObject(const TSharedRef<class FJsonObject>& Object);
    void Join();
    void Reload();
    void SetBots(int32 Count);
    void UpdateRemotes(double Now);
    void ReceiveLocal(const TSharedPtr<class FJsonObject>& Player);
    void PreviewDisplay(int32 Choice);
    void ConfirmDisplay();
    void RevertDisplay();
    void ProbeDisplay();
    void OnFocusChanged(bool bFocused);
    void ToggleRecording();
    void SaveScreenshot();
    void SavePreferences();
    void TickSmokeTest(double Now);
    FString DiagnosticLine() const;
    FString MenuStatus() const;
    bool IsPlaying() const { return !bMenuOpen && bConnected && bInitialized; }
    static FVector Position(double X,double Y,double Z) { return FVector(-Z*100,X*100,Y*100); }
    static FVector Size(double W,double H,double D) { return FVector(D*100,W*100,H*100); }
    UBasicFPSSettings* Settings() const;

    FString Address,OwnId,Status=TEXT("연결 준비 중…"),DisplayStatus=TEXT("출력 상태 확인 전");
    bool bMenuOpen=true,bConnected=false,bInitialized=false,bRecording=false;
    bool bDisplayPending=false,bExclusiveConfirmed=false,bProbePending=false;
    bool bEndingPlay=false,bSmokeTest=false;
    int Epoch=0,Ammo=12,ServerAmmo=12,ConnectionGeneration=0,DisplayChoice=2,DisplayGeneration=0;
    int SmokePhase=0;
    double SmokeStarted=0,SmokePhaseAt=0,SmokeX=0,SmokeZ=0;
    std::int64_t ServerAck=0;
    int32 MonitorHz=0;
    double Yaw=0,Pitch=0,Accumulator=0,ServerOffsetMs=0;
    double OffsetX=0,OffsetZ=0,ReloadEnd=0,LastShot=-1000,HitUntil=0;
    double ConfirmUntil=0,NextProbe=0,StatsElapsed=0;
    double FPS=0,FPSWindow=0;int FPSFrames=0;
    BasicFPS::State Predicted;
    std::int64_t Sequence=0;
    TArray<BasicFPS::Input> Pending;
    TMap<FString,FBasicFPSRemote> Players;
    TArray<TPair<FString,double>> Feed;
    TArray<FString> StatsRows;
    TSharedPtr<IWebSocket> Socket;
    TSharedPtr<SWidget> Menu;
    TArray<TSharedPtr<FString>> ModeOptions,AAOptions;
    TSharedPtr<FString> SelectedMode,SelectedAA;
    FDelegateHandle FocusHandle;
    FIntPoint PreviousResolution;
    EWindowMode::Type PreviousMode=EWindowMode::Windowed;
    FIntPoint PreviewWindowSize;
    FVector2D PreviewWindowPosition=FVector2D(-1,-1);
};

UCLASS()
class BASICFPS_API ABasicFPSHUD : public AHUD {
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

UCLASS()
class BASICFPS_API ABasicFPSGameMode : public AGameModeBase {
    GENERATED_BODY()
public:
    ABasicFPSGameMode();
    virtual void BeginPlay() override;
};
