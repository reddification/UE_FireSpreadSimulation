// Fill out your copyright notice in the Description page of Project Settings.


#include "FSGameState.h"

#include "Components/WindComponent.h"

AFSGameState::AFSGameState()
{
	WindComponent = CreateDefaultSubobject<UWindComponent>("WindComponent");
}
