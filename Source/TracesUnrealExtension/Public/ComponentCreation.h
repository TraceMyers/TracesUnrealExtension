#pragma once

#include "Runtime/Engine/Classes/Engine/EngineTypes.h"

// convenience helper macros for creating components in actor constructors.
// e.g. CREATE_PRIMITIVE_COMPONENT(MyComponentVar, .bVisible=true, .bSimulatePhysics=true);

struct FCreateComponentParams
{
	USceneComponent* AttachTo = nullptr;
	const wchar_t* CollisionProfile = L"";
	bool bCastShadow      = false;
	bool bVisible         = false;
	bool bReceivesDecals  = false;
	bool bSimulatePhysics = false;
	ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::NoCollision;
	EComponentMobility::Type ComponentMobility = EComponentMobility::Movable;
};

#define CREATE_ROOT_COMPONENT()												\
	if (!GetRootComponent())												\
	{																		\
		SetRootComponent(CreateDefaultSubobject<USceneComponent>(L"Root")); \
	}

// The size/functionality hierarchy from most to least is primitive component, scene component, actor component
// I think the names are bad, but it's important to know so that you pick the right initialization macro.

#define CREATE_PRIMITIVE_COMPONENT(PropName, ...) \
	do {                                                                                                    \
		PropName = CreateDefaultSubobject<std::remove_reference_t<decltype(*PropName)>>(TEXT(#PropName));   \
		const FCreateComponentParams Params = {__VA_ARGS__};												\
		PropName->SetupAttachment(Params.AttachTo ? Params.AttachTo : GetRootComponent());					\
		if (*Params.CollisionProfile != L'\0') {															\
			PropName->SetCollisionProfileName(Params.CollisionProfile);										\
		}																									\
		PropName->CastShadow = Params.bCastShadow;															\
		PropName->SetVisibility(Params.bVisible);															\
		PropName->bReceivesDecals = Params.bReceivesDecals;													\
		PropName->SetSimulatePhysics(Params.bSimulatePhysics);												\
		PropName->SetCollisionEnabled(Params.CollisionEnabled);												\
		PropName->SetMobility(Params.ComponentMobility);													\
} while (0);

#define CREATE_PRIMITIVE_COMPONENT_AS_ROOT(PropName, ...)	\
	do {													\
		CREATE_PRIMITIVE_COMPONENT(PropName, __VA_ARGS__)	\
		SetRootComponent(PropName);							\
	} while (0);

#define CREATE_SCENE_COMPONENT(PropName, ...)																\
	do {                                                                                                    \
		PropName = CreateDefaultSubobject<std::remove_reference_t<decltype(*PropName)>>(TEXT(#PropName));   \
		const FCreateComponentParams Params = {__VA_ARGS__};												\
		PropName->SetupAttachment(Params.AttachTo ? Params.AttachTo : GetRootComponent());					\
		PropName->SetVisibility(Params.bVisible);															\
	} while (0);

#define CREATE_SCENE_COMPONENT_AS_ROOT(PropName, ...)	\
    do {												\
		CREATE_SCENE_COMPONENT(PropName, __VA_ARGS__)	\
        SetRootComponent(PropName);						\
    } while (0);

#define CREATE_ACTOR_COMPONENT(PropName)																	\
	do {                                                                                                    \
		PropName = CreateDefaultSubobject<std::remove_reference_t<decltype(*PropName)>>(TEXT(#PropName));   \
	} while (0);