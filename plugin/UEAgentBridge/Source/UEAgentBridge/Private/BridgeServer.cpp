#include "BridgeServer.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Common/TcpSocketBuilder.h"

// Editor & Blueprint
#include "IPythonScriptPlugin.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Engine/Blueprint.h"
#include "Logging/TokenizedMessage.h"
#include "EditorAssetLibrary.h"
#include "Async/Async.h"

// Level & Actor
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "Selection.h"

// Blueprint
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

// Asset Registry
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"

// ===== Error Codes =====

namespace EUEAgentError
{
	constexpr int32 InvalidParams  = -1;  // Missing or invalid required parameters
	constexpr int32 NotFound       = -2;  // Asset / actor / resource not found
	constexpr int32 Internal       = -3;  // Unexpected internal error
	constexpr int32 NotAvailable   = -4;  // Required subsystem unavailable (GEditor, World, Python, etc.)
}

// ===== Helpers =====

static TSharedPtr<FJsonObject> MakeErrorResponse(const FString& Message, int32 Code = EUEAgentError::Internal)
{
	auto Err = MakeShared<FJsonObject>();
	auto ErrorObj = MakeShared<FJsonObject>();
	ErrorObj->SetNumberField(TEXT("code"), Code);
	ErrorObj->SetStringField(TEXT("message"), Message);
	Err->SetObjectField(TEXT("error"), ErrorObj);
	return Err;
}

static const TCHAR* JsonTypeToString(EJson Type)
{
	switch (Type)
	{
		case EJson::None: return TEXT("None");
		case EJson::Null: return TEXT("Null");
		case EJson::String: return TEXT("String");
		case EJson::Number: return TEXT("Number");
		case EJson::Boolean: return TEXT("Boolean");
		case EJson::Array: return TEXT("Array");
		case EJson::Object: return TEXT("Object");
		default: return TEXT("Unknown");
	}
}

static TSharedPtr<FJsonObject> ActorToJson(AActor* Actor)
{
	auto Obj = MakeShared<FJsonObject>();
	if (!Actor) return Obj;

	Obj->SetStringField(TEXT("name"), Actor->GetName());
	Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

	FVector Loc = Actor->GetActorLocation();
	auto JLoc = MakeShared<FJsonObject>();
	JLoc->SetNumberField(TEXT("x"), Loc.X);
	JLoc->SetNumberField(TEXT("y"), Loc.Y);
	JLoc->SetNumberField(TEXT("z"), Loc.Z);
	Obj->SetObjectField(TEXT("location"), JLoc);

	FRotator Rot = Actor->GetActorRotation();
	auto JRot = MakeShared<FJsonObject>();
	JRot->SetNumberField(TEXT("pitch"), Rot.Pitch);
	JRot->SetNumberField(TEXT("yaw"), Rot.Yaw);
	JRot->SetNumberField(TEXT("roll"), Rot.Roll);
	Obj->SetObjectField(TEXT("rotation"), JRot);

	FVector Scale = Actor->GetActorScale3D();
	auto JScale = MakeShared<FJsonObject>();
	JScale->SetNumberField(TEXT("x"), Scale.X);
	JScale->SetNumberField(TEXT("y"), Scale.Y);
	JScale->SetNumberField(TEXT("z"), Scale.Z);
	Obj->SetObjectField(TEXT("scale"), JScale);

	// Static mesh info
	if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(Actor))
	{
		if (UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent())
		{
			if (UStaticMesh* Mesh = SMC->GetStaticMesh())
			{
				Obj->SetStringField(TEXT("static_mesh"), Mesh->GetPathName());
				Obj->SetNumberField(TEXT("material_count"), SMC->GetNumMaterials());
			}
		}
	}

	return Obj;
}

static UBlueprint* LoadBlueprintAsset(const FString& AssetPath)
{
	UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
	return Cast<UBlueprint>(Loaded);
}

// Suggest similar blueprint paths for NotFound error enrichment
static FString SuggestBlueprintPaths(const FString& PartialName, int32 MaxSuggestions = 5)
{
	if (PartialName.Len() < 3) return TEXT("");

	FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = Module.Get();

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(FName("/Game"));
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));

	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	TArray<FString> Suggestions;
	for (const FAssetData& Asset : Assets)
	{
		FString Name = Asset.AssetName.ToString();
		FString Path = Asset.GetObjectPathString();
		if (Name.Contains(PartialName) || Path.Contains(PartialName))
		{
			Suggestions.Add(Path);
			if (Suggestions.Num() >= MaxSuggestions) break;
		}
	}

	if (Suggestions.Num() == 0) return TEXT("");

	FString Hint = TEXT(" Did you mean: ");
	for (int32 i = 0; i < Suggestions.Num(); i++)
	{
		if (i > 0) Hint += TEXT(", ");
		Hint += Suggestions[i];
	}
	return Hint;
}

