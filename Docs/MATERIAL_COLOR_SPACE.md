# 머티리얼 색 공간

## 저장 규약

머티리얼 RGB는 선형 값이다. 복제, 씬·애셋 저장/로딩과 GPU 파라미터 전송에 같은 규약을 사용한다.

| 프로퍼티 | 의미 |
| --- | --- |
| DiffuseOrFactor.RGB | 색상 맵이 없으면 diffuse 계수, 있으면 맵 RGB에 곱하는 factor |
| DiffuseOrFactor.W | 불투명도 |
| SpecularColor, SpecularFactor | 선형 반사광 색상과 강도 계수 |
| EmissiveColor, EmissiveFactor | 선형 자체 발광 색상과 강도 계수. HDR 값을 유지한다. |

FBX 상수 RGB와 OBJ/MTL Kd는 선형 값으로 해석한다.
FBX 프로퍼티 매핑은 [FBX_IMPORT_POLICY.md](FBX_IMPORT_POLICY.md)의 머티리얼 항목을 따른다.

## 편집과 셰이더 평가

- 에디터 색상 선택기는 선형 RGB를 sRGB로 표시하고, 입력한 RGB를 선형으로 변환해 저장한다. 알파는 그대로 저장한다.
- SpecularColor·EmissiveColor 편집은 HDR 값을 지원한다. FLinearColor 프로퍼티도 같은 선형 RGB 저장 규약을 사용한다.
- 메시·데칼 셰이더는 색상 텍스처의 RGB를 sRGB에서 선형으로 디코딩한 뒤 DiffuseOrFactor를 곱한다.
- Blinn-Phong 반사광에는 SpecularColor × SpecularFactor를 곱한다.
- EmissiveColor × EmissiveFactor는 조명·그림자 계산 후 더한다.
- 데칼 컴포넌트의 색상과 안개 색상은 선형 값으로 평가한다.
- 노멀맵과 불투명도 마스크는 수치 데이터로 샘플링한다.
- 조명 결과는 HDR 선형 공간에 쌓고 마지막 톤매핑 패스에서 표시용 sRGB로 인코딩한다.
- 톤매핑 이후 LDR에 그리는 에디터 화살표는 해당 셰이더에서 선형 계산 결과를 sRGB로 인코딩한다.
