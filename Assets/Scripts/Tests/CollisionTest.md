# Lua 충돌 이벤트 테스트

`CollisionTest.lua`는 테스트 원본입니다. 엔진은 임의의 `.lua` 파일을 자동 실행하지 않으므로,
각 액터의 ScriptComponent에서 **Edit Script**로 연 `<Scene>_<Actor>.lua`에 내용을 복사하세요.
기존 테스트 스크립트를 보존하려면 새 씬과 새 액터로 진행하세요.

## 1. Overlap 시작/종료

1. 델리게이트 연결 변경이 포함된 에디터를 빌드하여 실행합니다.
2. 새 씬을 `CollisionTest.scene`으로 **먼저 저장**합니다. 이후 액터 이름을 확정한 다음 스크립트를 만듭니다.
   씬이나 액터 이름이 바뀌면 연결되는 스크립트 파일 이름도 달라집니다.
3. 액터 생성 목록에서 **ABoxActor 두 개**를 추가합니다. 각 액터에는 루트 `CollisionComponent`가 있습니다.
   StaticMeshActor의 화면에 보이는 메시만으로는 Shape 충돌이 생기지 않습니다.
4. 아래 설정을 적용합니다. 컴포넌트는 PIE 시작 전에 모두 추가하세요.

| 설정 | 이동 액터 | 고정 액터 |
| --- | --- | --- |
| 위치 | `(-3, 0, 0)` | `(0, 0, 0)` |
| 회전 | `(0, 0, 0)` | `(0, 0, 0)` |
| 스케일 | `(1, 1, 1)` | `(1, 1, 1)` |
| CollisionComponent의 BoxExtent | `(0.5, 0.5, 0.5)` | `(0.5, 0.5, 0.5)` |
| bGenerateOverlapEvents | `true` | `true` |
| bBlockComponent | `false` | `false` |
| bDrawOnlyIfSelected | `false` | `false` |
| 추가 컴포넌트 | `UScriptComponent` | `UScriptComponent` |
| 복사한 Lua 파일의 MODE | `"overlap"` | `"observer"` |

5. 두 ScriptComponent 각각에서 **Edit Script**를 눌러 생성된 파일에 `CollisionTest.lua` 내용을 복사합니다.
   고정 액터 파일의 `local MODE = "overlap"`만 `local MODE = "observer"`로 바꿉니다.
6. 씬을 저장하고 **Play/PIE**를 실행합니다. Output Log에서 `[CollisionTest]`를 찾습니다.

이동 액터는 시작 후 1초 대기하고 X=-3에서 X=3까지 초당 2만큼 이동합니다.
끝에서 1초 기다린 뒤 되돌아오며, PIE 종료까지 반복합니다.
기본 박스 크기에서는 시작 약 2초 후 `OVERLAP_BEGIN`, 약 3초 후 `OVERLAP_END`가 두 액터에서 출력됩니다.
8초 후 이동 액터에 `[PASS] Overlap Begin/End pair received`가 나오면 정상입니다.
`COUNTS`는 4초마다 한 번만 출력됩니다. Overlap은 겹쳐 있는 매 프레임이 아니라 진입/이탈 시 호출됩니다.

예상 로그의 핵심 부분:

```text
[CollisionTest] <이동 액터> v1 BEGIN_PLAY mode= overlap ...
[CollisionTest] <고정 액터> v1 BEGIN_PLAY mode= observer ...
[CollisionTest] <이동 액터> v1 OVERLAP_BEGIN count= 1 other= <고정 액터>
[CollisionTest] <고정 액터> v1 OVERLAP_BEGIN count= 1 other= <이동 액터>
[CollisionTest] <이동 액터> v1 OVERLAP_END count= 1 other= <고정 액터>
[CollisionTest] <이동 액터> v1 [PASS] Overlap Begin/End pair received
```

양쪽 액터 로그의 상대적인 순서는 달라도 됩니다. 이 테스트에서는 `hit=0`이 정상입니다.

## 2. Hit

현재 Lua의 `SetActorLocation`은 Sweep 이동이 아니므로 위 왕복 스크립트만으로 Hit는 발생하지 않습니다.
기존 `UProjectileMovementComponent`를 사용해 Hit 발생 경로를 확인합니다.

