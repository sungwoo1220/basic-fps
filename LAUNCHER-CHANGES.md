# Launcher changes

The manual previous-version restore button, renderer bridge, and main-process handler have been removed. New installation pointers no longer retain a previous-version link. Failed downloads still preserve the currently installed working game. Existing launchers require reinstalling the new setup EXE to receive this launcher UI change; game-file updates cannot replace the launcher itself. Historical directories already on disk are not deleted automatically.

## 2026-11 정리
저장소의 압축 원본과 패치 스크립트를 없애고 게임/런처 소스를 일반 파일로 옮겼습니다. 같은 때 런처에서 게임 실행을 연타하면 창이 여러 개 뜨던 문제, 업데이트마다 이전 설치 폴더가 쌓이던 문제, 내 PC 서버 모드에서 주소 칸이 비면 시작되지 않던 문제를 고쳤습니다. 이 변경은 런처 자체에 들어 있으므로 **새 설치 파일을 다시 설치해야** 적용됩니다. 이미 쌓인 이전 설치 폴더는 새 런처가 첫 업데이트에 성공할 때 함께 정리합니다.
