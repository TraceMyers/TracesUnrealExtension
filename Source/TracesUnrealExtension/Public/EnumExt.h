#pragma once

#include "CoreMinimal.h"

namespace EnumExt
{
	template<typename T>
	constexpr auto ToInt(T E)
	{
		static_assert(std::is_enum_v<T>);
		return static_cast<std::underlying_type_t<T>>(E);
	}
	
	template<typename T>
	constexpr T FromInt(uint64 X)
	{
		static_assert(std::is_enum_v<T>);
		return static_cast<T>(X);
	}
	
	template<typename T>
	FORCEINLINE bool HasFlag(const T Flags, T Other)
	{
		return (ToInt(Flags) & ToInt(Other)) != 0;
	}
	
	template<typename T>
	FORCEINLINE bool OverlapAny(const T Flags, T Other)
	{
		return (ToInt(Flags) & ToInt(Other)) != 0;
	}
	
	template<typename T>
	FORCEINLINE bool OverlapAll(const T Flags, T Other)
	{
		return (ToInt(Flags) & ToInt(Other)) == ToInt(Other);
	}
	
	template<typename T>
	FORCEINLINE void AddFlags(T& Flags, T Add)
	{
		auto NewFlags = ToInt(Flags) | ToInt(Add);
		Flags = FromInt<T>(NewFlags);
	}
	
	template<typename T>
	FORCEINLINE void RemoveFlags(T& Flags, T Remove)
	{
		auto NewFlags = ToInt(Flags) & ~ToInt(Remove);
		Flags = FromInt<T>(NewFlags);
	}
	
	template<typename T>
	FORCEINLINE void Clear(T& E)
	{
		E = FromInt<T>(0);
	}
	
	template<typename T>
	FORCEINLINE T Or(T A, T B)
	{
		return FromInt<T>(ToInt(A) | ToInt(B));
	}
	
	template<typename T>
	FORCEINLINE T And(T A, T B)
	{
		return FromInt<T>(ToInt(A) & ToInt(B));
	}
}