# DK2 save format — попытка реконструкции и точный блокер

## 0. Что проверялось

| Источник | Путь | Результат |
|---|---|---|
| Декомпиляция 8260 функций | `re/ghidra/decomp/`, `functions.json`, `symbols.json` | найдены функции записи/чтения «PacketRecord», но **не** найден сериализатор существ |
| Бинарник | `game/DKII.EXE` (2 977 280 байт) | нет байт `DK2S`, нет zlib-импортов |
| Референс upstream | `research/dk2-save-tools/src/{header,creature,index}.ts` | header помечен working, creature block — **BROKEN** |
| 010-шаблон | `research/dk2-tools/Formats/Save/save_struct.bt` | структура описана, но крупные блоки — `ubyte data[2705]`, `data[3938]` |
| Наш инструмент | `tools/save_tool.py`, `tests/test_save.py` | честно не пар creature |
| Образцы сейвов | `game/Data/Save/save.txt` (1 байт), `Data/Save/` | **реальных сейвов нет** |

---

## 1. Отрицательные результаты (важнее положительных)

### 1.1. Магического `DK2S` в бинарнике нет (HIGH)

Проверено тремя способами:

```
grep -ril '53324b44\|444b3253' re/ghidra/asm re/ghidra/decomp   → 0 файлов
python3 -c "d=open('game/DKII.EXE','rb').read(); print([m.start() for m in re.finditer(b'DK2S',d)])"
                                                                 → []
strings -a -e l game/DKII.EXE | grep -i DK2S                    → 0
```

`DK2S` встречается **только** в `research/dk2-save-tools/src/header.ts:1`:

```ts
const MAGIC = Buffer.from("DK2S", "ascii");
```

Вывод: либо upstream-инструмент описывает **другую ревизию/платформу** сейва,
либо магическое число выдумано. Наш `tools/save_tool.py:13` (`MAGIC = b"DK2S"`)
и `tests/test_save.py`built на нём — **не верифицированы относительно
`DKII.EXE`**. Это первый точный блокер: нечем валидировать даже header.

### 1.2. zlib в бинарнике нет (HIGH)

`re/ghidra/symbols.json`: 0 символов с `compress`, `deflate`, `inflate`, `zlib`,
`adler`, `crc32`, `uncompress`. Есть только `CreateFileA/ReadFile/WriteFile`,
`SetFilePointer`, `GetFileSize`, `CloseHandle` (CRT-обвязка). Если сейв
сжимается, компрессор реализован внутри движка; в export он не атрибутирован.

### 1.3. Реальных образцов сейва нет (HIGH)

`find` по всему дереву проекта: `*.sav`, `*.qsav`, `*.dk2` — 0 файлов
(единственные совпадения — SVG-иконки OpenKeeper). `game/Data/Save/save.txt`
состоит из 1 байта. Без образца невозможно проверить ни смещения, ни magic,
ни variable-length записи.

---

## 2. Что даёт 010-шаблон (HIGH как описание, LOW как истина)

`research/dk2-tools/Formats/Save/save_struct.bt` описывает файл как:

```c
uint signature;
uint u_1;
char date[20];
uint u_2; // 16
ubyte not_changed_1[4];
uint u_4;
ubyte u_5[4];
uint u_6; // 40
uint u_7;
ubyte not_changed_2[4];
uint u_8;
ubyte u_9[4];
uint u_10;  // 8
uint u_11;  // 8
ushort u_12_1;  // 3
ushort u_12_2;
ubyte u_13[118];
char level[24];
...
Player good;
Player neutral;
Player1 player;
PlayerData player_data[507];
ubyte u_15_1[35];
D1 u_15_2[1143];
...
FSeek(size - 242 * sizeof(Data) - 52);
Data d[242];        // sizeof(Data) = 2 + 2 + 46 = 50
ubyte u_15_3[52];
```

Ключевые числа: `PlayerData` = `ushort + ushort + ubyte[32]` = 36 байт,
всего `507 * 36 = 18 252` байт; хвост = `242 * 50 + 52 = 12 152` байт.
 размер `Player` огромен и содержит `wchar_t name[218]` (436 байт) плюс
