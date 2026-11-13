#pragma once
#include "CoreMinimal.h"
struct FIMGTraversalDebugFrame;
namespace IMGTraversalDebugTrace
{
void Startup();
void Shutdown();
bool IsRecording();
uint64 GetRecordingSerial();
void Output(const UObject* Actor, FIMGTraversalDebugFrame& Frame);
} // namespace IMGTraversalDebugTrace
