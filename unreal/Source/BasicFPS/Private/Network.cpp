#include "BasicFPSGame.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Async/Async.h"
#include "Engine/World.h"

namespace {
double Number(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,double Default=0) {
    double Value=Default;return O.IsValid()&&O->TryGetNumberField(Key,Value)&&FMath::IsFinite(Value)?Value:Default;
}
FString String(const TSharedPtr<FJsonObject>& O,const TCHAR* Key) { FString Value;if(O.IsValid())O->TryGetStringField(Key,Value);return Value; }
void BuildFigure(ABasicFPSController& C,FBasicFPSRemote& Remote) {
    auto* Body=C.GetWorld()->SpawnActor<ABasicFPSGeometry>();Body->Color(FLinearColor(.035,.04,.045));
    auto* Edges=C.GetWorld()->SpawnActor<ABasicFPSGeometry>();Edges->Color(FLinearColor(1,.02,.015));
    for(const auto& P:BasicFPS::Parts) {
        Body->Box(C.Position(P.x,P.y,P.z),C.Size(P.w,P.h,P.d));
        constexpr double T=.012;
        for(int A:{-1,1})for(int B:{-1,1}) {
            Edges->Box(C.Position(P.x,P.y+A*P.h/2,P.z+B*P.d/2),C.Size(P.w+T,T,T));
            Edges->Box(C.Position(P.x+A*P.w/2,P.y,P.z+B*P.d/2),C.Size(T,P.h+T,T));
            Edges->Box(C.Position(P.x+A*P.w/2,P.y+B*P.h/2,P.z),C.Size(T,T,P.d+T));
        }
    }
    Remote.Body=Body;Remote.Edges=Edges;
}
}