// Suggest similar actor names for NotFound error enrichment
static FString SuggestActorNames(UWorld* World, const FString& PartialName, int32 MaxSuggestions = 5)
{
	if (!World || PartialName.Len() < 3) return TEXT("");

	TArray<FString> Suggestions;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		FString Name = Actor->GetName();
		FString Label = Actor->GetActorLabel();
		if (Name.Contains(PartialName) || Label.Contains(PartialName))
		{
			Suggestions.Add(Label != Name ? FString::Printf(TEXT("%s (%s)"), *Label, *Name) : Name);
			if (Suggestions.Num() >= MaxSuggestions) break;
		}
	}

	if (Suggestions.Num() == 0) return TEXT("");

	FString Hint = TEXT(" Did you mean: ");
	for (int32 i = 0; i < Suggestions.Num(); i++)
	{
		if (i > 0) Hint += TEXT(", ");
		Hint += Suggestions[i];
	}
	return Hint;
}

// Suggest available properties for a blueprint (for "Property not found" error)
static FString SuggestProperties(UBlueprint* Blueprint, const FString& PartialName, int32 MaxSuggestions = 10)
{
	if (!Blueprint || !Blueprint->GeneratedClass) return TEXT("");

	TArray<FString> Names;
	for (TFieldIterator<FProperty> It(Blueprint->GeneratedClass); It; ++It)
	{
		FString PropName = It->GetName();
		if (PartialName.IsEmpty() || PropName.Contains(PartialName))
		{
			Names.Add(PropName);
			if (Names.Num() >= MaxSuggestions) break;
		}
	}

	if (Names.Num() == 0) return TEXT("");

	FString Hint = TEXT(" Available properties: ");
	for (int32 i = 0; i < Names.Num(); i++)
	{
		if (i > 0) Hint += TEXT(", ");
		Hint += Names[i];
	}
	return Hint;
}

// Helper: get property value as string (UE5.6: ExportTextItem removed from FProperty)
static FString GetPropertyValueAsString(FProperty* Prop, UObject* Container)
{
	if (FStrProperty* P = CastField<FStrProperty>(Prop))
		return P->GetPropertyValue_InContainer(Container);
	if (FIntProperty* P = CastField<FIntProperty>(Prop))
		return FString::FromInt(P->GetPropertyValue_InContainer(Container));
	if (FFloatProperty* P = CastField<FFloatProperty>(Prop))
		return FString::SanitizeFloat(P->GetPropertyValue_InContainer(Container));
	if (FDoubleProperty* P = CastField<FDoubleProperty>(Prop))
		return FString::SanitizeFloat(P->GetPropertyValue_InContainer(Container));
	if (FBoolProperty* P = CastField<FBoolProperty>(Prop))
		return P->GetPropertyValue_InContainer(Container) ? TEXT("true") : TEXT("false");
	if (FByteProperty* P = CastField<FByteProperty>(Prop))
		return FString::FromInt(P->GetPropertyValue_InContainer(Container));
	if (FTextProperty* P = CastField<FTextProperty>(Prop))
		return P->GetPropertyValue_InContainer(Container).ToString();
	return TEXT("<unknown>");
}

static FString SerializeResponse(const TSharedPtr<FJsonObject>& Response)
{
	FString ResponseStr;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ResponseStr);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
	return ResponseStr;
}

// ===== Lifecycle =====

FBridgeServer& FBridgeServer::Get()
{
	static FBridgeServer Instance;
	return Instance;
}

FBridgeServer::~FBridgeServer()
{
	Shutdown();
}

void FBridgeServer::Start(int32 InPort)
{
	if (bIsRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UEAgentBridge] Server already running"));
		return;
	}

	Port = InPort;
	bStopRequested = false;

	RegisterHandlers();

	Thread = FRunnableThread::Create(this, TEXT("UEAgentBridge"), 0, TPri_Normal);
	if (!Thread)
	{
		UE_LOG(LogTemp, Error, TEXT("[UEAgentBridge] Failed to create thread"));
	}
}

void FBridgeServer::Stop()
{
	bStopRequested = true;
}

void FBridgeServer::Shutdown()
{
	bStopRequested = true;

	if (ServerSocket)
	{
		ServerSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ServerSocket);
		ServerSocket = nullptr;
	}

	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	bIsRunning = false;
	UE_LOG(LogTemp, Log, TEXT("[UEAgentBridge] Server stopped"));
}

bool FBridgeServer::Init()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[UEAgentBridge] No socket subsystem"));
		return false;
	}

	FIPv4Endpoint Endpoint(FIPv4Address::InternalLoopback, Port);

	ServerSocket = FTcpSocketBuilder(TEXT("UEAgentBridge"))
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.Listening(8)
		.WithReceiveBufferSize(65536)
		.WithSendBufferSize(65536);

	if (!ServerSocket)
	{
		UE_LOG(LogTemp, Error, TEXT("[UEAgentBridge] Failed to create socket on port %d"), Port);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[UEAgentBridge] Listening on tcp://127.0.0.1:%d"), Port);
	return true;
}

uint32 FBridgeServer::Run()
{
	if (!ServerSocket) return 1;

	bIsRunning = true;
	UE_LOG(LogTemp, Log, TEXT("[UEAgentBridge] Accept thread started"));

	while (!bStopRequested)
	{
		bool bHasPending = false;
		if (!ServerSocket->WaitForPendingConnection(bHasPending, FTimespan::FromMilliseconds(100)))
		{
			continue;
		}

		if (!bHasPending) continue;

		FSocket* ClientSocket = ServerSocket->Accept(TEXT("UEAgentBridge-Client"));
		if (ClientSocket)
		{
			UE_LOG(LogTemp, Log, TEXT("[UEAgentBridge] Client connected"));
			HandleClient(ClientSocket);
			ClientSocket->Close();
			ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
			UE_LOG(LogTemp, Log, TEXT("[UEAgentBridge] Client disconnected"));
		}
	}

	return 0;
}

