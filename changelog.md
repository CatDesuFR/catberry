# v1.0.1

- Finds cheats hiding deep in AppData. The file scan went only 2 folders into AppData, where executors usually sit (`AppData\Local\<name>\...`, INetCache and so on); it now goes much deeper there, and no longer skips every folder named "Windows" (only the real system one), which had hidden `AppData\Local\Microsoft\Windows\INetCache`.
- Catches a renamed Roblox executor. On a Roblox scan it reads programs in your own folders for the functions only an executor has (`getgenv`, `hookfunction`, `hookmetamethod`...) and flags one that has several, whatever it is named. Tested on a real executor renamed and buried deep: caught. The function names are kept scrambled inside catberry scan so it doesn't flag itself.

# v1.0.0

- First release: PC check for Roblox, Counter-Strike 2 and Easy Anti-Cheat games, with signed results files.
