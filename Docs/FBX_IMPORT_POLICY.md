# FBX 임포트 정책

FBX 입력의 해석, 출력 데이터, 충돌·미지원 처리와 재임포트 규칙을 정의한다.
사용과 코드 연결 방법은 [FBX_IMPORT_IMPLEMENTATION.md](FBX_IMPORT_IMPLEMENTATION.md)에 정리한다.

## 1. 지원 범위

- StaticMesh와 SkeletalMesh, 소스 LOD, 섹션과 머티리얼 슬롯.
- USkeleton, 머티리얼 프로퍼티, Diffuse·NormalMap 텍스처와 불투명도 베이크.
- 에디터 임포트와 .fbxbin 저장·로딩.

미지원 항목은 애니메이션 임포트, 스켈레탈 LOD 자동 생성, 리타기팅, VirtualBone,
Curve 메타데이터, LinkupCache, 충돌 형상·PhysicsAsset·모프 타깃·카메라·라이트·씬 배치 복원이다.

## 2. 임포트와 로딩

### 에디터 임포트

```text
원본·출력 폴더·옵션 선택
→ 원본 해석과 출력 데이터 검증
→ 출력 파일·애셋 교체
```

임포트 실행마다 원본을 읽어 새 결과를 만든다. 사용한 옵션과 소스 식별 정보를 바이너리에 저장한다.

### 엔진 시작

```text
Assets의 .fbxbin 스캔
→ 바이너리와 참조 애셋 읽기
→ 런타임 리소스 생성
→ 등록
```

- 런타임 로딩 입력은 .fbxbin과 그 안의 애셋 참조다.
- 참조는 조회 후 필요한 애셋을 로딩하는 방식으로 연결하며 동일 키의 객체를 재사용한다.
- 참조 누락·타입 불일치·순환과 형식·버전 오류는 로딩 오류로 처리한다.
- 바이너리 재생성은 에디터 임포트로 수행한다.

### 경로

- 원본 FBX는 파일 경로로 선택하고 참조 텍스처는 파일 참조를 따라 읽는다.
- 출력 폴더는 Assets 또는 그 하위 폴더다.
- 기본 출력 폴더는 `Assets/Imported/FBX파일명`이다. 필요한 하위 폴더는 임포트 시 생성한다.
- 메시·Skeleton·머티리얼·텍스처의 .fbxbin은 선택한 출력 폴더에 저장한다.
- 내장 텍스처 추출, 임시 바이너리와 교체 백업은 출력 폴더의 `.fbx-import-*` 폴더를 사용한다.
- 출력 경계는 정규화한 실제 경로와 디렉터리 경계로 검사한다.
- 원본 경로는 출처 식별과 재임포트 메타데이터로 사용한다.

## 3. 바이너리와 식별

### 파일과 데이터

- 확장자는 .fbxbin이며 파일 하나가 애셋 하나를 담는다.
- 헤더는 Magic, Version, AssetType을 포함한다.
- 로더는 현재 형식·버전의 바이너리를 읽는다.
- GPU 리소스는 저장된 CPU 데이터로 생성한다.

| 애셋 | 자체 데이터 | 참조 |
| --- | --- | --- |
| StaticMesh | LOD별 기하, 섹션·슬롯, 바운드, 전환 기준 | Material |
| SkeletalMesh | LOD별 기하·본 영향, 메시 RefSkeleton | Material, USkeleton |
| Skeleton | 본 이름·부모 계층·로컬 기준 자세 | — |
| Material | ShadingModel·색상·수치 프로퍼티 | Texture2D |
| Texture2D | 크기·포맷·픽셀 또는 압축 블록 | — |

메시는 슬롯 이름과 섹션의 슬롯 인덱스를 저장한다. 머티리얼 프로퍼티는 Material 파일에 둔다.
본별 inverse bind는 메시 기준 자세로부터 생성·로딩 시 계산한다.
텍스처의 저장 밉 범위와 런타임 밉 생성은 ImageLoader와 리소스 생성 규칙을 따른다.

### 애셋 키와 파일명

- .fbxbin의 애셋 키는 Assets 기준 바이너리 상대 경로다.
- AssetMap 조회와 씬 참조에 같은 문자열 키를 사용한다.
- 출력 이름은 다음 형식을 사용한다.

```text
[종류접두사]_[FBX파일명]_[출력원본이름].fbxbin
```

| 접두사 | 애셋 |
| --- | --- |
| SM | StaticMesh |
| SK | SkeletalMesh |
| SKEL | Skeleton |
| M | Material |
| T | Texture2D |