void FBridgeServer::Exit()
{
	UE_LOG(LogTemp, Log, TEXT("[UEAgentBridge] Accept thread exited"));
}

// ===== Handler Registry =====

void FBridgeServer::RegisterHandlers()
{
	HandlerRegistry.Empty();

	// Core
	HandlerRegistry.Add(TEXT("ping"), [this](const TSharedPtr<FJsonObject>& P) { return HandlePing(P); });

	// Editor
	HandlerRegistry.Add(TEXT("execute_python"), [this](const TSharedPtr<FJsonObject>& P) { return HandleExecutePython(P); });
	HandlerRegistry.Add(TEXT("execute_command"), [this](const TSharedPtr<FJsonObject>& P) { return HandleExecuteCommand(P); });
	HandlerRegistry.Add(TEXT("get_selected"), [this](const TSharedPtr<FJsonObject>& P) { return HandleGetSelected(P); });

	// Level
	HandlerRegistry.Add(TEXT("get_actors"), [this](const TSharedPtr<FJsonObject>& P) { return HandleGetActors(P); });
	HandlerRegistry.Add(TEXT("spawn_actor"), [this](const TSharedPtr<FJsonObject>& P) { return HandleSpawnActor(P); });
	HandlerRegistry.Add(TEXT("modify_actor"), [this](const TSharedPtr<FJsonObject>& P) { return HandleModifyActor(P); });
	HandlerRegistry.Add(TEXT("delete_actor"), [this](const TSharedPtr<FJsonObject>& P) { return HandleDeleteActor(P); });

	// Asset
	HandlerRegistry.Add(TEXT("search_assets"), [this](const TSharedPtr<FJsonObject>& P) { return HandleSearchAssets(P); });

	// Blueprint
	HandlerRegistry.Add(TEXT("compile_blueprint"), [this](const TSharedPtr<FJsonObject>& P) { return HandleCompileBlueprint(P); });
	HandlerRegistry.Add(TEXT("compile_blueprints"), [this](const TSharedPtr<FJsonObject>& P) { return HandleCompileBlueprints(P); });
	HandlerRegistry.Add(TEXT("read_blueprint"), [this](const TSharedPtr<FJsonObject>& P) { return HandleReadBlueprint(P); });
	HandlerRegistry.Add(TEXT("set_blueprint_property"), [this](const TSharedPtr<FJsonObject>& P) { return HandleSetBlueprintProperty(P); });

	UE_LOG(LogTemp, Log, TEXT("[UEAgentBridge] Registered %d handlers"), HandlerRegistry.Num());
}

// ===== TCP I/O =====

void FBridgeServer::HandleClient(FSocket* ClientSocket)
{
	// Network thread ONLY handles socket I/O and line parsing.
	// All engine API calls are dispatched to the game thread via AsyncTask.

	FString PartialLine;
	uint8 RecvBuffer[4096];
	int32 ConsecutiveErrors = 0;
	const int32 MaxConsecutiveErrors = 50;
	bool bClientError = false;

	while (!bStopRequested)
	{
		int32 BytesRead = 0;
		if (ClientSocket->Recv(RecvBuffer, sizeof(RecvBuffer), BytesRead, ESocketReceiveFlags::None))
		{
			ConsecutiveErrors = 0;

			if (BytesRead > 0)
			{
				FUTF8ToTCHAR Converter((const ANSICHAR*)RecvBuffer, BytesRead);
				PartialLine.AppendChars(Converter.Get(), Converter.Length());

				int32 NewlineIdx;
				while ((NewlineIdx = PartialLine.Find(TEXT("\n"))) != INDEX_NONE)
				{
					FString Line = PartialLine.Left(NewlineIdx).TrimStartAndEnd();
					PartialLine.RightChopInline(NewlineIdx + 1);

					if (Line.IsEmpty()) continue;

					// === DISPATCH TO GAME THREAD ===
					TPromise<FString> ResponsePromise;
					TFuture<FString> ResponseFuture = ResponsePromise.GetFuture();

					AsyncTask(ENamedThreads::GameThread, [this, Line, &ResponsePromise]()
					{
						UE_LOG(LogTemp, Log, TEXT("[UEAgentBridge] Received: %s"), *Line);
						FString Response = ProcessMessage(Line);
						ResponsePromise.SetValue(Response);
					});

					FString Response = ResponseFuture.Get() + TEXT("\n");

					// === SEND ON NETWORK THREAD ===
					FTCHARToUTF8 ResponseUtf8(*Response);
					int32 TotalSent = 0;
					int32 ToSend = ResponseUtf8.Length();
					const uint8* Data = reinterpret_cast<const uint8*>(ResponseUtf8.Get());

					while (TotalSent < ToSend)
					{
						int32 Sent = 0;
						if (!ClientSocket->Send(Data + TotalSent, ToSend - TotalSent, Sent))
						{
							bClientError = true;
							break;
						}
						TotalSent += Sent;
					}

					if (bClientError) break;
				}
			}

			if (bClientError) break;
		}
		else
		{
			ESocketErrors LastError = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();

			if (LastError == SE_EWOULDBLOCK || LastError == SE_ETIMEDOUT)
			{
				FPlatformProcess::Sleep(0.001f);
				continue;
			}

			ConsecutiveErrors++;
			if (ConsecutiveErrors <= MaxConsecutiveErrors)
			{
				FPlatformProcess::Sleep(0.01f);
				continue;
			}

			break;
		}

		FPlatformProcess::Sleep(0.001f);
	}
}

