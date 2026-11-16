#include "BasicFPSGame.h"
#include "Modules/ModuleManager.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "IWebSocket.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/SWindow.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl,BasicFPS,"BasicFPS");

static UMaterialInstanceDynamic* Flat(UObject* Owner,const FLinearColor& Color) {
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/Flat.Flat"));
    if(!Base)return nullptr;
    auto* Material=UMaterialInstanceDynamic::Create(Base,Owner);
    Material->SetVectorParameterValue(TEXT("Color"),Color);return Material;
}
ABasicFPSGeometry::ABasicFPSGeometry() {
    Mesh=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Boxes"));SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Mesh->SetStaticMesh(Cube.Object);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);Mesh->SetCanEverAffectNavigation(false);
}
void ABasicFPSGeometry::Color(const FLinearColor& Value) { if(auto* Material=Flat(this,Value))Mesh->SetMaterial(0,Material); }
void ABasicFPSGeometry::Box(const FVector& P,const FVector& S) { Mesh->AddInstance(FTransform(FQuat::Identity,P,S/100.0)); }

ABasicFPSPawn::ABasicFPSPawn() {
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));Camera->SetupAttachment(RootComponent);
    Camera->SetRelativeLocation(FVector(0,0,160));Camera->FieldOfView=103;Camera->bUsePawnControlRotation=false;
    WeaponRoot=CreateDefaultSubobject<USceneComponent>(TEXT("Weapon"));WeaponRoot->SetupAttachment(Camera);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto Add=[&](const TCHAR* Name,FVector P,FVector S) {
        auto* Component=CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Component->SetupAttachment(WeaponRoot);Component->SetStaticMesh(Cube.Object);
        Component->SetRelativeLocation(P);Component->SetRelativeScale3D(S/100);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);Component->SetCastShadow(false);
        return Component;
    };
    Add(TEXT("GunBody"),FVector(43,25,-23),FVector(32,14,15));
    Add(TEXT("GunBarrel"),FVector(68,25,-21),FVector(20,6.5,6.5));
    Flash=Add(TEXT("MuzzleFlash"),FVector(79,25,-21),FVector(5,11,11));Flash->SetVisibility(false);
}
void ABasicFPSPawn::BeginPlay() {
    Super::BeginPlay();
    TArray<UStaticMeshComponent*> Components;GetComponents(Components);
    for(auto* Component:Components)if(auto* Material=Flat(this,Component==Flash?FLinearColor(1,.7,.25):FLinearColor(.08,.15,.2)))Component->SetMaterial(0,Material);
}
void ABasicFPSPawn::AnimateWeapon(double Now,double End,double Shot,bool bPlaying) {
    const double Progress=End>Now?1-(End-Now)/2.3:0;
    const double Pose=Progress>0?FMath::Sin(Progress*PI):0;
    WeaponRoot->SetRelativeLocation(FVector(-Pose*15,0,-Pose*18));
    WeaponRoot->SetRelativeRotation(FRotator(Pose*20,0,-Pose*37));
    Flash->SetVisibility(bPlaying&&Now-Shot<.05);
}

ABasicFPSGameMode::ABasicFPSGameMode() {
    DefaultPawnClass=ABasicFPSPawn::StaticClass();PlayerControllerClass=ABasicFPSController::StaticClass();HUDClass=ABasicFPSHUD::StaticClass();
}
void ABasicFPSGameMode::BeginPlay() {
    Super::BeginPlay();
    auto* Floor=GetWorld()->SpawnActor<ABasicFPSGeometry>();Floor->Color(FLinearColor(.23,.32,.38));
    Floor->Box(ABasicFPSController::Position(0,-.125,0),ABasicFPSController::Size(40,.25,40));
    auto* Walls=GetWorld()->SpawnActor<ABasicFPSGeometry>();Walls->Color(FLinearColor(.04,.09,.13));
    for(int Sign:{-1,1}) {
        Walls->Box(ABasicFPSController::Position(0,2,Sign*20.5),ABasicFPSController::Size(41,4,1));
        Walls->Box(ABasicFPSController::Position(Sign*20.5,2,0),ABasicFPSController::Size(1,4,40));
    }
    auto* Obstacles=GetWorld()->SpawnActor<ABasicFPSGeometry>();Obstacles->Color(FLinearColor(.13,.22,.29));
    for(const auto& Box:BasicFPS::Boxes)Obstacles->Box(ABasicFPSController::Position(Box.x,Box.h/2,Box.z),ABasicFPSController::Size(Box.w,Box.h,Box.d));
    auto* Grid=GetWorld()->SpawnActor<ABasicFPSGeometry>();Grid->Color(FLinearColor(.3,.4,.46));
    for(int N=-20;N<=20;N+=2) {
        Grid->Box(ABasicFPSController::Position(N,.003,0),ABasicFPSController::Size(.007,.003,40));
        Grid->Box(ABasicFPSController::Position(0,.003,N),ABasicFPSController::Size(40,.003,.007));
    }
}