таблицы `D2..D6` с `quantity`-зависимыми массивами. **Это не современная
variable-length creature record, а фиксированные block-структуры**;
почему dk2-save-tools видит «drift после 4 записей» — 010 template не
объясняет. LOW.

---

## 3. Что реально пишет оригинал: `PacketRecord` (HIGH по коду)

В `DKII.EXE` есть строки (file offsets → VA):

| VA строки | Строка | Где встречается |
|---|---|---|
| `0x0068e56c` | `PacketRecord: Loading from %s` | `.data`, нет code-ref в export |
| `0x0068e5b8` | `PacketRecord: Render Frequency is %d` | там же |
| `0x0068e5e0` | `PacketRecord: Saving to %s` | там же |
| `0x0068e634` | `%s\*.sav` | там же |

Фактический записывающий код — `FUN_0055ea10` (`0055ea10.c:8-20`):

```c
pbVar2 = param_2;
local_4 = param_1;
piVar3 = (int *)FUN_005b3eb0(&local_4,*(undefined4 *)(param_1 + 0x10c),param_2,0x11,&param_2);
if (-1 < *piVar3) {
  bVar1 = *pbVar2;
  if ((bVar1 != 0) &&
     (piVar3 = (int *)FUN_005b3eb0(&local_4,*(undefined4 *)(param_1 + 0x10c),pbVar2 + 0x11,
                                   (uint)bVar1 * 0x12,&param_2), *piVar3 < 0)) {
    return;
  }
  FUN_005b3eb0(&local_4,*(undefined4 *)(param_1 + 0x10c),param_1 + 0x128,0xc,&param_2);
}
```

Разбор (HIGH):

1. `FUN_005b3eb0` — запись через vtable-метод объекта `FileSystem`-подобного
   интерфейса (сравнение `iVar1 == param_4` как успех).
2. Пишется **0x11 = 17 байт заголовка**.
3. Из заголовка читается `bVar1 = *pbVar2` — счётчик записей.
4. Пишется `bVar1 * 0x12 = 18 * N` байт данных.
5. Пишется **0x0c = 12 байт трейлера** из `param_1 + 0x128`.

Та же формула в `FUN_0055eab0.c:57`:

```c
FUN_005b3ee0(&param_2,param_1[0x43],param_2,(uint)*param_2 * 0x12 + 0x11,local_50);
FUN_005b3ee0(&param_2,param_1[0x43],param_3,0xc,local_50);
FUN_005b3a60(&param_2,param_1[0x43]);
```

Итого контейнер записи: **`17 + 18*N + 12` байт**; `FUN_0055eab0` добавляет
проверку `DAT_0075ae57` (командный флаг `-NOFILECHECKSUM`), то есть в
присутствии checksum-флага в конец добавляется ещё что-то через
`FUN_0055ea10(param_1 + 0x128, ...)`. HIGH для структуры вызовов, LOW для
содержимого 17/18/12-байтовых блоков.

### 3.1. Имена файлов и CLI (HIGH)

| Функция | Роль | Цитата |
|---|---|---|
| `FUN_0055c020` | строит `Data\Save`, `_sdata_Save_`, `_s_Dk2TextureCache_`, пути WAD | `0055c020.c:125-142` |
| `FUN_00532090` | убирает расширение `.sav`/`.SAV` из имени (`FUN_00636af0`) и вызывает `FUN_0055d560` | `00532090.c:12-22` |
| `FUN_0055d560` | кладёт имя в структуру `param_1+0x90`, обнуляет `+0x10e`, ставит `+0x114 = 1` | `0055d560.c:4-9` |
| `FUN_0055e630` | обрабатывает `-PSAVE <name>` | `005a63b0.c:206-211` |
| `FUN_0055e7d0` | обрабатывает `-PLOAD <name>`; после успеха копирует имя в `DAT_0075ad57` и ставит `DAT_0075ae57 = 1` | `005a63b0.c:196-204`, `0055e7d0.c:86-93` |

Строки из бинарника: `%s\%s.sav` (`VA 0x006ad1c8`), `QuickSave.QSAV`
(`0x006a5b90`), `Failed to load saved game (%s)` (`0x006a53a8`),
`Data\Save` (`0x006a4b34`), `-PSAVE` (`0x006bf100`). HIGH.

