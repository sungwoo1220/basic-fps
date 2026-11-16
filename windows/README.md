# Windows 빌드 PC 연결과 시험

## 현재 상태

언리얼 5.6 시험판의 소스와 자동 빌드 절차를 준비했어요. **연결된 Windows PC에서 아직 컴파일하지 않았고, 실행 파일도 아직 제공하지 않아요.** 아래 최초 준비는 해당 PC에서 한 번 필요해요. 이곳에서 연결되지 않은 PC에 접속하거나 대신 설치할 수는 없어요.

기존 브라우저·휴대폰 게임은 그대로예요. 새 런처를 설치해도 기본 선택은 기존 게임이고, 언리얼 시험판이 없으면 없다고 안내해요.

## 1. 전용 Windows PC 준비

개인 자료와 불필요한 로그인 정보가 없는 **전용 PC 또는 격리된 x64 Windows 계정/환경**을 사용해 주세요. 이 저장소의 main 코드를 그 계정 권한으로 실행하게 돼요. 공개 저장소에 self-hosted runner를 연결하는 것은 위험을 수반하며, PR 실행을 차단해도 main에 올라온 악성 코드까지 막아주지는 않아요. 이 PC에 배포용 토큰을 따로 저장하지 마세요.

- Epic Games Launcher에서 **Unreal Engine 5.6**을 설치해 주세요. Epic 계정 로그인·약관 동의는 PC 소유자가 진행해요. 다른 엔진 버전으로 자동 전환하지 않아요.
- Visual Studio 2022 **17.8 이상(17.14 권장)**과 C++ 데스크톱/게임 개발 도구, **MSVC 14.38.33130 이상**, **Windows SDK 10.0.19041.0 이상(10.0.22621.0 이상 권장)**을 설치해 주세요. 이 버전 기준은 아래 Epic 문서 기준이에요.
- Git과 Node.js 22 이상이 필요해요. 자동 빌드에서는 Node.js를 준비해 주지만 수동 사전 점검에는 설치돼 있어야 해요.
- 이 절차의 자체 안전 기준은 메모리 **16GB 이상**, 작업 드라이브 여유 공간 **40GB 이상**이에요. 엔진 설치 공간은 별도이며, 메모리는 32GB 이상을 권장해요. 이 숫자는 언리얼의 공식 최소 사양을 뜻하지 않아요.
- 저장소를 작업 폴더에 내려받고 64비트 PowerShell에서 다음을 실행해 주세요. 실행 정책이 막으면 조직/PC 정책을 확인하세요. 보안 설정을 영구 해제하는 절차는 제공하지 않아요.

```powershell
.\windows\Prepare-BuildPC.ps1 -OpenRunnerSetup
# 다른 위치에 엔진을 설치했다면:
.\windows\Prepare-BuildPC.ps1 -EngineRoot 'D:\Epic Games\UE_5.6' -OpenRunnerSetup
```

점검은 설치를 하지 않고 빠진 항목을 안내해요. 결과는 `%LOCALAPPDATA%\BasicFPSBuild\preflight.json`에 남아요. 엔진을 기본 경로가 아닌 곳에 설치했다면 runner 계정의 `UE_ROOT` 환경 변수에 해당 엔진 폴더를 지정하고 runner를 다시 실행해 주세요.

## 2. GitHub에서 PC 한 번 연결

