# FBX 임포터 구현과 사용

입력 해석과 프로퍼티 매핑은 [FBX_IMPORT_POLICY.md](FBX_IMPORT_POLICY.md)를 따른다.
이 문서는 FBX 임포트의 빌드·사용 방법과 Parse/Cook·바이너리 저장·애셋 로딩 경로를 설명한다.

## 빌드

저장소 루트에서 `SlnGenerator.bat`을 실행한 뒤 에디터를 빌드한다.
원본 FBX 해석에는 Autodesk FBX SDK를 사용하며, 생성 스크립트가 SDK와 vcpkg 의존성을 준비한다.

| 항목 | 위치·동작 |
| --- | --- |
| FBX SDK | `Source/ThirdParty/FBX/2020.2/` |
| 다운로드 캐시 | `Intermediate/Dependencies/` |
| 준비 스크립트 | `Setup/SetupFbxSdk.ps1` |
| SDK 설치 | Autodesk 2020.2 VS2017 배포 파일 다운로드 → SHA256 검증 → 헤더·Windows x64 MD 라이브러리 배치 |
| 설치 오류 | 프로젝트 생성을 중단하고 오류 표시 |
| CI | `CI=1`로 마지막 키 입력 대기 생략 |

유효한 SDK는 재사용한다. SDK 폴더가 유효하지 않으면 안내된 경로를 확인하고 폴더를 옮긴 뒤 다시 실행한다.
SDK와 다운로드 캐시는 Git 제외 대상이다.

직접 설치한 SDK는 `FBX_SDK_ROOT`로 지정한다.

```powershell
$env:FBX_SDK_ROOT = 'SDK가 있는 폴더'
./Setup/premake5.exe --file=Setup/premake5.lua vs2026
```

해당 폴더에는 `include/fbxsdk.h`와 `lib/vs2017/x64/{debug,release}`의
`libfbxsdk-md.lib`, `libxml2-md.lib`, `zlib-md.lib`가 필요하다.
SDK 연결 대상은 Debug/Release의 원본 임포트 기능이다. Executable은 .fbxbin을 로딩한다.

## 임포트

1. `File > Import FBX...`를 연다.
2. `Source FBX`의 `Browse...`로 원본을 선택한다.
3. `Output folder`의 `Browse...`로 Assets 내부 출력 폴더를 선택한다. 경로를 직접 입력할 수도 있다.
   기본값은 `Assets/Imported/FBX파일명`이며 필요한 하위 폴더는 임포트 시 생성한다.
4. Static/Skeletal의 Bake Meshes와 Combine Meshes를 설정하고 `Import`를 누른다.
5. 컴포넌트의 메시 선택 목록에서 생성한 StaticMesh 또는 SkeletalMesh를 선택한다.

성공하면 임포트 창이 닫히고 결과가 등록된다. 오류는 임포트 창에, 경고는 Output Log에 표시한다.
`Cancel`과 우상단 X로 창을 닫는다.

### 경로와 출력

- 원본 FBX와 참조 텍스처는 읽을 수 있는 파일 경로에서 가져온다.
- 모든 .fbxbin과 임포트 임시 파일은 선택한 Assets 내부 출력 폴더에 저장한다.
- 엔진 시작 시 Assets의 .fbxbin과 그 참조를 로딩한다.
- 애셋 키는 Assets 기준 바이너리 상대 경로다.

| 접두사 | 애셋 | 저장 내용 |
| --- | --- | --- |
| SM | UStaticMesh | LOD 기하, 섹션·슬롯, 머티리얼 참조, 스태틱 LOD 생성용 이음매 정보 |
| SK | USkeletalMesh | LOD 기하·본 영향, 머티리얼·Skeleton 참조, 메시 RefSkeleton |
| SKEL | USkeleton | 본 이름·부모 계층·로컬 기준 자세 |
| M | UMaterial | 셰이딩 모델·선형 색상·광택·샘플러·블렌드 모드, 텍스처 참조 |
| T | UTexture2D | 이미지 속성과 최상위 밉 픽셀·압축 블록 |

출력 이름은 `접두사_소스파일명_출력이름.fbxbin`이다. 이름 충돌에는 출처 기반 해시를 붙인다.
같은 원본을 같은 출력 폴더에 재임포트하면 같은 키의 애셋을 교체한다.
출력 폴더를 바꾸면 새 키로 저장하며, 이번 출력 목록에 포함되지 않은 파일은 보관한다.