void ABasicFPSController::Connect() {
    FString URL=Address.TrimStartAndEnd();
    if(URL.Len()>2048||URL.Contains(TEXT("@"))||URL.Contains(TEXT("\n"))||URL.Contains(TEXT("\r"))||URL.Contains(TEXT(" "))||
       !(URL.StartsWith(TEXT("http://"))||URL.StartsWith(TEXT("https://")))) {
        Status=TEXT("http://서버주소:3000 형식의 주소를 입력해 주세요.");return;
    }
    URL=URL.StartsWith(TEXT("https://"))?TEXT("wss://")+URL.Mid(8):TEXT("ws://")+URL.Mid(7);
    const int Generation=++ConnectionGeneration;
    if(Socket){Socket->Close();Socket.Reset();}
    bConnected=false;bInitialized=false;OwnId.Empty();Pending.Reset();Sequence=0;Epoch=0;Accumulator=0;
    for(auto& Entry:Players){if(Entry.Value.Body.IsValid())Entry.Value.Body->Destroy();if(Entry.Value.Edges.IsValid())Entry.Value.Edges->Destroy();}Players.Reset();
    Status=TEXT("서버에 연결하고 있어요…");
    Socket=FWebSocketsModule::Get().CreateWebSocket(URL);
    const TWeakObjectPtr<ABasicFPSController> Weak(this);
    Socket->OnConnected().AddLambda([Weak,Generation]() {
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation] {
            if(auto* C=Weak.Get();C&&C->ConnectionGeneration==Generation){C->bConnected=true;C->Status=TEXT("연결됐어요. 상태를 기다리고 있어요…");}
        });
    });
    Socket->OnMessage().AddLambda([Weak,Generation](const FString& Message) {
        if(Message.Len()>262144)return;
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation,Message] {
            if(auto* C=Weak.Get();C&&C->ConnectionGeneration==Generation)C->HandleMessage(Message);
        });
    });
    auto Disconnected=[Weak,Generation](const FString& Reason) {
        AsyncTask(ENamedThreads::GameThread,[Weak,Generation,Reason] {
            if(auto* C=Weak.Get();C&&C->ConnectionGeneration==Generation) {
                C->bConnected=false;C->bInitialized=false;C->Status=TEXT("연결이 종료됐어요. 주소를 확인하고 다시 연결해 주세요. ")+Reason.Left(180);C->SetMenu(true);
            }
        });
    };
    Socket->OnConnectionError().AddLambda(Disconnected);
    Socket->OnClosed().AddLambda([Disconnected](int32,const FString& Reason,bool){Disconnected(Reason);});
    Socket->Connect();
}
void ABasicFPSController::SendObject(const TSharedRef<FJsonObject>& Object) {
    if(!bConnected||!Socket.IsValid()||!Socket->IsConnected())return;
    FString Text;const auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Object,Writer);Socket->Send(Text);
}
void ABasicFPSController::FlushInputs(const TArray<BasicFPS::Input>& Inputs) {
    TArray<TSharedPtr<FJsonValue>> Items;
    for(const auto& Input:Inputs) {
        auto Item=MakeShared<FJsonObject>();Item->SetNumberField(TEXT("seq"),Input.seq);Item->SetNumberField(TEXT("keys"),Input.keys);
        Item->SetNumberField(TEXT("yaw"),Input.yaw);Item->SetNumberField(TEXT("pitch"),Input.pitch);
        Item->SetNumberField(TEXT("epoch"),Input.epoch);Item->SetNumberField(TEXT("viewTime"),Input.viewTime);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }
    auto Packet=MakeShared<FJsonObject>();Packet->SetStringField(TEXT("type"),TEXT("inputs"));Packet->SetArrayField(TEXT("items"),Items);SendObject(Packet);
}
void ABasicFPSController::Join() {
    if(!bConnected||!bInitialized){Status=TEXT("연결이 완료된 뒤 입장해 주세요.");return;}
    if(bDisplayPending){Status=TEXT("먼저 화면 설정을 유지하거나 되돌려 주세요.");return;}
    Settings()->Nickname=Settings()->Nickname.TrimStartAndEnd().Left(16);if(Settings()->Nickname.IsEmpty())Settings()->Nickname=TEXT("Player");
    SavePreferences();auto Packet=MakeShared<FJsonObject>();Packet->SetStringField(TEXT("type"),TEXT("join"));Packet->SetStringField(TEXT("name"),Settings()->Nickname);SendObject(Packet);SetMenu(false);
}
void ABasicFPSController::Reload() {
    if(!IsPlaying())return;auto Packet=MakeShared<FJsonObject>();Packet->SetStringField(TEXT("type"),TEXT("reload"));SendObject(Packet);
}
void ABasicFPSController::SetBots(int32 Count) {
    auto Packet=MakeShared<FJsonObject>();Packet->SetStringField(TEXT("type"),TEXT("bots"));Packet->SetNumberField(TEXT("count"),FMath::Clamp(Count,0,6));SendObject(Packet);
}
void ABasicFPSController::ReceiveLocal(const TSharedPtr<FJsonObject>& Player) {
    const int NewEpoch=static_cast<int>(Number(Player,TEXT("epoch")));
    const BasicFPS::State Authoritative{Number(Player,TEXT("x")),Number(Player,TEXT("z")),Number(Player,TEXT("vx")),Number(Player,TEXT("vz"))};
    Ammo=FMath::Clamp(static_cast<int>(Number(Player,TEXT("ammo"),12)),0,12);
    ServerAmmo=Ammo;ServerAck=static_cast<std::int64_t>(Number(Player,TEXT("ack")));
    ReloadEnd=FPlatformTime::Seconds()+FMath::Max(0.0,Number(Player,TEXT("reloadRemaining")))/1000;
    if(NewEpoch!=Epoch) {
        Epoch=NewEpoch;Pending.Reset();Predicted=Authoritative;OffsetX=0;OffsetZ=0;Accumulator=0;LastShot=-1000;bInitialized=true;
        Status=TEXT("연결됐어요. 입장할 수 있어요.");return;
    }
    const auto Ack=static_cast<std::int64_t>(Number(Player,TEXT("ack")));
    Pending.RemoveAll([Ack](const auto& Input){return Input.seq<=Ack;});
    const double OldX=Predicted.x,OldZ=Predicted.z;Predicted=Authoritative;
    for(const auto& Input:Pending)BasicFPS::Move(Predicted,Input);
    const double X=OldX-Predicted.x,Z=OldZ-Predicted.z;
    if(X*X+Z*Z<9){OffsetX+=X;OffsetZ+=Z;}else{OffsetX=0;OffsetZ=0;}
}
void ABasicFPSController::HandleMessage(const FString& Message) {
    TSharedPtr<FJsonObject> Packet;const auto Reader=TJsonReaderFactory<>::Create(Message);
    if(!FJsonSerializer::Deserialize(Reader,Packet)||!Packet.IsValid())return;
    const FString Type=String(Packet,TEXT("type"));const double Now=FPlatformTime::Seconds();
    if(Type==TEXT("welcome")){OwnId=String(Packet,TEXT("id"));return;}
    if(Type==TEXT("error")){Status=String(Packet,TEXT("message")).Left(256);return;}
    if(Type==TEXT("hit")){HitUntil=Now+.15;return;}
    if(Type==TEXT("kill")) {
        Feed.Insert(TPair<FString,double>(String(Packet,TEXT("killer"))+TEXT(" → ")+String(Packet,TEXT("victim")),Now+5),0);
        if(Feed.Num()>6)Feed.SetNum(6);return;
    }
    if(Type==TEXT("shot")) {
        const TArray<TSharedPtr<FJsonValue>> *From=nullptr,*To=nullptr;
        if(Packet->TryGetArrayField(TEXT("from"),From)&&Packet->TryGetArrayField(TEXT("to"),To)&&From->Num()==3&&To->Num()==3) {
            const FVector A=Position((*From)[0]->AsNumber(),(*From)[1]->AsNumber(),(*From)[2]->AsNumber());
            const FVector B=Position((*To)[0]->AsNumber(),(*To)[1]->AsNumber(),(*To)[2]->AsNumber());
            if(!A.ContainsNaN()&&!B.ContainsNaN()) {
                auto* Trace=GetWorld()->SpawnActor<ABasicFPSGeometry>();Trace->Color(FLinearColor(1,.75,.2));
                Trace->SetActorLocation((A+B)/2);Trace->SetActorRotation((B-A).Rotation());Trace->Box(FVector::ZeroVector,FVector((B-A).Size(),1,1));Trace->SetLifeSpan(.08f);
                bool Blood=false;Packet->TryGetBoolField(TEXT("blood"),Blood);
                bool Impact=false;Packet->TryGetBoolField(TEXT("impact"),Impact);
                if(Blood||Impact) {
                    auto* Effect=GetWorld()->SpawnActor<ABasicFPSGeometry>();Effect->Color(Blood?FLinearColor(.8,.02,.015):FLinearColor(1,.7,.25));
                    Effect->Box(B,FVector(7,7,7));Effect->SetLifeSpan(.22f);
                }
            }
        }return;
    }
    if(Type!=TEXT("state"))return;
    const double ServerTime=Number(Packet,TEXT("serverTime"),Now*1000);ServerOffsetMs=ServerTime-Now*1000;
    const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;if(!Packet->TryGetArrayField(TEXT("players"),Entries)||Entries->Num()>16)return;
    TSet<FString> Seen;
    for(const auto& Entry:*Entries) {
        if(Entry->Type!=EJson::Object)continue;const auto Player=Entry->AsObject();const FString Id=String(Player,TEXT("id"));if(Id.IsEmpty())continue;Seen.Add(Id);
        FBasicFPSRemote& Remote=Players.FindOrAdd(Id);
        Remote.Name=String(Player,TEXT("name")).Left(16);Remote.HP=Number(Player,TEXT("hp"));Remote.Kills=Number(Player,TEXT("kills"));Remote.Deaths=Number(Player,TEXT("deaths"));
        const int NewEpoch=Number(Player,TEXT("epoch"));
        if(NewEpoch!=Remote.Epoch)Remote.Samples.Reset();Remote.Epoch=NewEpoch;
        Remote.X=Number(Player,TEXT("x"));Remote.Z=Number(Player,TEXT("z"));
        Remote.Samples.Add({ServerTime,Remote.X,Remote.Z,Number(Player,TEXT("yaw")),Remote.Epoch});if(Remote.Samples.Num()>12)Remote.Samples.RemoveAt(0);
        if(Id==OwnId)ReceiveLocal(Player);else if(!Remote.Body.IsValid())BuildFigure(*this,Remote);
    }
    for(auto It=Players.CreateIterator();It;++It)if(!Seen.Contains(It.Key())) {
        if(It.Value().Body.IsValid())It.Value().Body->Destroy();if(It.Value().Edges.IsValid())It.Value().Edges->Destroy();It.RemoveCurrent();
    }
}
void ABasicFPSController::UpdateRemotes(double Now) {
    const double Target=Now*1000+ServerOffsetMs-100;
    for(auto& Pair:Players) {
        auto& P=Pair.Value;if(Pair.Key==OwnId||!P.Body.IsValid()||!P.Samples.Num())continue;
        auto A=P.Samples[0],B=A;
        for(const auto& Sample:P.Samples){if(Sample.Time<=Target)A=Sample;if(Sample.Time>=Target){B=Sample;break;}B=Sample;}
        const double Alpha=B.Time>A.Time?FMath::Clamp((Target-A.Time)/(B.Time-A.Time),0.0,1.0):0;
        const FVector Location=Position(FMath::Lerp(A.X,B.X,Alpha),0,FMath::Lerp(A.Z,B.Z,Alpha));
        P.Body->SetActorLocation(Location);if(P.Edges.IsValid())P.Edges->SetActorLocation(Location);
    }
}