FString FBridgeServer::ProcessMessage(const FString& RawMessage)
{
	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawMessage);

	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		auto ErrObj = MakeErrorResponse(TEXT("Invalid JSON"), EUEAgentError::InvalidParams);
		FString ErrStr;
		TSharedRef<TJsonWriter<>> ErrWriter = TJsonWriterFactory<>::Create(&ErrStr);
		FJsonSerializer::Serialize(ErrObj.ToSharedRef(), ErrWriter);
		return ErrStr;
	}

	int32 Id = 0;
	JsonObj->TryGetNumberField(TEXT("id"), Id);

	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetNumberField(TEXT("id"), Id);

	try
	{
		TSharedPtr<FJsonObject> Result = RouteRequest(JsonObj);

		const TSharedPtr<FJsonObject>* ErrorObj;
		if (Result->TryGetObjectField(TEXT("error"), ErrorObj))
		{
			Response->SetObjectField(TEXT("error"), *ErrorObj);
		}
		else
		{
			Response->SetObjectField(TEXT("result"), Result);
		}
	}
	catch (...)
	{
		auto ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetNumberField(TEXT("code"), EUEAgentError::Internal);
		ErrorObj->SetStringField(TEXT("message"), TEXT("Internal server error"));
		Response->SetObjectField(TEXT("error"), ErrorObj);
	}

	return SerializeResponse(Response);
}

TSharedPtr<FJsonObject> FBridgeServer::RouteRequest(const TSharedPtr<FJsonObject>& Request)
{
	FString Method;
	if (!Request->TryGetStringField(TEXT("method"), Method))
	{
		return MakeErrorResponse(TEXT("Missing 'method' field"), EUEAgentError::InvalidParams);
	}

	const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
	Request->TryGetObjectField(TEXT("params"), ParamsPtr);
	TSharedPtr<FJsonObject> Params = ParamsPtr ? *ParamsPtr : MakeShared<FJsonObject>();

	FRequestHandler* Handler = HandlerRegistry.Find(Method);
	if (Handler)
	{
		return (*Handler)(Params);
	}

	return MakeErrorResponse(FString::Printf(TEXT("Unknown method: %s"), *Method), EUEAgentError::NotFound);
}

// ===== Core =====

TSharedPtr<FJsonObject> FBridgeServer::HandlePing(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("pong"));
	return Result;
}

// ===== Editor =====

TSharedPtr<FJsonObject> FBridgeServer::HandleExecutePython(const TSharedPtr<FJsonObject>& Params)
{
	FString Code;
	if (!Params->TryGetStringField(TEXT("code"), Code) || Code.IsEmpty())
	{
		return MakeErrorResponse(TEXT("Missing required field: code"), EUEAgentError::InvalidParams);
	}

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
	{
		return MakeErrorResponse(TEXT("Python scripting is not available"), EUEAgentError::NotAvailable);
	}

	FPythonCommandEx PythonCommand;
	PythonCommand.Command = Code;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;

	bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("command_result"), PythonCommand.CommandResult);

	TArray<TSharedPtr<FJsonValue>> LogArray;
	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		auto LogEntry = MakeShared<FJsonObject>();
		LogEntry->SetStringField(TEXT("type"), LexToString(Entry.Type));
		LogEntry->SetStringField(TEXT("output"), Entry.Output);
		LogArray.Add(MakeShared<FJsonValueObject>(LogEntry));
	}
	Result->SetArrayField(TEXT("log_output"), LogArray);

	return Result;
}

TSharedPtr<FJsonObject> FBridgeServer::HandleExecuteCommand(const TSharedPtr<FJsonObject>& Params)
{
	FString Command;
	if (!Params->TryGetStringField(TEXT("command"), Command) || Command.IsEmpty())
	{
		return MakeErrorResponse(TEXT("Missing required field: command"), EUEAgentError::InvalidParams);
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
	if (!World)
	{
		return MakeErrorResponse(TEXT("No editor world available"), EUEAgentError::NotAvailable);
	}

	UKismetSystemLibrary::ExecuteConsoleCommand(World, Command, nullptr);

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("command"), Command);
	return Result;
}

TSharedPtr<FJsonObject> FBridgeServer::HandleGetSelected(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MakeErrorResponse(TEXT("GEditor is not available"), EUEAgentError::NotAvailable);
	}

	TArray<TSharedPtr<FJsonValue>> ActorArray;

	USelection* Selection = GEditor->GetSelectedActors();
	if (!Selection)
	{
		auto Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("actors"), ActorArray);
		Result->SetNumberField(TEXT("count"), 0);
		return Result;
	}

	for (int32 i = 0; i < Selection->Num(); ++i)
	{
		AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i));
		if (Actor)
		{
			ActorArray.Add(MakeShared<FJsonValueObject>(ActorToJson(Actor)));
		}
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("actors"), ActorArray);
	Result->SetNumberField(TEXT("count"), ActorArray.Num());
	return Result;
}

