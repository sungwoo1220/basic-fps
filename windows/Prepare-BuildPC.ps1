[CmdletBinding()]
param([string]$EngineRoot=$env:UE_ROOT,[switch]$OpenRunnerSetup)
$ErrorActionPreference='Stop'
Write-Host 'Basic FPS Windows 노트북/PC 빌드 준비'
Write-Host '직접 빌드할 때는 GitHub runner 연결이 필요 없어요. 엔진·개발 도구를 자동으로 설치하지 않아요.'
& (Join-Path $PSScriptRoot 'Test-BuildPC.ps1') -EngineRoot $EngineRoot
if ($OpenRunnerSetup) {
    Write-Warning '자동 빌드 연결은 개인 자료가 없는 전용 계정/격리 환경에서만 진행해 주세요. main 코드가 해당 계정 권한으로 실행돼요.'
    Start-Process 'https://github.com/sungwoo1220/basic-fps/settings/actions/runners/new'
    Write-Host 'GitHub 공식 안내에 따라 별도 폴더에 Windows x64 runner를 등록하고 basic-fps-unreal 레이블을 추가해 주세요.'
    Write-Host '등록 정보나 토큰을 채팅·소스 파일에 넣지 마세요. 실제 화면 시험은 runner 서비스가 아니라 사용자 데스크톱에서 진행해요.'
} else {
    Write-Host '로컬 빌드는 Build-Unreal.ps1로 실행할 수 있어요. 준비·실행·화면 시험 순서는 windows/README.md를 확인해 주세요.'
}
