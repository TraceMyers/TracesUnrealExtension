#pragma once

#include "Runtime/Engine/Classes/Engine/EngineTypes.h"

// helper macros for creating components in actor constructors.
// e.g. CREATE_PRIMITIVE_COMPONENT(MyComponentVar, .bVisible=true, .bSimulatePhysics=true);

struct FCreateComponentParams
{
	// for SetupAttachment(), which sets the transform parent.
	// leave as nullptr if the component is being created as root, or if you want it to attach to the existing root.
	USceneComponent* AttachTo = nullptr;
	// set either this or CollisionEnabled, or neither if no collision.
	const FName CollisionProfile = TEXT("");
	// if left empty, the component variable name will be used. this may be necessary to use of the component is
	// stored on a member variable, e.g. "VisualStuff.ProceduralMesh" is invalid because of the '.'
	const FName ComponentName = TEXT("");
	bool bCastShadow      = false;
	bool bVisible         = false;
	bool bReceivesDecals  = false;
	bool bSimulatePhysics = false;
	// set either this or CollisionProfile, or neither if no collision.
	ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::NoCollision;
	EComponentMobility::Type ComponentMobility = EComponentMobility::Movable;
};

#define CREATE_ROOT_COMPONENT()														\
	do {																			\
		if (!GetRootComponent())													\
		{																			\
			SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));\
		}																			\
	} while (0)

inline FName ChooseComponentName(FName FieldName, FName OverrideName)
{
	FName OutName = OverrideName != NAME_None ? OverrideName : FieldName;
	FText InvalidReason;
	checkf(OutName.IsValidObjectName(InvalidReason), TEXT("Invalid object name %s because %s"), *OutName.ToString(), *InvalidReason.ToString())
	return OutName;
}

// The size/functionality hierarchy from most to least is primitive component, scene component, actor component
// I think the names are bad, but it's important to know so that you pick the right initialization macro.

// ..._NO_ATTACHMENT() macros are the implementations for the other versions. If you use them on their own,
// you should wrap them in a scope, like { CREATE_PRIMITIVE_COMPONENT_NO_ATTACHMENT(propname, args) }
// so that the expanded stack variables don't collide with other macro expansions.

#define CREATE_PRIMITIVE_COMPONENT_NO_ATTACHMENT(PropName, ...)																\
	const FCreateComponentParams CreateComponent_Params = {__VA_ARGS__};													\
	const FName CreateComponent_Name = ChooseComponentName(FName(TEXT(#PropName)), CreateComponent_Params.ComponentName);	\
	PropName = CreateDefaultSubobject<std::remove_reference_t<decltype(*PropName)>>(CreateComponent_Name);					\
	if (CreateComponent_Params.CollisionProfile != NAME_None)																\
	{																														\
		PropName->SetCollisionProfileName(CreateComponent_Params.CollisionProfile);											\
	}																														\
	else																													\
	{																														\
		PropName->SetCollisionEnabled(CreateComponent_Params.CollisionEnabled);												\
	}																														\
	PropName->CastShadow = CreateComponent_Params.bCastShadow;																\
	PropName->SetVisibility(CreateComponent_Params.bVisible);																\
	PropName->bReceivesDecals = CreateComponent_Params.bReceivesDecals;														\
	PropName->SetSimulatePhysics(CreateComponent_Params.bSimulatePhysics);													\
	PropName->SetMobility(CreateComponent_Params.ComponentMobility);														\

#define CREATE_PRIMITIVE_COMPONENT(PropName, ...)																			\
	do {																													\
		CREATE_PRIMITIVE_COMPONENT_NO_ATTACHMENT(PropName, __VA_ARGS__)														\
		PropName->SetupAttachment(CreateComponent_Params.AttachTo ? CreateComponent_Params.AttachTo : GetRootComponent());	\
	} while (0)

#define CREATE_PRIMITIVE_COMPONENT_AS_ROOT(PropName, ...)				\
	do {																\
		CREATE_PRIMITIVE_COMPONENT_NO_ATTACHMENT(PropName, __VA_ARGS__)	\
		SetRootComponent(PropName);										\
	} while (0)

#define CREATE_SCENE_COMPONENT_NO_ATTACHMENT(PropName, ...)																	\
	const FCreateComponentParams CreateComponent_Params = {__VA_ARGS__};													\
	const FName CreateComponent_Name = ChooseComponentName(FName(TEXT(#PropName)), CreateComponent_Params.ComponentName);	\
	PropName = CreateDefaultSubobject<std::remove_reference_t<decltype(*PropName)>>(CreateComponent_Name);					\
	PropName->SetVisibility(CreateComponent_Params.bVisible);																\
	PropName->SetMobility(CreateComponent_Params.ComponentMobility);														\

#define CREATE_SCENE_COMPONENT(PropName, ...)																				\
	do {																													\
		CREATE_SCENE_COMPONENT_NO_ATTACHMENT(PropName, __VA_ARGS__)															\
		PropName->SetupAttachment(CreateComponent_Params.AttachTo ? CreateComponent_Params.AttachTo : GetRootComponent());\
	} while (0)

#define CREATE_SCENE_COMPONENT_AS_ROOT(PropName, ...)				\
    do {															\
		CREATE_SCENE_COMPONENT_NO_ATTACHMENT(PropName, __VA_ARGS__)	\
        SetRootComponent(PropName);									\
    } while (0)

#define CREATE_ACTOR_COMPONENT(PropName, ...)																					\
	do {																														\
		const FCreateComponentParams CreateComponent_Params = {__VA_ARGS__};													\
		const FName CreateComponent_Name = ChooseComponentName(FName(TEXT(#PropName)), CreateComponent_Params.ComponentName);	\
		PropName = CreateDefaultSubobject<std::remove_reference_t<decltype(*PropName)>>(CreateComponent_Name);					\
	} while (0)