1. [이 저장소의 새 runner 등록](https://github.com/sungwoo1220/basic-fps/settings/actions/runners/new)을 열어요.
2. Windows / x64를 선택하고, 그 페이지의 최신 공식 명령으로 **저장소 바깥 별도 폴더**에 runner를 설치·등록해요. GitHub가 보여 주는 등록 정보는 이 대화나 저장소에 보내지 마세요.
3. runner에 **`basic-fps-unreal`** 레이블을 추가해요. 기본 레이블 `self-hosted`, `Windows`, `X64`도 유지해요.
4. runner를 실행하고 GitHub에 **Idle/Online**으로 표시되는지 확인해요. 서비스로 실행해도 빌드와 화면 없는 통신 시험은 가능하지만, 실제 그래픽 시험은 로그인된 사용자 화면에서 따로 해요.
5. Actions에서 **Unreal Windows trial (dedicated PC)** → **Run workflow** → **main**으로 첫 빌드를 실행해요. 연결 전에 대기하던 작업이 취소됐다면 이 방법으로 다시 실행해요.

자동 실행은 이 저장소의 main 푸시와 main 수동 실행만 허용해요. PR·포크·`pull_request_target`은 이 PC를 사용하지 않아요. 일반 PR 검사는 기존 GitHub 제공 PC에서만 해요. 빌드 PC에는 읽기 권한만 전달하며, 릴리스 게시는 별도 GitHub 제공 PC에서 해요. 2단계 인증과 main 변경 권한 보호를 권장해요.

## 3. 연결 후 자동으로 하는 일

1. 기존 게임·런처·업데이트 검사를 실행해요.
2. 언리얼 에디터용 C++을 컴파일하고, 코드로 작은 맵과 단색 머티리얼을 만들어요. 에셋을 소스에 넣지 않아요.
3. 이동 규칙 일치와 MSAA/FPS 설정 자동 시험을 실행해요. 시험 결과가 없거나 필수 검사가 빠져도 실패로 처리해요.
4. Windows Development 패키지를 만들고, **실제 제작한 클라이언트**로 임시 방에 접속해 이동 응답·사격 탄약 감소·수동 재장전을 화면 없이 시험해요.
5. 모두 성공해야 8MB 이하 조각과 SHA-256 목록을 만들고 `ue-test-N-attempt` **prerelease**로 게시해요. 기존 `update.json`과 안정 채널은 바꾸지 않아요.

PC가 꺼져 있으면 대기하고, 컴파일이나 시험이 실패하면 시험판을 게시하지 않아요. 결과에 `InteractiveGraphicsVerified=false`가 남는 것이 정상이에요. 빌드 성공은 실제 GPU 시험 성공이 아니에요.

빌드 결과는 runner 임시 폴더에, 진단 기록은 Actions의 `unreal-build-diagnostics`에 남아요. `content.log`, `automation.log`, `native-smoke.log`, `build-result.json`으로 어느 단계가 실패했는지 구분해요. 진단 묶음은 7일, 큰 시험판 중간 묶음은 3일 보관해요. PC 경로가 포함될 수 있어 공개 저장소에 올리는 기록에서 개인 정보가 없는지 확인해 주세요.

### 자동 연결 없이 수동으로 빌드하기

```powershell
$runtime=Join-Path $env:LOCALAPPDATA 'BasicFPSBuild\js-runtime'
npm.cmd install --prefix $runtime --no-audit --no-fund --no-save ws@8.18.3 three@0.170.0
$env:NODE_PATH=Join-Path $runtime 'node_modules'
.\windows\Build-Unreal.ps1 -EngineRoot 'D:\Epic Games\UE_5.6'
```

기본 결과 위치는 `%LOCALAPPDATA%\BasicFPSBuild\runs\날짜-시간`이에요. 이전 폴더를 덮어쓰지 않아요. 에디터 컴파일·자동 시험까지만 확인하려면 `-SkipPackage`를 붙여요. 이 경우 실행 파일과 통신 시험 결과는 만들지 않아요.

## 4. 런처에서 시험판 사용

이번 변경의 **새 `Basic-FPS-Launcher-Setup.exe`를 다시 설치**해야 시험판 선택이 나타나요. 기존 런처는 자동으로 바뀌지 않아요.

- 새 런처의 ‘언리얼 시험판 다운로드 / 업데이트’를 눌러요. Windows 빌드가 성공하기 전에는 다운로드할 시험판이 없어요.
- ‘언리얼 Windows 시험판’을 선택하고 방 열기 또는 친구 주소 접속으로 실행해요. 방을 직접 열려면 기존 게임 파일도 준비돼 있어야 해요.
- 다운로드 목록·각 조각·전체 파일을 확인하고, 실행 전에도 설치 파일 전체를 다시 확인해요. 실패한 다운로드는 현재 설치를 바꾸지 않아요. 이전 설치 하나를 남기고, 새 게임이 시작 직후 비정상 종료되면 이전 파일까지 다시 검증한 뒤 복구해요. 복구 후 실행 버튼을 다시 눌러야 해요.
- 최초 실행에 필수 구성 요소 오류가 나면 내려받은 패키지의 `Engine\Extras\Redist\en-us\UEPrereqSetup_x64.exe`를 출처 확인 후 직접 실행해 주세요. 런처는 권한 상승이나 설치를 몰래 하지 않아요. 기본 내려받기 위치는 `%APPDATA%\basic-fps-launcher\unreal` 아래 현재 `ue-...` 폴더예요.
- 설정은 해당 PC의 언리얼 `GameUserSettings.ini`에, 성능 CSV·화면 저장은 게임의 `Saved\Diagnostics`에 남아요. 런처의 설치 경로가 바뀌어도 사용자 설정은 유지하도록 언리얼 사용자 저장 위치를 사용해요. 실제 저장 위치는 Windows 실행 후 확인해야 해요.

## 5. 반드시 로그인된 화면에서 할 시험

```powershell
.\windows\Test-Graphics.ps1 -GameExe 'C:\빌드결과\Package\Windows\BasicFPS\Binaries\Win64\BasicFPS.exe'
```

이 도구는 게임을 실행하고 순서를 안내할 뿐, 화면을 자동 합격 처리하지 않아요. 방은 런처나 `npm run game`으로 먼저 열어 주세요.

| 구분 | 확인할 것 |
| --- | --- |
| 화면 모드 | 창모드 / 전체화면 창모드 / 독점 전체화면을 각각 적용해요. 독점은 DXGI 조회 결과까지 확인해요. 15초 미확인, 직접 복구, 전환 중 포커스 이탈 시 원래 모드·창 크기 복구를 확인해요. |
| 복귀·저장 | Alt+Tab 후 돌아와 계속하기, 종료 후 재실행, 다중 모니터의 원래 창 위치를 확인해요. 화면 설정이 보이지 않으면 실행 인수 `-windowed -ResX=1280 -ResY=800`으로 복구해요. |
| 그래픽 | 같은 위치·봇 수에서 MSAA 끔 / 2배 / 4배, 제한 300 / 0을 각각 비교해요. 렌더 배율은 100% 고정이고 FXAA/TAA/TSR로 바꾸지 않아요. |
| 측정 | 메뉴의 성능 기록 시작 → 계속하기 → 20초 이상 플레이 → 메뉴에서 저장을 반복해요. 메뉴 구간은 최대 30FPS라 플레이 구간과 섞어 비교하지 마세요. CPU/GPU 시간과 GPU 이름도 확인해요. GPU 값이 없으면 측정값 없음으로 남겨요. |
| 멀티플레이 | 기존 브라우저 참가자와 새 참가자를 같은 방에 넣어 이동·피해·12발 탄창·재장전·봇·Tab 점수판을 확인해요. 머리/몸통/다리 판정은 기존 방이 결정해요. |
| GPU·노트북 | Windows 그래픽 설정에 런처가 아닌 **새 실제 게임 실행 파일**을 지정해요. 독점 상태, 외장 GPU 선택, 실제 표시 프레임 수, 자동 MUX 전환을 각각 따로 기록해요. 높은 FPS나 MUX 전환을 보장하지 않아요. |

## 참고 문서
- [Epic: UE 5.6 Visual Studio 호환 버전과 개발 도구](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.6)
- [GitHub: self-hosted runner 개념과 관리 책임](https://docs.github.com/en/actions/concepts/runners/self-hosted-runners)
