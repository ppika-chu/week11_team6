#pragma once

// 쉐이크 재생 시간 설정
struct FCameraShakeStateParams
{
	float Duration{ 1.0f }; // 0 이하이면 Stop을 호출할 때까지 무한 재생
	float BlendInTime{ 0.2f };
	float BlendOutTime{ 0.2f };

	bool IsInfinite() const
	{
		return Duration <= 0.0f;
	}
};

// 경과 시간, BlendIn/Out 가중치, 종료 판정을 맡는다.
// 패턴은 매 프레임 Update가 돌려준 가중치를 결과에 곱하기만 하면 된다.
class FCameraShakeState
{
public:
	void Initialize(const FCameraShakeStateParams& InParams);

	// 처음부터 재생한다. 블렌드아웃 중에 다시 시작하면 현재 가중치에서 이어서 블렌드인한다.
	void Start();

	// 시간을 진행시키고 이번 프레임의 블렌드 가중치(0~1)를 반환한다. 끝났으면 0이다.
	float Update(float DeltaTime);

	// bImmediately가 false이면 지금부터 BlendOutTime 동안 블렌드아웃한 뒤 끝난다.
	void Stop(bool bImmediately);

	bool IsActive() const { return mIsActive; }
	bool IsBlendingIn() const { return mIsBlendingIn; }
	bool IsBlendingOut() const { return mIsBlendingOut; }
	float GetElapsedTime() const { return mElapsedTime; }
	float GetCurrentWeight() const { return mCurrentWeight; }
	const FCameraShakeStateParams& GetParams() const { return mParams; }

private:
	float CalcBlendInWeight() const;
	float CalcBlendOutWeight() const;

private:
	FCameraShakeStateParams mParams{};

	float mElapsedTime{};
	float mBlendOutElapsedTime{}; // Stop으로 시작한 블렌드아웃의 경과 시간
	float mCurrentWeight{};

	bool mIsActive{};
	bool mIsBlendingIn{};
	bool mIsBlendingOut{}; // Stop으로 시작한 블렌드아웃 중인지
};
