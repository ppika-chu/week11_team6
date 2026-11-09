#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimPose.h"
#include "Asset/AssetManager.h"
#include "Render/SkeletalMesh.h"

USkeletalMeshComponent::USkeletalMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	SetSkeletalMesh(UAssetManager::GetAssetByPath<USkeletalMesh>("Models/AllMight/SK_AllMight_root.fbxbin"));
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	ClearAnimScriptInstance();
}

void USkeletalMeshComponent::SetAnimInstanceClass(UClass* NewClass)
{
	if (NewClass && (!NewClass->IsChildOf(UAnimInstance::StaticClass()) || !NewClass->Constructor))
	{
		return;
	}
	if (AnimClass == NewClass && AnimationMode == EAnimationMode::AnimationClass)
	{
		return;
	}

	AnimClass = NewClass;
	AnimationMode = EAnimationMode::AnimationClass;
	ClearAnimScriptInstance();
	InitAnim(true);
}

void USkeletalMeshComponent::InitAnim(bool bForceReinit)
{
	if (IsRegistered() && SkeletalMesh)
	{
		InitializeAnimScriptInstance(bForceReinit);
	}
}

bool USkeletalMeshComponent::InitializeAnimScriptInstance(bool bForceReinit)
{
	UClass* InstanceClass = AnimationMode == EAnimationMode::AnimationSingleNode ? UAnimSingleNodeInstance::StaticClass() : AnimClass;
	if (!InstanceClass)
	{
		return false;
	}
	bool bCreatedInstance = false;
	if (!AnimScriptInstance)
	{
		AnimScriptInstance = NewObject<UAnimInstance>(this, InstanceClass);
		bForceReinit = true;
		bCreatedInstance = true;
	}
	if (AnimScriptInstance && (bForceReinit || !AnimScriptInstance->IsInitialized()))
	{
		AnimScriptInstance->InitializeAnimation();
		// 새로 만든 SingleNode 인스턴스에만 Details 설정을 넘긴다. 기존 인스턴스는 재생 상태를 유지한다
		// (UE InitializeAnimScriptInstance: 새 인스턴스는 AnimationData.Initialize, 기존 것은 PopulateFrom으로 이어받음)
		if (UAnimSingleNodeInstance* SingleNodeInstance = GetSingleNodeInstance(); SingleNodeInstance && bCreatedInstance)
		{
			// UE FSingleAnimationPlayData::Initialize와 같은 순서
			SingleNodeInstance->SetAnimationAsset(AnimToPlay);
			SingleNodeInstance->SetPosition(SavedPosition, false);
			SingleNodeInstance->SetPlayRate(SavedPlayRate);
			SingleNodeInstance->SetPlaying(bSavedPlaying);
			SingleNodeInstance->SetLooping(bSavedLooping);
		}
		return true;
	}
	return false;
}

void USkeletalMeshComponent::ClearAnimScriptInstance()
{
	// 전역 객체 정리로 Instance가 먼저 삭제되었을 수 있으므로 생존 여부를 확인한다.
	if (AnimScriptInstance && FindLiveObjectByAddress(AnimScriptInstance))
	{
		AnimScriptInstance->UninitializeAnimation();
		delete AnimScriptInstance;
	}
	AnimScriptInstance = nullptr;
}

void USkeletalMeshComponent::SetAnimationMode(EAnimationMode::Type InAnimationMode, bool bForceInitAnimScriptInstance)
{
	const bool bNeedChange = AnimationMode != InAnimationMode;
	if (bNeedChange)
	{
		AnimationMode = InAnimationMode;
		ClearAnimScriptInstance();
	}
	if (bNeedChange || (AnimationMode == EAnimationMode::AnimationClass && bForceInitAnimScriptInstance))
	{
		InitAnim(true);
	}
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const
{
	return Cast<UAnimSingleNodeInstance>(AnimScriptInstance);
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping)
{
	SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SetAnimation(NewAnimToPlay);
	Play(bLooping);
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset* NewAnimToPlay)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetAnimationAsset(NewAnimToPlay, false);
		Instance->SetPlaying(false);
	}
}

void USkeletalMeshComponent::Play(bool bLooping)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPlaying(true);
		Instance->SetLooping(bLooping);
	}
}

void USkeletalMeshComponent::Stop()
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPlaying(false);
	}
}

bool USkeletalMeshComponent::IsPlaying() const
{
	const auto* Instance = GetSingleNodeInstance();
	return Instance && Instance->IsPlaying();
}

void USkeletalMeshComponent::SetPosition(float InPos, bool bFireNotifies)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPosition(InPos, bFireNotifies);
	}
}

float USkeletalMeshComponent::GetPosition() const
{
	const auto* Instance = GetSingleNodeInstance();
	return Instance ? Instance->GetCurrentTime() : 0.0f;
}

void USkeletalMeshComponent::SetPlayRate(float Rate)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPlayRate(Rate);
	}
}

float USkeletalMeshComponent::GetPlayRate() const
{
	const auto* Instance = GetSingleNodeInstance();
	return Instance ? Instance->GetPlayRate() : 0.0f;
}

void USkeletalMeshComponent::OnRegister()
{
	EnsurePoseInitialized();
	Super::OnRegister();
	InitAnim(true);
}

void USkeletalMeshComponent::OnUnregister()
{
	if (AnimScriptInstance)
	{
		AnimScriptInstance->UninitializeAnimation();
	}
	Super::OnUnregister();
}

