#include "EnginePCH.h"
#include "BlendSpace1D.h"

UBlendSpace1D::UBlendSpace1D()
{
	// X축을 지정한다. 기반 클래스의 구성·구간 생성·검색이 이 축을 사용한다.
	DimensionIndices = { 0 };
}