// ===== Level =====

TSharedPtr<FJsonObject> FBridgeServer::HandleGetActors(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
	if (!World)
	{
		return MakeErrorResponse(TEXT("No editor world available"), EUEAgentError::NotAvailable);
	}

	FString ClassFilter;
	Params->TryGetStringField(TEXT("class"), ClassFilter);

	int32 MaxResults = 100;
	Params->TryGetNumberField(TEXT("max_results"), MaxResults);
	if (MaxResults <= 0) MaxResults = 100;

	TArray<TSharedPtr<FJsonValue>> ActorArray;
	int32 TotalCount = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		TotalCount++;

		if (!ClassFilter.IsEmpty())
		{
			if (!Actor->GetClass()->GetName().Contains(ClassFilter) &&
				!Actor->GetClass()->GetPathName().Contains(ClassFilter))
			{
				continue;
			}
		}

		if (ActorArray.Num() >= MaxResults)
		{
			continue; // Keep counting but stop adding
		}

		ActorArray.Add(MakeShared<FJsonValueObject>(ActorToJson(Actor)));
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("actors"), ActorArray);
	Result->SetNumberField(TEXT("total"), TotalCount);
	Result->SetNumberField(TEXT("returned"), ActorArray.Num());
	return Result;
}

TSharedPtr<FJsonObject> FBridgeServer::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (!Params->TryGetStringField(TEXT("class"), ClassName) || ClassName.IsEmpty())
	{
		return MakeErrorResponse(TEXT("Missing required field: class"), EUEAgentError::InvalidParams);
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
	if (!World)
	{
		return MakeErrorResponse(TEXT("No editor world available"), EUEAgentError::NotAvailable);
	}

	// Resolve class
	UClass* ActorClass = nullptr;

	// Try with full path first (e.g. /Script/Engine.StaticMeshActor)
	if (ClassName.StartsWith(TEXT("/Script/")))
	{
		ActorClass = LoadClass<AActor>(nullptr, *ClassName);
	}
	else
	{
		// Try as short name
		ActorClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None, ELogVerbosity::Fatal);
		if (!ActorClass)
		{
			ActorClass = LoadClass<AActor>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
		}
	}

	if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
	{
		return MakeErrorResponse(FString::Printf(TEXT("Actor class not found: %s"), *ClassName), EUEAgentError::NotFound);
	}

	// Get optional params
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector Scale = FVector(1.0f, 1.0f, 1.0f);
	FString Label;

	const TSharedPtr<FJsonObject>* LocObj;
	if (Params->TryGetObjectField(TEXT("location"), LocObj))
	{
		(*LocObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Location.Z);
	}

	const TSharedPtr<FJsonObject>* RotObj;
	if (Params->TryGetObjectField(TEXT("rotation"), RotObj))
	{
		(*RotObj)->TryGetNumberField(TEXT("pitch"), Rotation.Pitch);
		(*RotObj)->TryGetNumberField(TEXT("yaw"), Rotation.Yaw);
		(*RotObj)->TryGetNumberField(TEXT("roll"), Rotation.Roll);
	}

	const TSharedPtr<FJsonObject>* ScaleObj;
	if (Params->TryGetObjectField(TEXT("scale"), ScaleObj))
	{
		(*ScaleObj)->TryGetNumberField(TEXT("x"), Scale.X);
		(*ScaleObj)->TryGetNumberField(TEXT("y"), Scale.Y);
		(*ScaleObj)->TryGetNumberField(TEXT("z"), Scale.Z);
	}

	Params->TryGetStringField(TEXT("label"), Label);

	// Spawn
	FActorSpawnParameters SpawnParams;
	if (!Label.IsEmpty())
	{
		SpawnParams.Name = *Label;
		SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	}

	AActor* SpawnedActor = World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
	if (!SpawnedActor)
	{
		return MakeErrorResponse(TEXT("Failed to spawn actor"), EUEAgentError::Internal);
	}

	SpawnedActor->SetActorScale3D(Scale);

	// Static mesh support
	FString StaticMeshPath;
	if (Params->TryGetStringField(TEXT("static_mesh"), StaticMeshPath) && !StaticMeshPath.IsEmpty())
	{
		if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(SpawnedActor))
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(StaticMeshPath));
			if (Mesh)
			{
				SMA->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			}
		}
	}

	// Material support
	FString MaterialPath;
	if (Params->TryGetStringField(TEXT("material"), MaterialPath) && !MaterialPath.IsEmpty())
	{
		if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(SpawnedActor))
		{
			UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
			if (Material)
			{
				// Apply to all material slots
				int32 NumMaterials = SMA->GetStaticMeshComponent()->GetNumMaterials();
				for (int32 i = 0; i < NumMaterials; ++i)
				{
					SMA->GetStaticMeshComponent()->SetMaterial(i, Material);
				}
			}
		}
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("actor"), ActorToJson(SpawnedActor));
	return Result;
}

// ===== Asset =====

