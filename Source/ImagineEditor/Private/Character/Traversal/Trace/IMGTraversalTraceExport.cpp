#include "Character/Traversal/Trace/IMGTraversalTraceExport.h"
#include "Character/Traversal/Trace/IMGTraversalTraceProvider.h"
#include "Dom/JsonObject.h"
#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "IRewindDebugger.h"
#include "JsonObjectConverter.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/Archive.h"
#include "UObject/UnrealType.h"

namespace IMGTraversalRewind
{
namespace
{
TSharedPtr<FJsonObject> RecordObject(const UStruct* Type, const void* Record);
TSharedPtr<FJsonValue> ExportValue(FProperty* Property, const void* Value)
{
	if (const auto* Signed = CastField<FInt64Property>(Property))
	{
		return MakeShared<FJsonValueString>(LexToString(Signed->GetPropertyValue(Value)));
	}
	if (const auto* Unsigned = CastField<FUInt64Property>(Property))
	{
		return MakeShared<FJsonValueString>(LexToString(Unsigned->GetPropertyValue(Value)));
	}
	if (const auto* Struct = CastField<FStructProperty>(Property))
	{
		const auto Object = RecordObject(Struct->Struct, Value);
		if (Object)
		{
			return MakeShared<FJsonValueObject>(Object);
		}
	}
	return nullptr;
}
TSharedPtr<FJsonObject> RecordObject(const UStruct* Type, const void* Record)
{
	// Preserve identities and expand math fields instead of using native rounded ExportTextItem strings.
	static const auto Export = FJsonObjectConverter::CustomExportCallback::CreateStatic(&ExportValue);
	auto Object = MakeShared<FJsonObject>();
	if (FJsonObjectConverter::UStructToJsonObject(Type, Record, Object, 0, 0, &Export))
	{
		return Object;
	}
	return nullptr;
}
bool WriteLine(FArchive& File, const TSharedRef<FJsonObject>& Object)
{
	FString Text;
	if (!FJsonSerializer::Serialize(
	        Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)))
	{
		return false;
	}
	Text += TEXT("\n");
	const FTCHARToUTF8 Utf8(*Text);
	File.Serialize(const_cast<void*>(static_cast<const void*>(Utf8.Get())), Utf8.Length());
	return !File.IsError();
}
TSharedRef<FJsonObject> Metadata()
{
	auto Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("type"), TEXT("metadata"));
	Object->SetStringField(TEXT("schema"), TEXT("ImagineGame.Traversal.DebugExport"));
	Object->SetNumberField(TEXT("version"), 2);
	Object->SetStringField(TEXT("math_encoding"), TEXT("expanded reflected numeric fields"));
	Object->SetStringField(
	    TEXT("trace_events"), TEXT("IMGTraversal/FrameV2, IMGTraversal/FrameV3, IMGTraversal/FrameV4"));
	Object->SetStringField(TEXT("height_basis"),
	    TEXT(
	        "Centimeters of world Z relative to the prepared capsule center. MontageRootHeight starts at the selected StartTime along its section path, without Warp. WarpedCapsuleHeight is observed execution including Warp and CMC. Compare only valid samples of the same HeightCapture."));
	Object->SetStringField(TEXT("exported_utc"), FDateTime::UtcNow().ToIso8601());
	Object->SetStringField(TEXT("int64_encoding"), TEXT("decimal strings"));
	Object->SetStringField(TEXT("time_basis"), TEXT("Object World recording elapsed seconds"));
	Object->SetStringField(
	    TEXT("role_values"), TEXT("0=None, 1=Simulated Proxy, 2=Autonomous Proxy, 3=Authority"));
	Object->SetStringField(
	    TEXT("net_mode_values"), TEXT("0=Standalone, 1=Dedicated Server, 2=Listen Server, 3=Client"));
	Object->SetStringField(
	    TEXT("scope"), TEXT("All recorded fields for one actor; display filters do not apply."));
	return Object;
}
} // namespace
bool FrameToJson(const FIMGTraversalDebugFrame& Frame, FString& OutText)
{
	OutText.Reset();
	const auto Object = RecordObject(FIMGTraversalDebugFrame::StaticStruct(), &Frame);
	if (!Object)
	{
		return false;
	}
	const auto Root = Metadata();
	Root->SetStringField(TEXT("type"), TEXT("frame_snapshot"));
	Root->SetObjectField(TEXT("frame"), Object);
	return FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&OutText));
}
bool RecordToJson(const UStruct* Type, const void* Record, FString& OutText)
{
	OutText.Reset();
	if (!Type || !Record)
	{
		return false;
	}
	const auto Object = RecordObject(Type, Record);
	return Object && FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&OutText));
}
bool ExportVisibleRange(uint64 ActorId, FString& OutPath, FString& OutError)
{
	OutPath.Reset();
	OutError.Reset();
	const auto* Debugger = IRewindDebugger::Instance();
	const auto Session = Debugger ? Debugger->GetAnalysisSessionAsShared() : nullptr;
	if (!Debugger || !Session || Debugger->IsRecording())
	{
		OutError = TEXT("Stop recording and select a recorded Traversal track before exporting.");
		return false;
	}
	const auto Range = Debugger->GetCurrentViewRange();
	if (!Range.HasLowerBound() || !Range.HasUpperBound())
	{
		OutError = TEXT("The timeline has no bounded visible range.");
		return false;
	}
	const double Start = Range.GetLowerBoundValue(), End = Range.GetUpperBoundValue();
	if (!FMath::IsFinite(Start) || !FMath::IsFinite(End) || End < Start)
	{
		OutError = TEXT("The visible time range is invalid.");
		return false;
	}
	TArray<TSharedPtr<const FIMGTraversalDebugFrame>> Frames;
	{
		TraceServices::FAnalysisSessionReadScope Lock(*Session);
		if (const auto* Provider = Session->ReadProvider<FProvider>(FProvider::Name))
		{
			Provider->ReadFrames(ActorId, Start, End, Frames);
		}
	}
	// Retain immutable frames; conversion and disk I/O must not hold the analysis lock.
	if (Frames.IsEmpty())
	{
		OutError = TEXT("No analyzed Traversal frames for this actor in the visible range.");
		return false;
	}
	const FString Directory =
	    FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Debug/Traversal"));
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = TEXT("Could not create the Traversal export directory.");
		return false;
	}
	const FString Filename = FString::Printf(TEXT("Traversal_%s_%s_%s.jsonl"), *LexToString(ActorId),
	    *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")),
	    *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString Path = Directory / Filename;
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_NoReplaceExisting));
	if (!File)
	{
		OutError = TEXT("Could not create the export file.");
		return false;
	}
	const auto Header = Metadata();
	Header->SetStringField(TEXT("actor_id"), LexToString(ActorId));
	Header->SetStringField(TEXT("actor"), Frames[0]->Actor);
	Header->SetStringField(TEXT("world"), Frames[0]->World);
	Header->SetNumberField(TEXT("range_start"), Start);
	Header->SetNumberField(TEXT("range_end"), End);
	Header->SetNumberField(TEXT("frame_count"), Frames.Num());
	Header->SetStringField(
	    TEXT("range_policy"), TEXT("Frames overlapping the visible range; includes whole boundary frames."));
	Header->SetBoolField(TEXT("analysis_snapshot"), true);
	bool bSuccess = WriteLine(*File, Header);
	bool bCancelled = false;
	FScopedSlowTask Progress(Frames.Num(), FText::FromString(TEXT("Exporting Traversal recording")));
	Progress.MakeDialogDelayed(.5f, true);
	for (const auto& Frame : Frames)
	{
		if (!bSuccess || (bCancelled = Progress.ShouldCancel()))
		{
			break;
		}
		Progress.EnterProgressFrame();
		const auto Object = RecordObject(FIMGTraversalDebugFrame::StaticStruct(), Frame.Get());
		if (!Object)
		{
			bSuccess = false;
			break;
		}
		const auto Record = MakeShared<FJsonObject>();
		Record->SetStringField(TEXT("type"), TEXT("frame"));
		Record->SetObjectField(TEXT("frame"), Object);
		bSuccess = WriteLine(*File, Record);
	}
	const bool bClosed = File->Close() && !File->IsError();
	File.Reset();
	if (!bSuccess || !bClosed || bCancelled)
	{
		OutError = bCancelled ? TEXT("Export cancelled.") : TEXT("Export failed.");
		if (!IFileManager::Get().Delete(*Path))
		{
			OutError += TEXT(" Could not remove partial file: ") + Path;
		}
		return false;
	}
	OutPath = Path;
	return true;
}
} // namespace IMGTraversalRewind
