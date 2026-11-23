#include "Character/SRCombatDummy.h"
#include "Debug/SRDebug.h"
#include "GAS/PSAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"

APSCombatDummy::APSCombatDummy()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    SetReplicates(true);

    USphereComponent* Root = CreateDefaultSubobject<USphereComponent>(TEXT("DummyRoot"));
    Root->InitSphereRadius(60.f);
    // Present as a Pawn so the combat trace and projectile queries treat it as a damage target.
    Root->SetCollisionObjectType(ECC_Pawn);
    Root->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Root->SetCollisionObjectType(ECC_Pawn);
    RootComponent = Root;

    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DummyBody"));
    BodyMesh->SetupAttachment(RootComponent);
    BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BodyMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    BodyMesh->SetRelativeScale3D(FVector(1.0f, 1.0f, 1.8f));

    StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DummyStatusText"));
    StatusText->SetupAttachment(RootComponent);
    StatusText->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
    StatusText->SetTextRenderColor(FColor::Green);
    StatusText->SetWorldSize(24.f);

    DummyAbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("DummyAbilitySystem"));
    DummyAttributes = CreateDefaultSubobject<UPSAttributeSet>(TEXT("DummyAttributes"));
}

UAbilitySystemComponent* APSCombatDummy::GetAbilitySystemComponent() const { return DummyAbilitySystem; }

void APSCombatDummy::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    // Attribute-set registration happens once in BeginPlay: a second
    // InitAbilityActorInfo call resets manually-added sets.
}

void APSCombatDummy::BeginPlay()
{
    Super::BeginPlay();
    // Deterministic registration once the component is registered (transient test
    // worlds defer PostInitializeComponents-time attribute registration).
    if (DummyAbilitySystem)
    {
        DummyAbilitySystem->AddAttributeSetSubobject(DummyAttributes.Get());
        DummyAbilitySystem->InitAbilityActorInfo(this, this);
        // Live health/shield display via GAS attribute change notifications.
        DummyAbilitySystem->GetGameplayAttributeValueChangeDelegate(UPSAttributeSet::GetHealthAttribute()).AddUObject(this, &APSCombatDummy::UpdateDisplay);
        DummyAbilitySystem->GetGameplayAttributeValueChangeDelegate(UPSAttributeSet::GetShieldAttribute()).AddUObject(this, &APSCombatDummy::UpdateDisplay);
        UpdateDisplay(FOnAttributeChangeData());
    }
}

float APSCombatDummy::GetDummyHealth() const { return DummyAttributes ? DummyAttributes->GetHealth() : 0.f; }
float APSCombatDummy::GetDummyShield() const { return DummyAttributes ? DummyAttributes->GetShield() : 0.f; }

void APSCombatDummy::UpdateDisplay(const FOnAttributeChangeData& /*UnusedData*/)
{
    if (!StatusText || !DummyAttributes) return;
    StatusText->SetText(FText::FromString(FString::Printf(TEXT("HP %.0f / SH %.0f"),
        DummyAttributes->GetHealth(), DummyAttributes->GetShield())));
    SRDebug::Log(SRDebug::ECategory::GAS, TEXT("Dummy"),
        FString::Printf(TEXT("%s health=%.1f shield=%.1f"), *GetName(), DummyAttributes->GetHealth(), DummyAttributes->GetShield()));
}