TSharedPtr<FJsonObject> FBridgeServer::HandleSearchAssets(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	Params->TryGetStringField(TEXT("query"), Query);

	FString ClassFilter;
	Params->TryGetStringField(TEXT("class"), ClassFilter);

	FString Directory = TEXT("/Game");
	Params->TryGetStringField(TEXT("directory"), Directory);

	int32 MaxResults = 50;
	Params->TryGetNumberField(TEXT("max_results"), MaxResults);
	if (MaxResults <= 0) MaxResults = 50;

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Build filter
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(*Directory);

	if (!ClassFilter.IsEmpty())
	{
		Filter.ClassPaths.Add(FTopLevelAssetPath(*ClassFilter));
	}

	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Matched = 0;

	for (const FAssetData& Asset : AssetList)
	{
		FString AssetName = Asset.AssetName.ToString();
		FString AssetPath = Asset.GetObjectPathString();

		// Name filtering
		if (!Query.IsEmpty())
		{
			if (!AssetName.Contains(Query) && !AssetPath.Contains(Query))
			{
				continue;
			}
		}

		if (Matched >= MaxResults) break;

		auto Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), AssetName);
		Item->SetStringField(TEXT("path"), AssetPath);
		Item->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());

		Results.Add(MakeShared<FJsonValueObject>(Item));
		Matched++;
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("assets"), Results);
	Result->SetNumberField(TEXT("total_in_filter"), AssetList.Num());
	Result->SetNumberField(TEXT("returned"), Results.Num());
	return Result;
}

// ===== Blueprint =====

TSharedPtr<FJsonObject> FBridgeServer::HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
	{
		return MakeErrorResponse(TEXT("Missing required field: asset_path"), EUEAgentError::InvalidParams);
	}

	UBlueprint* Blueprint = LoadBlueprintAsset(AssetPath);
	if (!Blueprint)
	{
		return MakeErrorResponse(FString::Printf(TEXT("Blueprint not found: %s%s"), *AssetPath, *SuggestBlueprintPaths(AssetPath)), EUEAgentError::NotFound);
	}

	FCompilerResultsLog CompileLog;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompileLog);

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetBoolField(TEXT("success"), CompileLog.NumErrors == 0);
	Result->SetNumberField(TEXT("errors"), CompileLog.NumErrors);
	Result->SetNumberField(TEXT("warnings"), CompileLog.NumWarnings);

	TArray<TSharedPtr<FJsonValue>> ErrorArray;
	TArray<TSharedPtr<FJsonValue>> WarningArray;
	for (const TSharedRef<FTokenizedMessage>& Msg : CompileLog.Messages)
	{
		if (Msg->GetSeverity() == EMessageSeverity::Error)
		{
			auto ErrMsg = MakeShared<FJsonObject>();
			ErrMsg->SetStringField(TEXT("message"), Msg->ToText().ToString());
			ErrorArray.Add(MakeShared<FJsonValueObject>(ErrMsg));
		}
		else if (Msg->GetSeverity() == EMessageSeverity::Warning)
		{
			auto WarnMsg = MakeShared<FJsonObject>();
			WarnMsg->SetStringField(TEXT("message"), Msg->ToText().ToString());
			WarningArray.Add(MakeShared<FJsonValueObject>(WarnMsg));
		}
	}
	Result->SetArrayField(TEXT("error_list"), ErrorArray);
	Result->SetArrayField(TEXT("warning_list"), WarningArray);

	return Result;
}

TSharedPtr<FJsonObject> FBridgeServer::HandleCompileBlueprints(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathsArray;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), PathsArray) || PathsArray->Num() == 0)
	{
		return MakeErrorResponse(TEXT("Missing required field: asset_paths (non-empty array)"), EUEAgentError::InvalidParams);
	}

	bool bSave = true;
	Params->TryGetBoolField(TEXT("save"), bSave);

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 SuccessCount = 0;
	int32 FailCount = 0;

	for (const TSharedPtr<FJsonValue>& PathVal : *PathsArray)
	{
		FString AssetPath = PathVal->AsString();
		UBlueprint* Blueprint = LoadBlueprintAsset(AssetPath);
		auto ItemResult = MakeShared<FJsonObject>();
		ItemResult->SetStringField(TEXT("asset_path"), AssetPath);

		if (!Blueprint)
		{
			ItemResult->SetStringField(TEXT("status"), TEXT("not_found"));
			ItemResult->SetBoolField(TEXT("success"), false);
			Results.Add(MakeShared<FJsonValueObject>(ItemResult));
			FailCount++;
			continue;
		}

		FCompilerResultsLog CompileLog;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &CompileLog);

		bool bCompileSuccess = (CompileLog.NumErrors == 0);
		ItemResult->SetBoolField(TEXT("success"), bCompileSuccess);
		ItemResult->SetNumberField(TEXT("errors"), CompileLog.NumErrors);
		ItemResult->SetNumberField(TEXT("warnings"), CompileLog.NumWarnings);

		if (bCompileSuccess)
		{
			ItemResult->SetStringField(TEXT("status"), TEXT("compiled"));
			if (bSave) UEditorAssetLibrary::SaveAsset(AssetPath);
			SuccessCount++;
		}
		else
		{
			ItemResult->SetStringField(TEXT("status"), TEXT("failed"));
			FailCount++;
		}

		Results.Add(MakeShared<FJsonValueObject>(ItemResult));
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("all_success"), FailCount == 0);
	Result->SetNumberField(TEXT("total"), PathsArray->Num());
	Result->SetNumberField(TEXT("compiled"), SuccessCount);
	Result->SetNumberField(TEXT("failed"), FailCount);
	Result->SetArrayField(TEXT("results"), Results);

	return Result;
}

