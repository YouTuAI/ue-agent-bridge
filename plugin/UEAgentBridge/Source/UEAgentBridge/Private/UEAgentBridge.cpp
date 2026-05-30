#include "UEAgentBridge.h"
#include "BridgeServer.h"

#define LOCTEXT_NAMESPACE "FUEAgentBridgeModule"

void FUEAgentBridgeModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("[UEAgentBridge] Module starting..."));
	FBridgeServer::Get().Start();
}

void FUEAgentBridgeModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("[UEAgentBridge] Module shutting down..."));
	FBridgeServer::Get().Shutdown();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUEAgentBridgeModule, UEAgentBridge)
