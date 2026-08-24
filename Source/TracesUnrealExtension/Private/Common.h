#pragma once

#define TUE_LOG_HEADER TEXT("[Traces Unreal Extension Tests] ")
#define TUE_LOG(Fmt, ...) \
	UE_LOG(LogTemp, Warning, TUE_LOG_HEADER TEXT(Fmt), __VA_ARGS__)
#define TUE_ERROR(Fmt, ...) \
	UE_LOG(LogTemp, Error, TUE_LOG_HEADER TEXT(Fmt), __VA_ARGS__)