TSharedPtr<FJsonObject> FBridgeServer::HandleReadBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
	{
		return MakeErrorResponse(TEXT("Missing required field: asset_path"), EUEAgentError::InvalidParams);
	}

	UBlueprint* Blueprint = LoadBlueprintAsset(AssetPath);
	if (!Blueprint)
	{
		return MakeErrorResponse(FString::Printf(TEXT("Blueprint not found: %s%s"), *AssetPath, *SuggestBlueprintPaths(AssetPath)), EUEAgentError::NotFound);
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("name"), Blueprint->GetName());
	Result->SetStringField(TEXT("class"), Blueprint->GetClass()->GetName());

	// Parent class
	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetPathName());
	}

	// Blueprint type
	Result->SetStringField(TEXT("blueprint_type"), StaticEnum<EBlueprintType>()->GetNameStringByValue((int64)Blueprint->BlueprintType));

	// Variables
	TArray<TSharedPtr<FJsonValue>> VarArray;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		auto VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		if (!Var.VarType.PinSubCategory.IsNone())
		{
			VarObj->SetStringField(TEXT("sub_type"), Var.VarType.PinSubCategory.ToString());
		}
		VarObj->SetBoolField(TEXT("editable"), (Var.PropertyFlags & CPF_Edit) != 0);
		VarObj->SetBoolField(TEXT("blueprint_readonly"), (Var.PropertyFlags & CPF_BlueprintReadOnly) != 0);
		if (!Var.Category.IsEmpty())
		{
			VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
		}
		VarArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Result->SetArrayField(TEXT("variables"), VarArray);
	Result->SetNumberField(TEXT("variable_count"), VarArray.Num());

	// Functions
	TArray<TSharedPtr<FJsonValue>> FuncArray;
	for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;
		auto FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Graph->GetName());
		FuncArray.Add(MakeShared<FJsonValueObject>(FuncObj));
	}
	Result->SetArrayField(TEXT("functions"), FuncArray);
	Result->SetNumberField(TEXT("function_count"), FuncArray.Num());

	// Components (SCS)
	TArray<TSharedPtr<FJsonValue>> CompArray;
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate) continue;
			auto CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			CompObj->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
			if (Node->ParentComponentOrVariableName != NAME_None)
			{
				CompObj->SetStringField(TEXT("parent"), Node->ParentComponentOrVariableName.ToString());
			}
			CompArray.Add(MakeShared<FJsonValueObject>(CompObj));
		}
	}
	Result->SetArrayField(TEXT("components"), CompArray);
	Result->SetNumberField(TEXT("component_count"), CompArray.Num());

	return Result;
}

TSharedPtr<FJsonObject> FBridgeServer::HandleSetBlueprintProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, PropertyName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
		return MakeErrorResponse(TEXT("Missing required field: asset_path"), EUEAgentError::InvalidParams);
	if (!Params->TryGetStringField(TEXT("property"), PropertyName) || PropertyName.IsEmpty())
		return MakeErrorResponse(TEXT("Missing required field: property"), EUEAgentError::InvalidParams);

	UBlueprint* Blueprint = LoadBlueprintAsset(AssetPath);
	if (!Blueprint)
		return MakeErrorResponse(FString::Printf(TEXT("Blueprint not found: %s%s"), *AssetPath, *SuggestBlueprintPaths(AssetPath)), EUEAgentError::NotFound);
	if (!Blueprint->GeneratedClass)
		return MakeErrorResponse(TEXT("Blueprint has no generated class"), EUEAgentError::Internal);

	UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject(false);
	if (!CDO)
		return MakeErrorResponse(TEXT("Failed to get Class Default Object"), EUEAgentError::Internal);

	FProperty* Prop = Blueprint->GeneratedClass->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
		return MakeErrorResponse(FString::Printf(TEXT("Property not found: %s%s"), *PropertyName, *SuggestProperties(Blueprint, PropertyName)), EUEAgentError::NotFound);

	// Read old value for response
	FString OldValue = GetPropertyValueAsString(Prop, CDO);

	// Get JSON value and set based on property type + JSON type
	TSharedPtr<FJsonValue> ValueField = Params->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
		return MakeErrorResponse(TEXT("Missing required field: value"), EUEAgentError::InvalidParams);

	bool bSet = false;

	if (ValueField->Type == EJson::String)
	{
		FString StrValue = ValueField->AsString();
		if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			StrProp->SetPropertyValue_InContainer(CDO, StrValue);
			bSet = true;
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			IntProp->SetPropertyValue_InContainer(CDO, FCString::Atoi(*StrValue));
			bSet = true;
		}
		else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			FloatProp->SetPropertyValue_InContainer(CDO, FCString::Atof(*StrValue));
			bSet = true;
		}
		else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			DoubleProp->SetPropertyValue_InContainer(CDO, FCString::Atod(*StrValue));
			bSet = true;
		}
		else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			BoolProp->SetPropertyValue_InContainer(CDO, StrValue.ToBool());
			bSet = true;
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			ByteProp->SetPropertyValue_InContainer(CDO, (uint8)FCString::Atoi(*StrValue));
			bSet = true;
		}
		else if (FTextProperty* TextProp = CastField<FTextProperty>(Prop))
		{
			TextProp->SetPropertyValue_InContainer(CDO, FText::FromString(StrValue));
			bSet = true;
		}
	}
	else if (ValueField->Type == EJson::Number)
	{
		double NumValue = ValueField->AsNumber();
		if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			IntProp->SetPropertyValue_InContainer(CDO, (int32)NumValue);
			bSet = true;
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			ByteProp->SetPropertyValue_InContainer(CDO, (uint8)NumValue);
			bSet = true;
		}
		else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			FloatProp->SetPropertyValue_InContainer(CDO, (float)NumValue);
			bSet = true;
		}
		else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			DoubleProp->SetPropertyValue_InContainer(CDO, NumValue);
			bSet = true;
		}
	}
	else if (ValueField->Type == EJson::Boolean)
	{
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			BoolProp->SetPropertyValue_InContainer(CDO, ValueField->AsBool());
			bSet = true;
		}
	}

	if (!bSet)
	{
		FString ErrMsg = FString(TEXT("Type mismatch: cannot assign ")) + JsonTypeToString(ValueField->Type) + TEXT(" to property '") + PropertyName + TEXT("'");
		return MakeErrorResponse(ErrMsg, EUEAgentError::InvalidParams);
	}

	// Read new value
	FString NewValue = GetPropertyValueAsString(Prop, CDO);

	// Mark dirty
	Blueprint->MarkPackageDirty();

	// Optional save
	bool bSave = false;
	Params->TryGetBoolField(TEXT("save"), bSave);
	if (bSave) UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetStringField(TEXT("old_value"), OldValue);
	Result->SetStringField(TEXT("new_value"), NewValue);
	Result->SetBoolField(TEXT("saved"), bSave);
	return Result;
}