### 3.2. Где хранится «сущность» сейва (HIGH)

`FUN_0055eab0.c:17-36` показывает, что state-объект записи имеет:
`*param_1 == 1` = save, `== 2` = `FUN_0055ea10`-path; `param_1[0x49]` —
флаг «уже записан»; `param_1[0x43]` — файловый дескриптор; `param_1[0x44]`,
`param_1[0x45]` — параметры из `-PLOAD`.

---

## 4. Почему creature record остаётся BROKEN — точный блокер

1. **`DK2S`-header непроверен** (§1.1). Мы не можем даже подтвердить, что
   `header.ts` описывает тот же формат, что `DKII.EXE`.
2. **Нет ни одного образца.** dk2-save-tools пишет «after four records the read
   cursor is roughly 11 bytes ahead» (`research/dk2-save-tools/src/creature.ts:6-24`).
   Проверить или опровергнуть это нечем: входных данных нет.
3. **Нет кода сериализации существ в export.** Поиск по всем 8260 функциям не
   нашёл функции, которая читает/пишет поля из дампа `FUN_00490240`
   (`LairId +0x3b7`, `PaymentNeeded +0x470`, `FearlessUntil +0x3d7`, ...) в
   файловый поток. Косвенные вызовы через vtable (`FUN_005b3eb0`) не дают
   имени методу. Следовательно, смещения полей из дампа **нельзя** напрямую
   перенести в record сейва.
4. **`PacketRecord` — не обязательно сам сейв.** Это абстрактный контейнер
   (17+18N+12), который используется и для сеть-пакетов (`00526590.c` содержит
   `GamePacket says we're OOS`, `Out Of Sync` и `-PNUMBER` checksum-логику).
   Ozначить, что `*.sav` = последовательность таких записей, **нельзя** без
   образца. LOW.
5. **Переменная длина имён.** `creature.ts:19-21` перебирал u8-prefix,
   фиксированные 32 байта и scan-forward; все три дают drift. 010-шаблон
   (`wchar_t name[218]`) содержит фиксированное имя в `Player`, но не в creature.
   Где именно лежит длина — неизвестно.

Формулировка блокера: **невозможно восстановить creature record, не имея ни
одного валидного `*.sav` и не найдя в `DKII.EXE` функцию, которая пишет поля
`CCreature` в файл.** Всё, что можно сделать на текущих данных, — описать
контейнер более низкого уровня (§3) и зафиксировать отрицательные результаты.

---

## 5. Что осталось неизвестным (и что đổiит гипотезу)

| Неизвестное | Какой результат его изменит |
|---|---|
| Magic файла | Найти `*.sav` (хотя бы из демо/патча); проверить первые 4 байта и Katmа `DK2S` |
| Есть ли сжатие | Найти в `DKII.EXE` вызов компрессора по косвенному vtable-слоту или дизассемблировать `FUN_005b3eb0` target |
| Структура 17-байтового заголовка PacketRecord | Дамп реального сейва + поиск повторяющегося шаблона 17+18N+12 |
| Порядок creature records | Найти функцию, пишущую `+0x3fb GoldHeld`/`+0x3ff ExperiencePoints` в буфер, и contrasting её с `creature.ts` |
| Хвост 242×50 + 52 (`save_struct.bt`) | Проверить на реальном файле: `FileSize - 12152` и сравнить сигнатуру |
| Связь с `-PSAVE`/`-PLOAD` | Проследить `DAT_0075ad57` → `FUN_0055eab0` → `FUN_005b3eb0` → конкретный vtable-метод |

## 6. Практический вывод для порта

`tools/save_tool.py` и `tests/test_save.py` сейчас **честно** объявляют
creatures неразобранными — это правильная позиция и её не следует менять без
образца. Единственное, что можно улучшить уже сейчас: добавить в документацию
предупреждение, что `MAGIC = b"DK2S"` не подтверждён `DKII.EXE`, и описать
`PacketRecord`-контейнер как **гипотезу уровня записи**, а не как факт о
формате `*.sav`.

Новый сейв-формат порта (`convert` → reconstructed DK2S v2) остаётся
несовместимым с оригиналом по построению; это нужно отразить в README статуса.
