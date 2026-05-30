#pragma once

#include "CoreMinimal.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

/**
 * Handler function signature: takes params JSON, returns result JSON (or error with "error" field).
 */
using FRequestHandler = TFunction<TSharedPtr<FJsonObject>(const TSharedPtr<FJsonObject>&)>;

/**
 * TCP JSON-RPC bridge server.
 * Listens on a local port and processes JSON-RPC requests from the TS MCP server.
 *
 * Adding a new tool:
 *   1. Declare HandleXxx() in this header
 *   2. Implement it in BridgeServer.cpp (called on game thread — no threading concerns)
 *   3. Register it in RegisterHandlers() with the method name
 *   4. Add the corresponding TypeScript tool in server/src/tools/
 */
class FBridgeServer : public FRunnable
{
public:
	static FBridgeServer& Get();

	void Start(int32 Port = 9877);
	void Shutdown();
	bool IsRunning() const { return bIsRunning; }

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

private:
	FBridgeServer() = default;
	~FBridgeServer();

	void HandleClient(FSocket* ClientSocket);
	FString ProcessMessage(const FString& RawMessage);
	TSharedPtr<FJsonObject> RouteRequest(const TSharedPtr<FJsonObject>& Request);
	void RegisterHandlers();

	// ===== Built-in handlers =====

	// Core
	TSharedPtr<FJsonObject> HandlePing(const TSharedPtr<FJsonObject>& Params);

	// Editor
	TSharedPtr<FJsonObject> HandleExecutePython(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleExecuteCommand(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetSelected(const TSharedPtr<FJsonObject>& Params);

	// Level
	TSharedPtr<FJsonObject> HandleGetActors(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSpawnActor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleModifyActor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteActor(const TSharedPtr<FJsonObject>& Params);

	// Asset
	TSharedPtr<FJsonObject> HandleSearchAssets(const TSharedPtr<FJsonObject>& Params);

	// Blueprint
	TSharedPtr<FJsonObject> HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCompileBlueprints(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleReadBlueprint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetBlueprintProperty(const TSharedPtr<FJsonObject>& Params);

	// ===== State =====
	FSocket* ServerSocket = nullptr;
	FRunnableThread* Thread = nullptr;
	FThreadSafeBool bIsRunning = false;
	FThreadSafeBool bStopRequested = false;
	int32 Port = 9877;

	TMap<FString, FRequestHandler> HandlerRegistry;
};
