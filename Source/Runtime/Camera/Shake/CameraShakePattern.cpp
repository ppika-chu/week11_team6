#include "EnginePCH.h"
#include "CameraShakePattern.h"

// ---------------------------------------------------------------------------
// FSimpleCameraShakePattern

void FSimpleCameraShakePattern::StartShakePattern()
{
	const bool bWasActive{ mState.IsActive() };
	if (!bWasActive)
	{
		mState.Initialize(StateParams);
		StartShakePatternImpl();
	}
	mState.Start();
}

void FSimpleCameraShakePattern::UpdateShakePattern(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
{
	OutResult.Reset();

	const float BlendWeight{ mState.Update(Params.DeltaTime) };
	if (BlendWeight <= 0.0f)
	{
		return;
	}

	UpdateShakePatternImpl(Params, OutResult);
	OutResult.ApplyScale(BlendWeight);
}

void FSimpleCameraShakePattern::StopShakePattern(bool bImmediately)
{
	mState.Stop(bImmediately);
}

bool FSimpleCameraShakePattern::IsFinished() const
{
	return !mState.IsActive();
}

// ---------------------------------------------------------------------------
// FWaveOscillatorCameraShakePattern

void FWaveOscillatorCameraShakePattern::StartShakePatternImpl()
{
	X.Initialize(mCurrentLocationOffset.X);
	Y.Initialize(mCurrentLocationOffset.Y);
	Z.Initialize(mCurrentLocationOffset.Z);
	Pitch.Initialize(mCurrentRotationOffset.X);
	Yaw.Initialize(mCurrentRotationOffset.Y);
	Roll.Initialize(mCurrentRotationOffset.Z);
	FOV.Initialize(mCurrentFOVOffset);
}

void FWaveOscillatorCameraShakePattern::UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
{
	const float Dt{ Params.DeltaTime };

	OutResult.Location.X = X.Update(Dt, LocationAmplitudeMultiplier, LocationFrequencyMultiplier, mCurrentLocationOffset.X);
	OutResult.Location.Y = Y.Update(Dt, LocationAmplitudeMultiplier, LocationFrequencyMultiplier, mCurrentLocationOffset.Y);
	OutResult.Location.Z = Z.Update(Dt, LocationAmplitudeMultiplier, LocationFrequencyMultiplier, mCurrentLocationOffset.Z);

	OutResult.Rotation.Pitch = Pitch.Update(Dt, RotationAmplitudeMultiplier, RotationFrequencyMultiplier, mCurrentRotationOffset.X);
	OutResult.Rotation.Yaw = Yaw.Update(Dt, RotationAmplitudeMultiplier, RotationFrequencyMultiplier, mCurrentRotationOffset.Y);
	OutResult.Rotation.Roll = Roll.Update(Dt, RotationAmplitudeMultiplier, RotationFrequencyMultiplier, mCurrentRotationOffset.Z);

	OutResult.FOV = FOV.Update(Dt, 1.0f, 1.0f, mCurrentFOVOffset);
}

// ---------------------------------------------------------------------------
// FPerlinNoiseCameraShakePattern

void FPerlinNoiseCameraShakePattern::StartShakePatternImpl()
{
	X.Initialize(mCurrentLocationOffset.X);
	Y.Initialize(mCurrentLocationOffset.Y);
	Z.Initialize(mCurrentLocationOffset.Z);
	Pitch.Initialize(mCurrentRotationOffset.X);
	Yaw.Initialize(mCurrentRotationOffset.Y);
	Roll.Initialize(mCurrentRotationOffset.Z);
	FOV.Initialize(mCurrentFOVOffset);
}

void FPerlinNoiseCameraShakePattern::UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
{
	const float Dt{ Params.DeltaTime };

	OutResult.Location.X = X.Update(Dt, LocationAmplitudeMultiplier, LocationFrequencyMultiplier, mCurrentLocationOffset.X);
	OutResult.Location.Y = Y.Update(Dt, LocationAmplitudeMultiplier, LocationFrequencyMultiplier, mCurrentLocationOffset.Y);
	OutResult.Location.Z = Z.Update(Dt, LocationAmplitudeMultiplier, LocationFrequencyMultiplier, mCurrentLocationOffset.Z);

	OutResult.Rotation.Pitch = Pitch.Update(Dt, RotationAmplitudeMultiplier, RotationFrequencyMultiplier, mCurrentRotationOffset.X);
	OutResult.Rotation.Yaw = Yaw.Update(Dt, RotationAmplitudeMultiplier, RotationFrequencyMultiplier, mCurrentRotationOffset.Y);
	OutResult.Rotation.Roll = Roll.Update(Dt, RotationAmplitudeMultiplier, RotationFrequencyMultiplier, mCurrentRotationOffset.Z);

	OutResult.FOV = FOV.Update(Dt, 1.0f, 1.0f, mCurrentFOVOffset);
}

// ---------------------------------------------------------------------------
// FCompositeCameraShakePattern

FCameraShakePattern* FCompositeCameraShakePattern::AddChildPattern(std::unique_ptr<FCameraShakePattern> Pattern)
{
	if (!Pattern)
	{
		return nullptr;
	}
	FCameraShakePattern* Raw{ Pattern.get() };
	mChildPatterns.Emplace(std::move(Pattern));
	return Raw;
}

void FCompositeCameraShakePattern::StartShakePattern()
{
	for (auto& Child : mChildPatterns)
	{
		Child->StartShakePattern();
	}
}

void FCompositeCameraShakePattern::UpdateShakePattern(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
{
	OutResult.Reset();

	FCameraShakeUpdateResult ChildResult{};
	for (auto& Child : mChildPatterns)
	{
		if (Child->IsFinished())
		{
			continue;
		}
		Child->UpdateShakePattern(Params, ChildResult);
		OutResult.Add(ChildResult);
	}
}

void FCompositeCameraShakePattern::StopShakePattern(bool bImmediately)
{
	for (auto& Child : mChildPatterns)
	{
		Child->StopShakePattern(bImmediately);
	}
}

bool FCompositeCameraShakePattern::IsFinished() const
{
	for (const auto& Child : mChildPatterns)
	{
		if (!Child->IsFinished())
		{
			return false;
		}
	}
	return true;
}