void USkeletalMeshComponent::InitializeComponent()
{
	Super::InitializeComponent();
	InitAnim(false);
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();
	// 씬을 불러올 때는 등록(OnRegister) 뒤에 프로퍼티가 채워진다.
	// 저장된 Animation Mode와 지금 인스턴스 종류가 다르면 다시 만들어 저장된 설정을 적용한다.
	const bool bWantsSingleNode = AnimationMode == EAnimationMode::AnimationSingleNode;
	if (AnimScriptInstance && bWantsSingleNode != (GetSingleNodeInstance() != nullptr))
	{
		ClearAnimScriptInstance();
	}
	InitAnim(false);
}

namespace
{
	// UE FSingleAnimationPlayData의 멤버에 해당하는 프로퍼티
	bool IsAnimationDataProperty(const FString& PropertyName)
	{
		return PropertyName == "AnimToPlay" || PropertyName == "bSavedLooping" || PropertyName == "bSavedPlaying" ||
			PropertyName == "SavedPosition" || PropertyName == "SavedPlayRate";
	}
}

void USkeletalMeshComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	// Details가 값을 직접 바꾼 뒤 부르므로 SetAnimationMode의 변경 비교를 쓸 수 없다. 인스턴스를 새로 만들어 적용한다
	if (Property.Name == "AnimationMode" || IsAnimationDataProperty(Property.Name))
	{
		ClearAnimScriptInstance();
		InitAnim(true);
	}
}

bool USkeletalMeshComponent::CanEditChange(const FProperty& Property) const
{
	// Anim to Play 묶음은 Use Animation Asset 모드에서만 편집한다 (UE는 모드에 맞지 않는 칸을 숨긴다)
	if (IsAnimationDataProperty(Property.Name))
	{
		return AnimationMode == EAnimationMode::AnimationSingleNode;
	}
	return Super::CanEditChange(Property);
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	TickAnimation(DeltaTime);
	if (IsRegistered() && AnimScriptInstance && AnimScriptInstance->IsInitialized())
	{
		RefreshBoneTransforms();
	}
}

void USkeletalMeshComponent::TickAnimation(float DeltaTime)
{
	EnsurePoseInitialized();
	if (IsRegistered() && SkeletalMesh && AnimScriptInstance)
	{
		AnimScriptInstance->UpdateAnimation(DeltaTime);
	}
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& LocalTransform)
{
	EnsurePoseInitialized();
	if (!IsValidBoneIndex(BoneIndex))
	{
		return;
	}

	BoneSpaceTransforms[BoneIndex] = LocalTransform;
	FillComponentSpaceTransforms(BoneSpaceTransforms);
	FinalizeBoneTransform();
}

void USkeletalMeshComponent::SetBoneRotation(int32 BoneIndex, const FRotator& Rotation)
{
	EnsurePoseInitialized();
	if (!IsValidBoneIndex(BoneIndex))
	{
		return;
	}

	// 행벡터 규칙: 먼저 본 원점에서 회전, 그다음 기준 자세(부모까지의 거리 포함)
	// 위치 / 스케일은 기준 자세 그대로 두고 회전만 Rotate * RefRotation으로 합친다 (비균등 스케일이면 근사)
	const FTransform& Ref = SkeletalMesh->RefSkeleton.RefBonePose[BoneIndex];
	const FMatrix Rotate = Rotation.Quaternion().ToFMatrix();
	BoneSpaceTransforms[BoneIndex] = Ref;
	BoneSpaceTransforms[BoneIndex].Rotation = MatrixToRotator(Rotate * Ref.Rotation.ToFMatrix());
	FillComponentSpaceTransforms(BoneSpaceTransforms);
	FinalizeBoneTransform();
}

void USkeletalMeshComponent::ResetToRefPose()
{
	EnsurePoseInitialized();
	if (!SkeletalMesh)
	{
		return;
	}

	BoneSpaceTransforms = SkeletalMesh->RefSkeleton.RefBonePose;
	FillComponentSpaceTransforms(BoneSpaceTransforms);
	FinalizeBoneTransform();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	if (!SkeletalMesh)
	{
		return;
	}

	if (AnimScriptInstance && AnimScriptInstance->IsInitialized())
	{
		FAnimPose EvaluatedPose;
		AnimScriptInstance->EvaluateAnimation(EvaluatedPose);
		if (!FinalizePoseEvaluationResult(EvaluatedPose))
		{
			AnimScriptInstance->DispatchQueuedAnimEvents();
			return;
		}
	}

	FillComponentSpaceTransforms(BoneSpaceTransforms);
	FinalizeBoneTransform();
	if (AnimScriptInstance)
	{
		AnimScriptInstance->DispatchQueuedAnimEvents();
	}
}

bool USkeletalMeshComponent::FinalizePoseEvaluationResult(const FAnimPose& InFinalPose)
{
	if (!SkeletalMesh || InFinalPose.LocalTransforms.Num() != SkeletalMesh->RefSkeleton.RefBonePose.Num())
	{
		HTR_LOG(Error, "[Skel] Evaluated pose must contain every mesh bone");
		return false;
	}

	BoneSpaceTransforms = InFinalPose.LocalTransforms;
	for (FTransform& Transform : BoneSpaceTransforms)
	{
		Transform.Rotation = Transform.Rotation.Normalized();
	}
	return true;
}

void USkeletalMeshComponent::InitPoseFromMesh()
{
	ClearAnimScriptInstance();
	// 임포터가 저장한 기준 자세(FTransform, 부모 상대)를 시작 포즈로 준비한 뒤
	// 부모가 배열 크기와 렌더 메시를 만들고 RefreshBoneTransforms를 부른다
	if (SkeletalMesh)
	{
		BoneSpaceTransforms = SkeletalMesh->RefSkeleton.RefBonePose;
	}
	else
	{
		BoneSpaceTransforms.Reset();
	}

	Super::InitPoseFromMesh();
	InitAnim(true);
}
