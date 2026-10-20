#pragma once

#include "CoreMinimal.h"

class UWorld;

namespace IMGTraversalDebug
{
#if ENABLE_DRAW_DEBUG
	bool IsEnabled(int32 RequiredLevel = 1);
	void Line(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color,
		int32 RequiredLevel = 1, float Thickness = 1.f, float Duration = -1.f);
	void Arrow(const UWorld* World, const FVector& Start, const FVector& End, const FColor& Color,
		int32 RequiredLevel = 1, float Duration = -1.f);
	void Point(const UWorld* World, const FVector& Location, const FColor& Color,
		int32 RequiredLevel = 1, float Size = 10.f, float Duration = -1.f);
	void Sphere(const UWorld* World, const FVector& Center, float Radius, const FColor& Color,
		int32 RequiredLevel = 1, float Duration = -1.f);
	void Capsule(const UWorld* World, const FVector& Center, float HalfHeight, float Radius, const FColor& Color,
		int32 RequiredLevel = 1, float Duration = -1.f);
	void String(const UWorld* World, const FVector& Location, const FString& Text, const FColor& Color,
		int32 RequiredLevel = 1, float Duration = -1.f);
#endif
}