- 출력원본이름에는 노드·LODGroup·리그 루트·머티리얼·텍스처 이름을 사용한다.
- Static 전체 Combine 출력 이름은 Combined다.
- 파일명 금지 문자는 치환하고 이름 충돌에는 출처 식별자 기반의 안정적인 해시를 붙인다.
- 같은 출처·출력 폴더의 재임포트는 같은 애셋 키를 사용한다.
- Assets 내부 원본의 출처 식별자는 Assets 상대 경로, 외부 원본은 정규화한 절대 경로다.

## 4. 출력 묶음과 옵션

- Static Combine은 최종 Static 메시 인스턴스를 하나의 메시 애셋으로 합친다.
- Skeletal Combine은 같은 소스 리그 루트의 메시를 하나의 SkeletalMesh로 합친다.
- Skeletal Combine을 끄면 메시 애셋을 분리하고 같은 리그 루트의 USkeleton을 공유한다.
- 스켈레탈 계층의 스킨 없는 메시도 Skeletal로 처리하고 적절한 본에 가중치 1로 부착한다.
- USkeleton은 소스 리그 루트별로 생성한다.
- 가시성과 가시성 상속을 반영한 표시 메시를 임포트한다. 숨김·충돌 전용 메시는 제외한다.

기본 옵션은 다음과 같다.

| 옵션 | Static | Skeletal |
| --- | --- | --- |
| Bake Meshes | 켜짐 | 켜짐 |
| Combine | 꺼짐 | 켜짐 |

## 5. 좌표계와 Bake Meshes

- 엔진의 1.0은 1미터다. FBX GlobalSettings의 축·단위를 엔진 규약으로 변환한다.
- 노드 변환은 피벗·상속을 반영한 SDK 평가 API로 구한다.
- Static Bake 켜짐과 Skeletal 기하 변환에 GeometricTransform을 적용한다.
- 노멀·탄젠트와 반전 변환의 면 방향을 정점 변환에 맞춰 처리한다.
- 선택한 UV 세트의 유효한 원본 Tangent·Binormal을 가져온다. 기하 변환과 최종 UV 변환을 반영하고 직교화한 Tangent와 비탄젠트 부호를 FVertex에 저장한다.
- 원본 탄젠트 기저가 없거나 유효하지 않은 정점은 최종 기하·UV로 탄젠트를 계산한다. 유효한 원본 기저는 유지하고 잘못된 원본 기저는 경고한다.
- Cook에서 완성한 UV·탄젠트를 바이너리에 저장하고 런타임 정점 데이터로 사용한다.

| 대상 | Bake 켜짐 | Bake 꺼짐 |
| --- | --- | --- |
| Static | 메시 노드와 부모 계층의 변환을 기하에 적용 | 메시 로컬 기하 사용 |
| Skeletal | 외부 배치를 포함해 메시와 본 기준 자세 변환 | 루트 본 부모의 바인드 글로벌 변환을 제거하고 루트 본의 로컬 변환 유지 |

Static Bake를 끈 메시의 Combine은 각 메시의 로컬 기하를 합친다.
다음 식은 같은 축·단위 체계의 행 벡터 표기다.

```text
C = 루트 본 부모의 바인드 글로벌 변환
G = 메시 GeometricTransform
M = 메시 노드의 바인드 글로벌 변환
B = 본의 바인드 글로벌 변환

Skeletal Bake 켜짐:
    pModel    = pLocal * G * M
    bindModel = B

Skeletal Bake 꺼짐:
    pModel    = pLocal * G * M * inverse(C)
    bindModel = B * inverse(C)

inverseBind = inverse(bindModel)
```

- 바인드 글로벌 변환은 노드별 유효 BindPose → Cluster 바인드 행렬 → 0시점의 평가된 글로벌 변환 순으로 선택한다. 유효한 BindPose가 여러 개면 첫 값을 사용한다.
- 바인드 정보 충돌과 TRS 근사 오차는 경고하고 선택된 결과로 진행한다.
- NaN·Inf와 필요한 역행렬을 구할 수 없는 변환은 오류로 처리한다.

## 6. SkeletalMesh 기준 데이터

- USkeletalMesh는 USkeleton 참조와 메시 자신의 FReferenceSkeleton을 소유한다.
- USkeleton은 공유 골격 정의인 FReferenceSkeleton을 소유한다.
- FReferenceSkeleton은 본 이름·부모 인덱스·부모 상대 로컬 Reference Pose를 담는다.
- 본은 부모 우선 순서로 저장한다.
- 메시 기준 자세를 모델 공간으로 누적하고 그 역행렬을 RefBasesInvMatrix에 둔다.

### 정점과 본 영향

