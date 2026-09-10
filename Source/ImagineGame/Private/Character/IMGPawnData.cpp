// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/IMGPawnData.h"
#include "Camera/IMGCameraMode_Default.h"

UIMGPawnData::UIMGPawnData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultCameraMode = UIMGCameraMode_Default::StaticClass();
}
