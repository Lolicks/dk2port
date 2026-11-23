# Источники и ссылки

## 1. Локальные копии (в `port/research/`)

| Репозиторий | Коммит/ветка | URL | Лицензия |
|---|---|---|---|
| OpenKeeper | HEAD `e2bfe95d…` (depth 1) | https://github.com/tonihele/OpenKeeper | GPLv3 (см. `LICENSE`) |
| dk2-tools | HEAD (depth 1) | https://github.com/ufdada/dk2-tools | см. `LICENSE`/README репозитория |

Ключевые файлы OpenKeeper:
- `src/main/java/toniarts/openkeeper/tools/convert/wad/WadFile.java` — `decompressFileData` (строки ~286–360)
- `src/main/java/toniarts/openkeeper/tools/convert/kmf/KmfFile.java` — парсер KMF
- `src/main/java/toniarts/openkeeper/tools/convert/KmfModelLoader.java` — модель → jME3
- `src/main/java/toniarts/openkeeper/tools/convert/textures/Dk2TextureDecoder.java`
- `src/main/java/toniarts/openkeeper/tools/convert/textures/enginetextures/EngineTextureDecoder.java`
- `src/main/java/toniarts/openkeeper/video/tgq/TgqFile.java`, `TgqFrame.java`, `EAAudioFrame.java`, `TgqPlayer.java`

Ключевые файлы dk2-tools:
- `Formats/Model/kmf_struct.bt` (автор ufdada, original code by Hoenir)
- `Formats/Archives/wad_struct.bt`
- `Scripts/wad_extract.1sc` — `decompressFileData`
- `Formats/Level/*.bt`, `Formats/Sound/*.bt`, `Formats/Archives/*.bt`

## 2. Оригинальная игра (локально)

`/home/kali/Desktop/rea/Dungeon Keeper 2` — установка GOG:
- `DKII.EXE` (SHA256 `e99818fbe9ad337ab4a24fa76901d5fe5a8c0a0dd306172d24b9547762c534cc`)
- `Data/Meshes.WAD`, `Data/EngineTextures.WAD`, `Data/FrontEnd.WAD`, `Data/Paths.WAD`, `Data/Sprite.WAD`
- `Data/Movies/*.tgq` (24 файла), `Data/Sound/Sfx/Global/*.sdt`
- `manual.pdf`, `reference_card.pdf`

## 3. Наши артефакты

- `port/re/ghidra/` — 8260 декомпилированных функций (Ghidra 12.2), `functions.json`, `symbols.json`, `decomp/`, `asm/`.
- `port/tools/run_ghidra_export.sh`, `port/re/GhidraExportScript.java` — воспроизводимый экспорт.
- `port/tools/wad_tool.py`, `kwd_tool.py`, `kmsh_decompress.py`, `kmf_tool.py`,
  `tgq_probe.py`, `tgq_decode.py`, `sdt_probe.py`, `mission_tool.py`.
- `port/build/KMF/` — 2015 распакованных KMF (не включать в репозиторий).
- `port/docs/format-research/kmf_catalog.json` — каталог всех мешей.

## 4. Инструменты

- REA 6.10.1 (https://github.com/morluto/rea) — `inspect_pe_imports`, `open_binary`, `binary_session`.
- Ghidra 12.2 PUBLIC — headless, `-Xmx3G`, `-analysisTimeoutPerFile 2400`.
- 010 Editor binary templates (dk2-tools) — как спецификация, не как runtime.
- `gh` CLI / GitHub API — поиск репозиториев и кода.
- `mpg123`, `pdftotext`, Python 3.14, clang++ 17.

## 5. Ссылки на публикации/поиск

- GitHub code search: `gh search code 'KMF.bt'` → `franjom/netherhold:docs/research/wall-rendering.md`
  (упоминает Hoenir's 010 Editor template и trass3r).
- GitHub repo search: `ufdada/dk2-tools`, `mleitner-dev/dk2-save-tools`.
- Прямые URL репозиториев из раздела 1 — первичные источники кода.

## 6. Оговорки

- Исходники OpenKeeper/dk2-tools использованы **только как спецификация**.
  В наш C-декодер переносится алгоритм и структура данных, а не Java-код
  и не зависимости jME3/libGDX.
- Ни один результат не сфабрикован: где декодер отсутствует, это указано явно.
- Игровые ресурсы не распространяются вместе с проектом.
