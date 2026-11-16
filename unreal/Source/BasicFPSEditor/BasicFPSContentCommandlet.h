#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "BasicFPSContentCommandlet.generated.h"

UCLASS()
class UBasicFPSContentCommandlet : public UCommandlet {
    GENERATED_BODY()
public:
    UBasicFPSContentCommandlet();
    virtual int32 Main(const FString& Params) override;
};