1. PIE를 멈춥니다. 액터 위치를 이동 `(-3,0,0)`, 고정 `(0,0,0)`으로 맞춥니다.
2. 이동 액터의 Lua 파일을 `MODE = "hit"`로 바꿉니다. 이 모드에서는 Lua가 위치를 변경하지 않습니다.
3. 양쪽 CollisionComponent의 `bBlockComponent = true`, `bGenerateOverlapEvents = false`로 바꿉니다.
4. 이동 액터에 **UProjectileMovementComponent**를 추가하고 다음 값을 설정합니다.

| 설정 | 값 |
| --- | --- |
| Velocity | `(2, 0, 0)` |
| InitialSpeed | `2` |
| MaxSpeed | `2` |
| ProjectileGravityScale | `0` |
| bSweepCollision | `true` |
| bSimulationEnabled | `true` |
| bShouldBounce | `false` |
| bRotationFollowsVelocity | `false` |
| bIsHomingProjectile | `false` |
| bConstrainToPlane | `false` |

5. 씬을 저장하고 Play를 실행합니다. 약 1초 후 양쪽 Lua에서 `HIT` 로그와 충돌 위치/법선이 출력되어야 합니다.
   이동 액터에서는 8초 후 `[PASS] Blocking Hit received`가 나와야 합니다.

**이동 액터는 Shape가 루트인 ABoxActor를 사용해야 합니다.**
MovementComponent는 액터의 루트를 움직이므로 StaticMeshActor에 BoxComponent를 자식으로 추가하기만 하면
Sweep 충돌이 적용되지 않습니다.
Hit 횟수는 이동 처리에 따라 여러 번 증가할 수 있습니다. 처음 3회만 상세 로그를 출력하도록 했습니다.
이 테스트에서는 `begin=0`, `end=0`이 정상입니다.
Hit 테스트를 마친 뒤 Overlap으로 돌아가려면 ProjectileMovement를 제거하거나 `bSimulationEnabled=false`로 바꾸세요.

## 3. Hot Reload와 종료

- Overlap PIE 실행 중 **실제로 연결된 이동 액터 파일**의 `LOG_TAG = "v1"`을 `"v2"`로 바꾸고 저장합니다.
  약 0.25초 내외에 `v2 RELOADED`가 출력되고, 이후 충돌 로그도 v2로 나와야 합니다.
  테스트 재현을 위해 Reload 시 카운터/진입 상태를 초기화하고 이동 액터를 X=-3으로 되돌립니다.
  초기화 직후 End 이벤트가 먼저 보일 수 있으므로 이후 새 진입/이탈 쌍을 확인하세요.
- PIE를 종료하면 각 액터의 `END_PLAY`와 누적 횟수가 출력됩니다.
- 다시 Play하면 카운터가 0부터 시작합니다. Shape가 한 개씩인 경우 한 번의 진입에서 각 액터의 BEGIN 카운터는
  하나씩 증가해야 합니다.

## 로그가 나오지 않을 때

| 증상 | 확인할 것 |
| --- | --- |
| BEGIN_PLAY 자체가 없음 | 새로 빌드한 에디터인지, ScriptComponent가 있는지, Edit Script가 가리키는 파일을 편집했는지 |
| BEGIN_PLAY는 있지만 OVERLAP이 없음 | 양쪽 Shape의 bGenerateOverlapEvents, 위치/크기/스케일, Play 상태인지 |
| Hit만 없음 | MODE=hit, Shape 루트, 양쪽 Block, 이동 액터의 ProjectileMovement/Sweep/속도/중력 |
| 로그가 예상보다 많이 발생 | 액터당 Shape/ScriptComponent가 여러 개인지, 다른 충돌 액터가 테스트 위치에 있는지 |
| 에디터에서는 상자가 보이지만 이벤트가 없음 | 에디터 시각화와 PIE의 이벤트 판정은 별개이므로 Play 중인지 확인 |
| nil 상대 액터 | 다른 콜백에서 상대가 파괴된 경우 가능한 값이며 테스트 스크립트는 이를 안전하게 출력함 |
