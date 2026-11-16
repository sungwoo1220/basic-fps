#include "BasicFPSGame.h"
#include "HAL/PlatformMisc.h"

// Runs only with an explicit command-line flag, against the real packaged client/server.
// This has no rendering and must never be reported as a GPU/fullscreen test.
void ABasicFPSController::TickSmokeTest(double Now) {
    if(SmokePhase==99)return;
    auto Fail=[this](const TCHAR* Message) {
        SmokePhase=99;UE_LOG(LogTemp,Error,TEXT("BasicFPSSmokeTest FAIL: %s"),Message);
        FPlatformMisc::RequestExitWithStatus(false,1);
    };
    if(Now-SmokeStarted>30){Fail(TEXT("Connection, acknowledgement or reload timed out"));return;}
    if(SmokePhase==0) {
        if(!bConnected||!bInitialized||!Players.Contains(OwnId))return;
        const auto& P=Players[OwnId];SmokeX=P.X;SmokeZ=P.Z;SmokePhase=1;SmokePhaseAt=Now;
        SetMenu(false);UE_LOG(LogTemp,Display,TEXT("BasicFPSSmokeTest: connected, moving"));return;
    }
    if(!bConnected||!bInitialized){Fail(TEXT("Connection lost"));return;}
    if(SmokePhase==1&&Now-SmokePhaseAt>1.2) {
        const auto* P=Players.Find(OwnId);
        if(!P||ServerAck<=0||FMath::Square(P->X-SmokeX)+FMath::Square(P->Z-SmokeZ)<.0625){Fail(TEXT("Server did not acknowledge movement"));return;}
        SmokePhase=2;SmokePhaseAt=Now;return;
    }
    if(SmokePhase==2&&Now-SmokePhaseAt>.8) {
        if(ServerAmmo>=12||ServerAmmo<=0){Fail(TEXT("Server did not consume ammunition"));return;}
        SmokePhase=3;SmokePhaseAt=Now;Reload();return;
    }
    if(SmokePhase==3&&ReloadEnd>Now+.1){SmokePhase=4;SmokePhaseAt=Now;return;}
    if(SmokePhase==4&&Now-SmokePhaseAt>2.5&&ServerAmmo==12&&ReloadEnd<=Now) {
        SmokePhase=99;UE_LOG(LogTemp,Display,TEXT("BasicFPSSmokeTest PASS: connected, authoritative movement acknowledged, firing consumed ammo, manual reload refilled magazine. Graphics untested."));
        FPlatformMisc::RequestExitWithStatus(false,0);
    }
}