// Helper: find actor in world by name or label
static AActor* FindActorByName(UWorld* World, const FString& Name)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetName() == Name || (*It)->GetActorLabel() == Name)
			return *It;
	}
	return nullptr;
}

TSharedPtr<FJsonObject> FBridgeServer::HandleModifyActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorName;
	if (!Params->TryGetStringField(TEXT("name"), ActorName) || ActorName.IsEmpty())
		return MakeErrorResponse(TEXT("Missing required field: name"), EUEAgentError::InvalidParams);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
	if (!World)
		return MakeErrorResponse(TEXT("No editor world available"), EUEAgentError::NotAvailable);

	AActor* FoundActor = FindActorByName(World, ActorName);
	if (!FoundActor)
		return MakeErrorResponse(FString::Printf(TEXT("Actor not found: %s%s"), *ActorName, *SuggestActorNames(World, ActorName)), EUEAgentError::NotFound);

	// Location
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (Params->TryGetObjectField(TEXT("location"), Obj))
		{
			FVector Loc = FoundActor->GetActorLocation();
			(*Obj)->TryGetNumberField(TEXT("x"), Loc.X);
			(*Obj)->TryGetNumberField(TEXT("y"), Loc.Y);
			(*Obj)->TryGetNumberField(TEXT("z"), Loc.Z);
			FoundActor->SetActorLocation(Loc);
		}
	}

	// Rotation
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (Params->TryGetObjectField(TEXT("rotation"), Obj))
		{
			FRotator Rot = FoundActor->GetActorRotation();
			(*Obj)->TryGetNumberField(TEXT("pitch"), Rot.Pitch);
			(*Obj)->TryGetNumberField(TEXT("yaw"), Rot.Yaw);
			(*Obj)->TryGetNumberField(TEXT("roll"), Rot.Roll);
			FoundActor->SetActorRotation(Rot);
		}
	}

	// Scale
	{
		const TSharedPtr<FJsonObject>* Obj;
		if (Params->TryGetObjectField(TEXT("scale"), Obj))
		{
			FVector Scale = FoundActor->GetActorScale3D();
			(*Obj)->TryGetNumberField(TEXT("x"), Scale.X);
			(*Obj)->TryGetNumberField(TEXT("y"), Scale.Y);
			(*Obj)->TryGetNumberField(TEXT("z"), Scale.Z);
			FoundActor->SetActorScale3D(Scale);
		}
	}

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("actor"), ActorToJson(FoundActor));
	return Result;
}

TSharedPtr<FJsonObject> FBridgeServer::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorName;
	if (!Params->TryGetStringField(TEXT("name"), ActorName) || ActorName.IsEmpty())
		return MakeErrorResponse(TEXT("Missing required field: name"), EUEAgentError::InvalidParams);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
	if (!World)
		return MakeErrorResponse(TEXT("No editor world available"), EUEAgentError::NotAvailable);

	AActor* FoundActor = FindActorByName(World, ActorName);
	if (!FoundActor)
		return MakeErrorResponse(FString::Printf(TEXT("Actor not found: %s%s"), *ActorName, *SuggestActorNames(World, ActorName)), EUEAgentError::NotFound);

	FString DeletedName = FoundActor->GetName();
	FoundActor->Destroy();

	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), DeletedName);
	return Result;
}