- 기하는 FVertex를 사용하고 본 영향은 최종 정점에 대응하는 배열에 저장한다.
- InfluenceBones는 메시 RefSkeleton 인덱스인 uint16 네 개로, 정점당 8바이트다.
- InfluenceWeights는 float 네 개로 저장한다.
- 최대 영향 본은 4개다. 초과 시 큰 가중치 네 개를 유지하고 정규화한다.
- 같은 본의 중복 영향은 최대 가중치를 사용하고, 무가중치 정점은 루트 본 가중치 1로 보완한다.
- 정점 분리·복제·재배열 시 영향 배열도 함께 대응시킨다.

### 스키닝 해석

- 스킨 가중치는 Linear Blend Skinning 기준으로 가져온다.
- DQ·Blend는 본·웨이트를 Linear로 가져오고 근사 처리를 경고한다.
- Additive는 정규화한 Linear 가중치로 근사하고 경고한다.

## 7. 머티리얼과 텍스처

### 프로퍼티 매핑

메시 .fbxbin은 Material .fbxbin을 참조하고, Material은 다음 프로퍼티와 Texture2D 참조를 저장한다.
상수 RGB는 선형 값으로 해석한다.

| FBX 입력 | 출력 프로퍼티 | 임포트 규칙 |
| --- | --- | --- |
| Lambert / Phong | ShadingModel | Lambert / BlinnPhong으로 매핑한다. 다른 모델은 경고 후 BlinnPhong 기본값을 사용한다. |
| Diffuse, DiffuseFactor | DiffuseOrFactor.RGB | Diffuse 맵을 사용하면 각 채널에 DiffuseFactor를 저장하고, 맵이 없으면 Diffuse × DiffuseFactor를 저장한다. |
| Phong Shininess | Shininess | 음수를 0으로 제한하고 4를 곱한다. |
| Phong Specular, SpecularFactor | SpecularColor, SpecularFactor | 색상과 계수를 각각 저장한다. Lambert의 SpecularFactor는 0이다. |
| Lambert/Phong Emissive, EmissiveFactor | EmissiveColor, EmissiveFactor | 색상과 계수를 각각 저장한다. |
| TransparencyFactor | DiffuseOrFactor.W | 투명도 맵을 베이크하지 않은 경우 clamp(1 − TransparencyFactor, 0, 1)을 저장한다. |
| TransparentColor의 파일 텍스처 | DiffuseOrFactor.W, BlendState | 불투명도 베이크가 성공하면 W는 1, BlendState는 AlphaBlend다. |

- Specular·SpecularFactor·Emissive·EmissiveFactor에 연결된 텍스처는 경고하고 상수 값을 사용한다.
- 투명도 맵을 베이크하지 않은 경우 W가 1보다 작으면 AlphaBlend, 1이면 Opaque다.
- BlendState와 SamplerState는 Material 바이너리에 저장한다.
- 비유한 색상·수치 값과 음수 SpecularFactor·EmissiveFactor는 오류로 처리한다.

### 투명도 텍스처

- `TransparentColor`에 직접 연결된 첫 파일 텍스처를 불투명도 입력으로 가져온다.
- 연결된 맵은 머티리얼별 색상 텍스처의 A 채널에 베이크한다. Diffuse RGB는 유지하고, Diffuse가 없으면 흰 RGB를 사용한다.
- 베이크한 A는 투명도 맵에서 구한 불투명도다. DiffuseOrFactor.W는 1이다.
- 텍스처 AlphaSource가 알파 채널 또는 RGB 밝기를 지정하면 그 출처를 사용한다. 출처 지정이 없으면 255 미만의 알파가 있는 이미지는 A를, 나머지는 RGB 밝기를 사용한다.
- 불투명도는 검정 0, 흰색 1인 수치 데이터다. RGB 밝기는 `.2126R + .7152G + .0722B`이며 sRGB 디코딩하지 않는다. RGB에 알파를 미리 곱하지 않는다.
- 출력 크기는 Diffuse 크기이며, Diffuse가 없으면 투명도 맵 크기다. 크기가 다른 맵은 정규화 좌표에서 선형 보간한다.
- 알파 합성 입력은 RGBA8·BGRA8·R8·A8이다. 합성할 수 없는 포맷은 경고 후 투명도 맵을 제외하고 원본 Diffuse와 상수 불투명도를 사용한다. 제외된 맵은 UV 요구 판정에서도 제외한다.
- 결과는 머티리얼별 Texture2D 애셋으로 저장한다. 투명도 맵이 없는 머티리얼은 소스 Diffuse 애셋을 공유한다. 최종 머티리얼이 참조하는 텍스처를 출력한다.

### 텍스처

