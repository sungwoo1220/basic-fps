#include "BasicFPSContentCommandlet.h"
#include "BasicFPSGame.h"
#include "Modules/ModuleManager.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl,BasicFPSEditor)
UBasicFPSContentCommandlet::UBasicFPSContentCommandlet() {IsClient=false;IsServer=false;IsEditor=true;LogToConsole=true;}
int32 UBasicFPSContentCommandlet::Main(const FString&) {
    // Generate our own tiny assets on the build PC. Do not check engine/starter assets into git.
    const FString Name=TEXT("/Game/Materials/Flat");
    UPackage* Package=CreatePackage(*Name);
    auto* Material=NewObject<UMaterial>(Package,TEXT("Flat"),RF_Public|RF_Standalone);
    Material->SetShadingModel(MSM_Unlit);
    auto* Color=NewObject<UMaterialExpressionVectorParameter>(Material);Color->ParameterName=TEXT("Color");Color->DefaultValue=FLinearColor(.2,.3,.4);
    Material->GetExpressionCollection().AddExpression(Color);
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0,Color);
    Material->PostEditChange();FAssetRegistryModule::AssetCreated(Material);
    const FString File=FPackageName::LongPackageNameToFilename(Name,FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;
    if(!UPackage::SavePackage(Package,Material,*File,Save))return 1;
    UWorld* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if(!World)return 2;
    World->GetWorldSettings()->DefaultGameMode=ABasicFPSGameMode::StaticClass();
    IFileManager::Get().MakeDirectory(*(FPaths::ProjectContentDir()/TEXT("Maps")),true);
    if(!UEditorLoadingAndSavingUtils::SaveMap(World,TEXT("/Game/Maps/Arena")))return 3;
    UE_LOG(LogTemp,Display,TEXT("Basic FPS content generation completed."));return 0;
}