출처 식별자는 Assets 내부 원본의 상대 경로 또는 외부 원본의 정규화한 절대 경로다.
원본 이동은 출처 식별에 영향을 준다. 런타임 로딩은 바이너리 안의 데이터와 애셋 키를 사용한다.

## 코드 구성

| 코드 | 역할 |
| --- | --- |
| `Source/Runtime/Asset/FbxImporter/FbxImporter.*` | SDK Parse/Cook, 메시 분류·출력 묶음·좌표 변환·Combine·본 영향 정리 |
| `FbxAssetBake.*` | 타입·버전 헤더와 CPU 데이터 직렬화. 페이로드 버전 4 |
| `FbxAssetLoader.*` | 참조 해석, 엔진 객체·GPU 리소스 생성, 출력 교체와 애셋 등록 |
| `FbxPath.*` | UTF-8 경로, 원본 식별자, 출력 경계와 파일명 처리 |
| `Source/Runtime/Animation/ReferenceSkeleton.*`, `Skeleton.h` | 부모 우선 본 계층·로컬 기준 자세와 공유 Skeleton |
| `Source/Runtime/Render/SkeletalMesh.*` | 메시·LOD·본 영향·Skeleton 연결과 inverse bind |
| `Source/Editor/EditorUI/EditorUI.*` | 임포트 창과 원본·출력 폴더 탐색기 |

Cook은 UV 선택·변환과 원본 탄젠트 변환을 수행하고, 누락·비정상 탄젠트를 계산한다.
바이너리는 완성된 정점 데이터를 저장한다.
스태틱 메시 리소스 생성은 섹션 내 삼각형 정렬 → 버퍼/BVH → 렌더 상태 구성 순서다.
스켈레탈 메시의 각 LOD도 삼각형 정렬 후 BuildTriangleBVH로 기준 기하의 BVH를 만든다.

## 스켈레탈 메시 로딩

- `USkeletalMesh::MeshData`, `SkinWeights`는 LOD0이며 `AdditionalLODs`가 이후 단계를 담는다.
- 로더는 LOD별 정점과 `InfluenceBones`·`InfluenceWeights`를 같은 순서로 복사한다.
- 메시의 `RefSkeleton`과 참조한 `USkeleton`을 연결한다.
- `RefBasesInvMatrix`는 메시의 Reference Pose로부터 로딩 시 계산한다.

기하·LOD·좌표 변환의 상세 규칙은 [FBX_IMPORT_POLICY.md](FBX_IMPORT_POLICY.md)를 따른다.

## 머티리얼과 텍스처 연결

| 코드 | 처리 |
| --- | --- |
| FImport::Material | FBX 머티리얼을 FFbxMaterialData로 구성하고 텍스처 애셋 키를 연결한다. |
| FImport::Texture | ImageLoader로 이미지를 읽어 Texture2D 출력 데이터를 만든다. 내장 이미지는 SDK 추출 경로에서 읽는다. |
| FImport::OpacityTexture | 색상·불투명도 이미지를 합성하고 머티리얼별 Texture2D 출력을 만든다. |
| FImport::SurfaceUV | 렌더링에 사용하는 텍스처의 UV 요구를 메시의 머티리얼별로 판정한다. |
| FImport::Run | 최종 머티리얼에서 참조하는 텍스처만 출력 목록에 남기고 데이터를 검증한다. |
| FLoadBatch | Material·Texture2D 참조를 해석하고 UMaterial 인스턴스와 GPU 리소스를 구성한다. |

머티리얼 로딩은 CreateInstance(DefaultMaterial)로 셰이더·텍스처 슬롯·파라미터 버퍼를 준비한 뒤
FFbxMaterialData의 프로퍼티를 복사한다. Diffuse와 NormalMap은 텍스처 슬롯 0과 1을 사용한다.
UVTiling은 (1, 1), UVScrollSpeed와 WorldUVTileSize는 (0, 0)으로 설정한다.

## 교체와 오류

```text
Cook
→ 임시 바이너리 쓰기·재검사
→ 새 객체·리소스 생성
→ 등록표 준비
→ 파일 교체
→ 등록표 반영
```