- Diffuse와 NormalMap 텍스처를 연결한다. NormalMap은 선형 데이터로 샘플링해 탄젠트 공간 노멀로 해석한다.
- NormalMap 없이 Bump 맵만 연결되어 있으면 경고하고 노멀 입력을 비운다. 둘 다 없으면 경고 없이 기본 표면 노멀을 사용한다.
- 파일은 FBX의 절대 참조, 원본 폴더 기준 상대 참조와 파일명 후보에서 찾는다.
- 필요한 파일을 찾거나 읽는 데 실패하면 임포트 오류로 처리한다.
- 내장 이미지는 SDK로 임시 추출하고 픽셀·압축 블록을 Texture2D 바이너리에 저장한다.
- 이미지 형식은 ImageLoader의 지원 범위를 따른다.

### UV와 샘플러

- 각 메시의 머티리얼별로 Diffuse·Normal·투명도 텍스처의 UV 요구를 판정한다.
- 같은 UV 세트를 사용하면 그 세트의 좌표를 FVertex.UV에 저장한다.
- 서로 다른 세트를 사용하면 경고하고 Diffuse → Normal → 투명도 맵 → 첫 UV 세트 순서로 선택하며 텍스처 변환을 모두 무시한다.
- 지정한 세트가 없으면 경고하고 같은 우선순위로 존재하는 세트를 선택하며 텍스처 변환을 무시한다. 빈 이름과 `default`는 기본 세트를 가리킨다.
- 같은 세트·같은 변환을 사용하면 배율·이동·회전·피벗·UV 교환을 정점 UV에 적용한 뒤 V를 `1-V`로 변환한다.
- 같은 세트에서 텍스처 변환이 다르면 경고하고 변환을 모두 무시한다. 변환 행렬 비교에는 부동소수점 허용 오차를 둔다.
- 머티리얼별 UV 요구가 다른 공유 정점은 분리하고 본 영향도 복제한다.
- Wrap은 텍스처 객체의 U·V 설정에서 읽어 Material.SamplerState에 저장한다. Repeat/Repeat는 LinearWrap, Clamp/Clamp는 LinearClamp다.
- 연결된 텍스처의 Wrap이 다르면 경고하고 Diffuse → Normal → 투명도 맵 순서로 선택한다. 알파 리샘플링에도 선택한 Wrap을 사용한다.
- 선택한 텍스처의 U·V Wrap이 다르면 미지원 경고 후 LinearClamp를 사용한다. 연결된 다른 텍스처의 축별 Wrap 혼합도 경고한다.

## 8. LOD

LODGroup은 메시 애셋 하나로 저장하며, 각 단계의 기하·본 영향과 메시 RefSkeleton을 구성한다.

- 소스 LOD0~3을 유지한다. LOD3이 마지막 전환 범위를 담당한다.
- 소스 LOD 구성이 없으면 LOD0으로 가져온다.
- Combine의 각 LOD에는 해당 단계에 존재하는 메시를 합친다.

```text
화면 크기 임계값 = 최종 바운딩 구 반지름 × 기준 투영 배율 / 전환 거리
```

- 거리 기준은 수직 FOV 60도, 원본 거리 단위와 최종 바운드를 반영해 변환한다.
- 백분율 기준은 100으로 나눈 값을 사용한다.
- 그룹별 기준이 충돌하면 세밀한 단계를 더 오래 유지하는 임계값을 사용한다.
- 전환 기준이 비어 있거나 유효하지 않으면 0.30 / 0.10 / 0.03을 사용한다.
- 가시성은 엔진의 가시성 정책을 사용하며, 소스 Min/MaxDistance 설정은 경고한다.

## 9. 교체·실패·Retired

### 파일 반영

- FBX 한 파일의 임포트를 하나의 작업 단위로 처리한다.
- 데이터·임시 파일·객체·리소스를 검증한 뒤 파일을 교체하고 새 애셋을 활성 등록한다.
- 실행 중 파일 교체 오류는 백업·롤백으로 복구한다.
- 복원이 실패하면 복구용 백업 경로를 오류에 표시하고 백업을 보관한다.
- 재임포트는 같은 키의 출력을 새 결과로 교체한다.
- 이번 출력 목록에 포함되지 않은 파일은 보관하며 다음 시작에도 로딩 대상이다.

### 객체 수명과 조회

- 새 결과는 새 객체로 생성한다. 대체된 동일 키의 객체는 retired로 표시한다.
- AssetMap과 새 조회·선택은 활성 버전을 사용한다.
- 기존 컴포넌트·프록시의 포인터와 의존 객체는 종료까지 유지한다.
- retire 대상은 이번 등록으로 대체된 애셋이다.
- retired 상태는 세션 동안 유지하고, 객체의 경로도 유지한다.
- 객체는 UObject 전역 등록으로 관리하며 종료 시 일괄 파괴한다.
- 씬의 경로 참조를 복원할 때는 같은 키의 활성 버전을 사용한다.