ABasicFPSController::ABasicFPSController() { PrimaryActorTick.bCanEverTick=true; }
UBasicFPSSettings* ABasicFPSController::Settings() const { return Cast<UBasicFPSSettings>(GEngine->GetGameUserSettings()); }
void ABasicFPSController::BeginPlay() {
    Super::BeginPlay();
    Address=TEXT("http://127.0.0.1:3000");FParse::Value(FCommandLine::Get(),TEXT("ServerUrl="),Address);
    bSmokeTest=FParse::Param(FCommandLine::Get(),TEXT("BasicFPSSmokeTest"));SmokeStarted=FPlatformTime::Seconds();
    Settings()->LoadSettings(false);Settings()->ApplyGraphics(true);
    if(bSmokeTest)Settings()->FrameCap=60;
    else if(Settings()->GetFullscreenMode()==EWindowMode::Windowed&&Settings()->bHasWindowPlacement&&GEngine->GameViewport->GetWindow().IsValid())
        GEngine->GameViewport->GetWindow()->MoveWindowTo(FVector2D(Settings()->WindowX,Settings()->WindowY));
    DisplayChoice=Settings()->GetFullscreenMode()==EWindowMode::Fullscreen?0:Settings()->GetFullscreenMode()==EWindowMode::WindowedFullscreen?1:2;
    if(FSlateApplication::IsInitialized())FocusHandle=FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this,&ABasicFPSController::OnFocusChanged);
    SetMenu(true);Connect();NextProbe=FPlatformTime::Seconds()+1;
}
void ABasicFPSController::EndPlay(const EEndPlayReason::Type Reason) {
    bEndingPlay=true;
    if(bDisplayPending)RevertDisplay();
    if(!bSmokeTest)SavePreferences();
    if(bRecording)ToggleRecording();
    ++ConnectionGeneration;if(Socket){Socket->Close();Socket.Reset();}
    if(FSlateApplication::IsInitialized())FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(FocusHandle);
    if(Menu.IsValid()&&GEngine&&GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
    Super::EndPlay(Reason);
}
void ABasicFPSController::SetupInputComponent() {
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&ABasicFPSController::ToggleMenu);
    InputComponent->BindKey(EKeys::R,IE_Pressed,this,&ABasicFPSController::Reload);
}
void ABasicFPSController::OnFocusChanged(bool bFocused) {
    if(bSmokeTest||bEndingPlay)return;
    if(!bFocused){if(bDisplayPending)RevertDisplay();SetMenu(true);}else NextProbe=FPlatformTime::Seconds()+.5;
}
void ABasicFPSController::ToggleMenu() { if(bMenuOpen)Join();else SetMenu(true); }
void ABasicFPSController::SetMenu(bool bOpen) {
    bMenuOpen=bOpen;bShowMouseCursor=bOpen;Accumulator=0;
    if(bOpen) {
        if(!bSmokeTest)BuildMenu();FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        if(Menu.IsValid())Mode.SetWidgetToFocus(Menu);SetInputMode(Mode);
    }else {
        if(Menu.IsValid())GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());Menu.Reset();
        FInputModeGameOnly Mode;SetInputMode(Mode);
    }
    Settings()->ApplyGraphics(bOpen);
}
void ABasicFPSController::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    const double Now=FPlatformTime::Seconds(),Dt=FMath::Min(static_cast<double>(DeltaSeconds),.1);
    if(bSmokeTest)TickSmokeTest(Now);
    FPSWindow+=DeltaSeconds;++FPSFrames;
    if(FPSWindow>=1){FPS=FPSFrames/FPSWindow;FPSWindow=0;FPSFrames=0;}
    if(bDisplayPending&&Now>=ConfirmUntil)RevertDisplay();
    if(!bSmokeTest&&Now>=NextProbe&&!bProbePending){NextProbe=Now+5;ProbeDisplay();}
    if(IsPlaying()&&!bSmokeTest) {
        float X=0,Y=0;GetInputMouseDelta(X,Y);
        Yaw=FMath::Fmod(Yaw-X*PI/180*.07*Settings()->Sensitivity,PI*2);
        Pitch=FMath::Clamp(Pitch-Y*PI/180*.07*Settings()->Sensitivity,-1.45,1.45);
    }
    Accumulator=FMath::Min(Accumulator+Dt,.1);
    TArray<BasicFPS::Input> Outgoing;
    while(bConnected&&bInitialized&&Accumulator>=BasicFPS::Step&&Outgoing.Num()<6) {
        Accumulator-=BasicFPS::Step;
        if(Pending.Num()>=120){Status=TEXT("네트워크 응답이 지연되고 있어요.");SetMenu(true);break;}
        int Bits=0;
        if(IsPlaying()) {
            if(bSmokeTest)Bits=SmokePhase==1?1:SmokePhase==2?16:0;
            else {
            if(IsInputKeyDown(EKeys::W))Bits|=1;if(IsInputKeyDown(EKeys::S))Bits|=2;
            if(IsInputKeyDown(EKeys::A))Bits|=4;if(IsInputKeyDown(EKeys::D))Bits|=8;
            if(IsInputKeyDown(EKeys::LeftMouseButton))Bits|=16;
            if(IsInputKeyDown(EKeys::LeftShift)||IsInputKeyDown(EKeys::RightShift))Bits|=32;
            }
        }
        BasicFPS::Input Input;Input.seq=++Sequence;Input.keys=Bits;Input.yaw=Yaw;Input.pitch=Pitch;Input.epoch=Epoch;Input.viewTime=Now*1000+ServerOffsetMs-100;
        BasicFPS::Move(Predicted,Input);Pending.Add(Input);Outgoing.Add(Input);
        if((Bits&16)&&Ammo>0&&Now>=ReloadEnd&&Now-LastShot>=BasicFPS::ShotInterval){LastShot=Now;--Ammo;}
    }
    if(Outgoing.Num())FlushInputs(Outgoing);
    const double Decay=FMath::Exp(-20*Dt);OffsetX*=Decay;OffsetZ*=Decay;
    BasicFPS::State Render=Predicted;
    if(Pending.Num()&&IsPlaying()) {
        BasicFPS::Move(Render,Pending.Last());const double Alpha=Accumulator/BasicFPS::Step;
        Render.x=Predicted.x+(Render.x-Predicted.x)*Alpha;Render.z=Predicted.z+(Render.z-Predicted.z)*Alpha;
    }
    if(auto* Pawn=Cast<ABasicFPSPawn>(GetPawn())) {
        Pawn->SetActorLocation(Position(Render.x+OffsetX,0,Render.z+OffsetZ));
        Pawn->SetActorRotation(FRotator(0,-FMath::RadiansToDegrees(Yaw),0));
        Pawn->Camera->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(Pitch),0,0));
        Pawn->AnimateWeapon(Now,ReloadEnd,LastShot,IsPlaying());
    }
    UpdateRemotes(Now);
    Feed.RemoveAll([Now](const auto& Item){return Item.Value<=Now;});
    if(bRecording) {
        StatsElapsed+=DeltaSeconds;
        if(StatsElapsed>=.1){StatsElapsed=0;StatsRows.Add(FString::Printf(TEXT("%.3f,%.2f,%d,%d,\"%s\""),Now,FPS,Settings()->FrameCap,Settings()->MSAASamples,*DiagnosticLine().Replace(TEXT("\""),TEXT("\"\""))));}
        if(StatsRows.Num()>=1801)ToggleRecording();
    }
}