실행 중 파일 교체가 실패하면 백업으로 복원한다. 복원이 실패하면 백업 경로를 오류에 표시하고 보관한다.
임시·백업 폴더는 출력 폴더 아래의 `.fbx-import-*`이며 정상 종료와 복구 완료 시 정리한다.

대체된 객체는 retired로 표시하고 기존 참조와 리소스를 엔진 종료까지 유지한다.
새 조회는 같은 키의 활성 객체를 사용한다.

헤더·버전 오류는 에디터 재임포트로 복구한다.
헤더와 데이터 직렬화는 FbxAssetBake.cpp에서 관리한다. SourcePath/SourceObject는 출력의 출처를 식별한다.

## Cook 처리 흐름

| 함수 | 역할 |
| --- | --- |
| Gather, FindRigs | 표시 메시와 LODGroup을 수집하고 스킨 연결·계층으로 리그를 구성한다. |
| PreparePoses, PoseMatrix, Bind, MeshBind | 노드별 바인드 행렬을 검증하고 선택·보완한다. |
| BuildRig | 부모 우선 본 계층과 로컬 기준 자세를 만든다. |
| AppendMesh | 좌표·UV·탄젠트를 변환하고 정점·인덱스·섹션·본 영향을 구성한다. |
| CookMesh, LODThresholds | 출력 묶음별 LOD 데이터와 전환 임계값을 만든다. |

## 자동 검증

저장소 루트에서 실행한다.

```powershell
./Setup/premake5.exe --file=Setup/premake5.lua --fbx-tests vs2026
# 생성된 FbxImporterTests 프로젝트를 Debug 구성으로 빌드
./Build/Bin/Debug-windows-x86_64/FbxImporterTests.exe
```

테스트는 SDK로 합성 FBX를 생성하고 D3D11 장치로 런타임 리소스를 검증한다.
테스트 폴더는 `Assets/.fbx-test-fixtures`와 `Build/.fbx-external-test-fixtures`다.
폴더가 이미 존재하면 검사를 중단한다. 정상 종료 시 생성한 폴더를 정리하고 실패 시 조사용으로 보관한다.

검증 항목은 다음과 같다.

- Bake/Combine, 계층·본 영향과 LOD 구성.
- UV 세트 선택·충돌·변환, Wrap 선택·충돌·축별 혼합, 원본 탄젠트 보존·반전·대체 계산.
- Diffuse 색상·계수, Shininess 변환, Specular·Emissive 상수 매핑과 바이너리 복원.
- Bump 경고와 NormalMap 선택.
- 상수·맵 투명도 판정, 알파·RGB 마스크 베이크, 공유 Diffuse 보존, 불투명도 전용 머티리얼, 크기 보간, GPU 알파 업로드와 블렌드 모드의 바이너리 복원.
- 외부 FBX, 한글 경로·상대 참조·내장 텍스처, 출력 폴더와 경로 경계.
- 파일명 충돌, 재임포트, retired 처리와 파일 교체 롤백.
- 바이너리 버전·잘림 검사와 메시·Skeleton·머티리얼·텍스처의 바이너리 의존 로딩.

## 참고 구현

- `Engine/Source/Runtime/Engine/Public/ReferenceSkeleton.h`.
- `Engine/Source/Runtime/Engine/Private/SkeletalMesh.cpp`: CalculateInvRefMatrices.
- `Engine/Plugins/Interchange/Runtime/Source/Parsers/Fbx/Private/FbxScene.cpp`: 장면·계층 해석.
- `Engine/Plugins/Interchange/Runtime/Source/Parsers/Fbx/Private/FbxMesh.cpp`: 기하·영향·바인드 정보.
- `Engine/Plugins/Interchange/Runtime/Source/Pipelines/Private/InterchangePipelineMeshesUtilities.cpp`: 분류·루트·메시 묶음.
- `Engine/Plugins/Interchange/Runtime/Source/Pipelines/Private/InterchangeLODDataParser.cpp`: LOD 구성.
- `Engine/Plugins/Interchange/Runtime/Source/Pipelines/Public/InterchangeGenericAssetsPipelineSharedSettings.h`.
- `Engine/Plugins/Interchange/Runtime/Source/Pipelines/Public/InterchangeGenericMeshPipeline.h`.
