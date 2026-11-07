#include "EnginePCH.h"
#include "BlendSpace1D.h"

UBlendSpace1D::UBlendSpace1D()
{
	// UE처럼 파생 Asset이 사용할 축을 지정하고 기반 클래스가 이를 소비한다.
	DimensionIndices = { 0 };
}
