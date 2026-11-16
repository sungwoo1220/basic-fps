[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GameExe,[string]$ServerUrl='http://127.0.0.1:3000')
$ErrorActionPreference='Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Windows의 로그인된 사용자 화면에서 실행해 주세요.' }
if (-not (Test-Path $GameExe)) { throw '제작한 BasicFPS.exe의 위치를 지정해 주세요.' }
$uri=$null
if (-not [Uri]::TryCreate($ServerUrl,[UriKind]::Absolute,[ref]$uri) -or $uri.Scheme -notin @('http','https') -or $uri.UserInfo -or $ServerUrl -match '[\r\n" ]') { throw '올바른 http/https 게임 서버 주소를 지정해 주세요.' }
Write-Host '자동 빌드 성공과 실제 화면 동작 확인은 달라요. 아래 순서대로 시험해 주세요.'
Write-Host '1. 창모드 → 전체화면 창모드 → 독점 전체화면. 15초 복구·유지·Alt+Tab·재실행 저장 확인.'
Write-Host '2. 같은 위치에서 MSAA 끔/2배/4배 각각 제한 300과 0을 비교. 성능 기록 버튼으로 CSV 저장.'
Write-Host '3. 다른 참가자와 이동·머리/몸통/다리 피해·12발 탄창·R 재장전·봇·점수판 확인.'
Write-Host '4. 독점 상태 확인과 자동 MUX 전환은 별개예요. Windows 그래픽 설정에는 새 게임의 실제 실행 파일을 선택해요.'
Start-Process -FilePath (Resolve-Path $GameExe).Path -WorkingDirectory (Split-Path (Resolve-Path $GameExe).Path) -ArgumentList ('-d3d11 -SaveToUserDir -log -ServerUrl="'+$ServerUrl+'"')
