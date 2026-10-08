# Literal commits do not start candidate UI

The prior commit path called `_StartComposition` even when a response contained only committed text. `_StartComposition` unconditionally called `CCandidateList::StartUI`, creating a popup and running initial skin/font/layout work before ending the text transaction. Empty-paint suppression prevented visibility but did not remove this work.

The text-only `_StartComposition` call now explicitly passes `candidateUI=FALSE`. The actual composing branch passes TRUE. The text-only start neither registers candidate UI nor requests its position. Idle layout callbacks also skip candidate extent requests. CandidateList consumes empty responses without publishing a drawing update; UI::Update caches idle notification state without Refresh. Notifications with no text and no explicit mode event do not schedule ShowWithTimeout.

Normal pinyin, mixed keypad/pinyin selection, and explicit CN/EN notifications retain their UI paths. Empty-window paint guards remain defensive, not the normal numeric-commit path. The running service and its caches still have normal memory usage; no CPU/memory benchmark or all-host runtime verification is claimed.

Validation: x64/Win32 Release builds; 237 SSF checks; 32 Lua behavioral assertions. Installed and reboot-staged files matched the release manifest. System32 TSF replacement requires Windows restart before live acceptance.