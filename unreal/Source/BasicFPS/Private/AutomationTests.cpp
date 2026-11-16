#include "BasicFPSGame.h"
#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBasicFPSMovementTest,"BasicFPS.Rules.MovementParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBasicFPSMovementTest::RunTest(const FString&) {TestTrue(TEXT("7,200 prediction steps match authoritative JavaScript"),BasicFPS::RunGoldenCases());return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBasicFPSSettingsTest,"BasicFPS.Settings.MSAAAndUncapped",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBasicFPSSettingsTest::RunTest(const FString&) {
    auto* Settings=NewObject<UBasicFPSSettings>();Settings->FrameCap=0;Settings->MSAASamples=0;Settings->ApplyGraphics(false);
    TestEqual(TEXT("Uncapped"),IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetInt(),0);
    TestEqual(TEXT("Disabled AA does not fall back to TAA"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt(),0);
    TestEqual(TEXT("MSAA disabled uses sample count 1"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.MSAACount"))->GetInt(),1);
    Settings->MSAASamples=2;Settings->FrameCap=300;Settings->ApplyGraphics(false);
    TestEqual(TEXT("MSAA2"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.MSAACount"))->GetInt(),2);
    TestEqual(TEXT("Cap300"),IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetInt(),300);
    Settings->MSAASamples=4;Settings->ApplyGraphics(false);
    TestEqual(TEXT("MSAA4"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.MSAACount"))->GetInt(),4);
    TestEqual(TEXT("Forward MSAA, not FXAA/TAA/TSR"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt(),3);
    TestEqual(TEXT("Native resolution"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetInt(),100);
    TestEqual(TEXT("No DPI upscaling"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.SecondaryScreenPercentage.GameViewport"))->GetInt(),100);
    TestEqual(TEXT("No dynamic resolution"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicRes.OperationMode"))->GetInt(),0);
    TestEqual(TEXT("VSync disabled"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))->GetInt(),0);
    Settings->ApplyGraphics(true);
    TestEqual(TEXT("Menu limited to 30"),IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetInt(),30);
    return true;
}
#endif
