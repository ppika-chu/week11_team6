#pragma once

// 한 프레임에서 실제 시간과 Time Dilation이 적용된 월드 시간을 함께 보관한다.
// 시간 배율 정책은 UWorld가 관리하고 FGameTime은 계산 결과만 제공한다.
class FGameTime
{
public:
	float GetDeltaRealTimeSeconds() const { return DeltaRealTimeSeconds; }
	float GetDeltaWorldTimeSeconds() const { return DeltaWorldTimeSeconds; }
	double GetRealTimeSeconds() const { return RealTimeSeconds; }
	double GetWorldTimeSeconds() const { return WorldTimeSeconds; }
	float GetTimeDilation() const { return TimeDilation; }

private:
	friend class UWorld;

	double RealTimeSeconds = 0.0;
	double WorldTimeSeconds = 0.0;
	float DeltaRealTimeSeconds = 0.0f;
	float DeltaWorldTimeSeconds = 0.0f;
	float TimeDilation = 1.0f;
};