void ABasicFPSHUD::DrawHUD() {
    Super::DrawHUD();auto* C=Cast<ABasicFPSController>(PlayerOwner);if(!C||!Canvas)return;
    const float Scale=FMath::Clamp(Canvas->ClipY/800.f,.75f,2.0f);
    DrawText(FString::Printf(TEXT("%.0f FPS · %s"),C->FPS,*C->DiagnosticLine()),FLinearColor(.8,.9,1),18,16,nullptr,Scale);
    DrawText(FString::Printf(TEXT("HP %d | %d / 12 | K %d / D %d | %d명"),C->Players.Contains(C->OwnId)?C->Players[C->OwnId].HP:100,C->Ammo,C->Players.Contains(C->OwnId)?C->Players[C->OwnId].Kills:0,C->Players.Contains(C->OwnId)?C->Players[C->OwnId].Deaths:0,C->Players.Num()),FLinearColor::White,20,Canvas->ClipY-40*Scale,nullptr,Scale);
    if(!C->IsPlaying())return;
    const float X=Canvas->ClipX/2,Y=Canvas->ClipY/2;
    DrawLine(X-5,Y,X+5,Y,FLinearColor::White);DrawLine(X,Y-5,X,Y+5,FLinearColor::White);
    if(FPlatformTime::Seconds()<C->HitUntil){DrawLine(X-9,Y-9,X+9,Y+9,FLinearColor::Yellow);DrawLine(X+9,Y-9,X-9,Y+9,FLinearColor::Yellow);}
    float FeedY=70;
    for(const auto& Item:C->Feed){DrawText(Item.Key,FLinearColor::White,20,FeedY,nullptr,Scale);FeedY+=24*Scale;}
    if(C->IsInputKeyDown(EKeys::Tab)) {
        TArray<FBasicFPSRemote> Board;C->Players.GenerateValueArray(Board);Board.Sort([](const auto& A,const auto& B){return A.Kills>B.Kills;});
        DrawRect(FLinearColor(0,0,0,.85),Canvas->ClipX*.3,100,Canvas->ClipX*.4,50+Board.Num()*30*Scale);
        float YRow=120;for(const auto& P:Board){DrawText(FString::Printf(TEXT("%s    %d K / %d D"),*P.Name,P.Kills,P.Deaths),FLinearColor::White,Canvas->ClipX*.32,YRow,nullptr,Scale);YRow+=30*Scale;}
    }
}
