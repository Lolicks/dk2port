# Оригинальный AI существ DK2 — реконструкция по декомпиляции

Источник: 8260 декомпилированных функций Ghidra в `re/ghidra/decomp/`,
`re/ghidra/functions.json`, `re/ghidra/symbols.json` и бинарник
`game/DKII.EXE` (VA→file-offset через PE-секции). OpenKeeper используется
**только как генератор гипотез**; его Java-код не является доказательством
поведения оригинального `DKII.EXE`.

## 0. Метод атрибуции (почему вообще можно назвать функции)

Ghidra не дала имён игровым функциям (`FUN_xxxxxxxx`), но в `.data` бинарника
сохранились **оригинальные отладочные строки** исходника: пути `.cpp` и
`$"-имя polja %d"`. Метка в `symbols.json` содержит адрес строки, а тело
функции ссылается на неё. Пример (`re/ghidra/decomp/00463190.c:20-22`,
`GuardState.cpp`, строка 0x228):

```c
if (0 < iVar3) {
  iVar1 = FUN_005558d0(iVar3,*(int *)(param_1 + 0xc) + 0x15a0,
                       s_D__Dev_DK2_Projects_Source_Game__006985c8,0x228);
```

Строка по VA `0x006985c8` в `game/DKII.EXE` (file offset `0x283fc8`):
`D:\Dev\DK2\Projects\Source\Game\GuardState.cpp`.
`FUN_005558d0(n, bucket, file, line)` — это макрос проверки/assert с номером
строки. Поэтому ссылка на путь + номер строки **однозначно**
приписывает функцию исходному файлу.

Всего из `.data` извлечено **73 строки путей оригинальных исходников** и
**1068 отладочных строк полей**. Это главный источник точности документа.

### Шкала достоверности

| Уровень | Что означает |
|---|---|
| **HIGH** | Есть цитируемый фрагмент декомпиляции и/или прямое обращение функции к строке из оригинального `.cpp`; имя/адрес/поле наблюдаемы. |
| **MEDIUM** | Имя поля прочитано из оригинальной отладочной строки дампа, но точный алгоритм его изменения не восстановлен. |
| **LOW** | Гипотеза по соседству адресов или по OpenKeeper; в бинарнике не подтверждена. |

---

## 1. Карта «исходный файл → функция»

Таблица составлена по ссылкам на строки путей (HIGH для строк с обращением из
кода, LOW для путей, у которых в export нет ни одного обращения — они лежат в
`.data`, но их-using функция в `decomp/` не сохранилась как именованная).

| Оригинальный файл (VA строки) | Функция(и) | Увер. |
|---|---|---|
| `ExploreState.cpp` (`0x00698008`) | `FUN_0045a290` (вызов в `00478800.c:88-89`) | HIGH |
| `FightWithDrawState.cpp` (`0x006984b8`) | `FUN_004611d0` | HIGH |
| `GoodState.cpp` (`0x00698510`) | `FUN_00461bb0` | HIGH |
| `GuardState.cpp` (`0x006985c8`) | `FUN_00463190` | HIGH |
| `ImpState.cpp` (`0x006989cc`) | `FUN_00464240`, `FUN_00464400` | HIGH |
| `ImpUtils.cpp` (`0x00698c60`) | `FUN_00468d10` | HIGH |
| `KeeperState.cpp` (`0x00698c90`) | `FUN_0046eab0` | HIGH |
| `ManufactureState.cpp` (`0x00698cf0`) | `FUN_00470b20` | HIGH |
| `PayState.cpp` (`0x00698d98`) | `FUN_00472390` (ссылка в `00472390.c:108`) | HIGH |
| `PrisonState.cpp` (`0x00698df8`) | `FUN_00473350` | HIGH |
| `ReaperState.cpp` (`0x00698e28`) | `FUN_004748e0` | HIGH |
| `ResearchState.cpp` (`0x00698e64`) | `FUN_004761f0` | HIGH |
| `RunAway.cpp` (`0x00698e98`) | `FUN_004764a0` | HIGH |
| `CasinoState.cpp` (`0x00697e78`) | `FUN_00456500`, `FUN_00456690`, `FUN_00456ec0`, `FUN_00457010` | HIGH |
| `Creature.cpp` (`0x0069c6a0`) | `FUN_00488930`, `FUN_0048ad50`, `FUN_0048e160`, `FUN_0048f410`, `FUN_0048f930`, `FUN_00491900`, `FUN_004919c0` | HIGH |
| `State.cpp` (`0x00698f08`) | `FUN_004776c0`, `FUN_004780e0`, `FUN_004782c0`, `FUN_004784c0`, `FUN_00478680` | HIGH |
| `StateGroup.cpp` (`0x00699e88`) | `FUN_0047ad70` | HIGH |
| `StateUtils.cpp` (`0x00699f78`) | `FUN_00480a00`, `FUN_00481610`, `FUN_00481d50` | HIGH |
| `WorldTrigger.cpp` (`0x006a5c9c`) | `FUN_00519b20`, `FUN_00519f90` | HIGH |
| `ComputerPlayer.cpp` (`0x006a3348`) | `FUN_004f9b90`, `FUN_004fa000`, `FUN_004fc690`, `FUN_004fce70`, `FUN_004fe9c0`, `FUN_004ffd80`, `FUN_005000a0`, `FUN_005003a0`, `FUN_005006d0`, `FUN_00500c20`, `FUN_00502f00`, `FUN_00503060`, `FUN_00503690` | HIGH |
| `HungerState.cpp` (`0x006985f8`), `LairState.cpp` (`0x00698cc0`), `SulkState.cpp` (`0x00699fa8`), `PatrolState.cpp` (`0x00698d68`), `TourtureState.cpp` (`0x0069a00c`), `TrainState.cpp` (`0x0069a040`), `DisArmTrapState.cpp` (`0x00697fd4`), `CombatPitState.cpp` (`0x00697ea8`), `PrayState.cpp` (`0x00698dc8`) | строка есть, но `grep -rl` по `re/ghidra/decomp/` даёт 0 обращений | LOW (существование состояния HIGH, адрес неизвестен) |

---

## 2. Структура состояние-машины: `CState` (дамп `FUN_00479200`)

`FUN_00479200` — дамп **объекта состояния**, встроенного в существо. Это не
гипотеза: каждая строка дампа содержит имя поля и смещение в `param_1`.
Цитата (`re/ghidra/decomp/00479200.c:5-25`):

```c
FUN_005595c0(&DAT_00756ee8,s_State__00699678);
FUN_005595c0(&DAT_00756ee8,s_Opened__d_00699664,*(undefined4 *)(param_1 + 4));
FUN_005595c0(&DAT_00756ee8,s_CurrentStateId__d_00699648,*(undefined4 *)(param_1 + 0x10));
FUN_005595c0(&DAT_00756ee8,s_GameTickLastUpdated__d_00699628,*(undefined4 *)(param_1 + 0x14));
FUN_005595c0(&DAT_00756ee8,s_StartedAt__d_00699614,*(undefined4 *)(param_1 + 0x18));
FUN_005595c0(&DAT_00756ee8,s_Age__d_00699604,*(undefined4 *)(param_1 + 0x1c));
FUN_005595c0(&DAT_00756ee8,s_LastAteTime__d_006995b0,*(undefined4 *)(param_1 + 0x48));
FUN_005595c0(&DAT_00756ee8,s_HungerNeed__d_00699598,*(undefined4 *)(param_1 + 0x4c));
FUN_005595c0(&DAT_00756ee8,s_LastAttemptedHunger__d_00699578,*(undefined4 *)(param_1 + 0x50));
FUN_005595c0(&DAT_00756ee8,s_LastSleptTime__d_00699560,*(undefined4 *)(param_1 + 0x54));
FUN_005595c0(&DAT_00756ee8,s_SleepNeeded__d_00699548,*(undefined4 *)(param_1 + 0x58));
FUN_005595c0(&DAT_00756ee8,s_LastAttemptedSleep__d_00699528,*(undefined4 *)(param_1 + 0x5c));
FUN_005595c0(&DAT_00756ee8,s_PayCheckedTime__d_0069950c,*(undefined4 *)(param_1 + 0x60));
FUN_005595c0(&DAT_00756ee8,s_CurrentlyDoingJobId__d_006994ec,*(undefined4 *)(param_1 + 100));
```

Строки извлечены из `game/DKII.EXE` по адресам меток, например
`0x00699598` → `"      HungerNeed %d\n"`, `0x006994ec` →
`"      CurrentlyDoingJobId %d\n"` (HIGH).

### 2.1. Поляneeds/job/mood (HIGH — имя и смещение Observable)

| Смещение в `CState` | Оригинальное имя | Смысл (по имени) | Confidence |
|---:|---|---|---|
| `+0x04` | `Opened` | состояние активно | HIGH |
| `+0x10` | `CurrentStateId` | текущий state id | HIGH |
| `+0x14` | `GameTickLastUpdated` | тик последнего обновления | HIGH |
| `+0x18` | `StartedAt`, `+0x1c` `Age` | возраст состояния | HIGH |
| `+0x40` | `StateBeforeUtilityState` | возврат из utility-состояния | HIGH |
| `+0x44` | `SuspendTime` | пауза | HIGH |
| `+0x48` | `LastAteTime` | последний приём пищи | HIGH |
| `+0x4c` | `HungerNeed` | потребность в еде | HIGH |
| `+0x50` | `LastAttemptedHunger` | последняя попытка поесть | HIGH |
| `+0x54` | `LastSleptTime` | последний сон | HIGH |
| `+0x58` | `SleepNeeded` | потребность во сне | HIGH |
| `+0x5c` | `LastAttemptedSleep` | последняя попытка спать | HIGH |
| `+0x60` | `PayCheckedTime` | проверка выплаты | HIGH |
| `+0x64` (100) | `CurrentlyDoingJobId` | текущая работа | HIGH |
| `+0x68` | `ReturnToJobId` | возврат к прерванной работе | HIGH |
| `+0x6c` | `ReturnRoomTagId` | тег комнаты возврата | HIGH |
| `+0x6e` | `DroppedToDoJobId` | брошенная работа | HIGH |
| `+0x7a` | `Data.CarriedGold` | переносимое золото | HIGH |
| `+0x86` | `Data.IdleCounter` | счётчик простоя | HIGH |
| `+0x8a..+0xa7` | `ImpJobData.*` (11 полей: `ImpTaskID`, `ImpTaskPositionX/Y`, `ImpTaskSurface`, `ImpTaskSlot`, `LastTaskType`, `JustDropped`, `DroppedTask`, `LastEvaluate`, `ScanningJob`, `ScanningData`) | внутренний планировщик работы импа | HIGH |
| `+0xdf` | `UtilityNewState` | целевое utility-состояние | HIGH |
| `+0xe3..+0xf5` | `MoveTo`, `RoomId`, `RoomTypeId`, `MoveToRoom`, `FailedMoveState`, `MoveWithinRangeOfThing`, `ThingId`, `RangeSquared`, `ReCalcNavCounter`, `MoveToForNTurns`, `TurnCounter`, `WaitFor` | параметры подсистемы перемещения | HIGH (switch по `CurrentStateId`, `00479200.c:48-95`) |
| `+0xf9` | `LastGameTickSuccessfullyChoseUnhappyJob` | выбор работы при настроении «unhappy» | HIGH |
| `+0xfd` | `LastGameTickSuccessfullyChoseHappyJob` | выбор работы при «happy» | HIGH |
| `+0x101` | `LastGameTickSuccessfullyChoseAngryJob` | выбор работы при «angry» | HIGH |
| `+0x105` | `LairCalculated` | логово пересчитано | HIGH |
| `+0x109` | `LairAvailability` | доступность логова | HIGH |
| `+0x10d` | `Lair` | id логова | HIGH |
| `+0x10f` | `PrisonCalculated` | тюрьма пересчитана | HIGH |
| `+0x113` | `PrisonAvailability` | доступность тюрьмы | HIGH |
| `+0x117` | `Prison` | id тюрьмы | HIGH |
| `+0x119/+0x11d` | `TorturingTargetPositionX/Y` | цель пытки | HIGH |

Ключевой вывод: **выбор работы разделён на три ранга настроения** — три
независимых флага «успешно выбрал работу в этом тике» для unhappy / happy /
angry (`00479200.c:96-104`). Это прямо опровергает текущую модель `DK2.cpp`
с одним линейным `if/else if`.

### 2.2. Подсистема перемещения (path following)

`FUN_00479200.c:48-95` показывает, что path following — это **перечисление
utility-состояний** с собственными параметрами и полем `FailedMoveState`:

```c
switch(*(undefined4 *)(param_1 + 0x10)) {
case 6:  /* MoveTo(x,y) */                 ... uVar1 = *(undefined4 *)(param_1 + 0xeb);
case 7:  /* RoomId, RoomTypeId, MoveToRoom */ ...
case 8:  /* MoveWithinRangeOfThing(x,y), ThingId, RangeSquared, ReCalcNavCounter */
case 9:  /* MoveToForNTurns(x,y), TurnCounter */
case 0x58: /* WaitFor */
}
```

Дополнительные/original строки об ошибках пути из `.data`:
`0x006a1310` `"rtp:Exceeded max path length (i:%d,R:%d) (%d,%d)->(%d,%d)"`,
`0x006a134c` (`L:`), `0x006a1388` `"grtc:Exceeded max path length (i:%d,rl:%d)"`,
далее дамп `path_finished`, `path_count`, `path_buffer_count`, `path_flags`,
`path_total_count`, `path_stuck_buffer`, `path_last_progress`, `path_tail_dist`,
`path_subpath_dist`, `path_total_dist`, `path_target_pos` (`0x006a1664..0x006a1810`).
HIGH для existence полей, LOW для алгоритма A*/nav-графа (код не атрибутирован).

---

## 3. `CCreature`: дамп `FUN_00490240` (поля существа)

`FUN_00490240` сам вызывает дамп состояния `FUN_00479200` (`00490240.c:18`)
и затем печатает **поля `CCreature`** с адресами. Фрагмент (`00490240.c:22-30`):

```c
FUN_005595c0(&DAT_00756ee8,s_LairId__d_0069d724,*(undefined2 *)(param_1 + 0x3b7));
FUN_005595c0(&DAT_00756ee8,s_RoomId__d_0069d714,*(undefined2 *)(param_1 + 0x3b9));
FUN_005595c0(&DAT_00756ee8,s_MovementMode__d_0069d6fc,*(undefined4 *)(param_1 + 0x3bb));
FUN_005595c0(&DAT_00756ee8,s_FearlessUntil__d_0069d63c,*(undefined4 *)(param_1 + 0x3d7));
FUN_005595c0(&DAT_00756ee8,s_Idle__d_0069d668,*(undefined4 *)(param_1 + 0x3d1));
```

Отобраные AI-поля (все HIGH — имя из строки + смещение из кода):

| Смещение | Имя | Категория |
|---:|---|---|
| `+0x36f` | `TypeId` | вид существа |
| `+0x37a` | `IsInBattleFlag` | бой |
| `+0x37e` | `CanLeaveBattleAt` | бой/_retreat_ |
| `+0x3af` | `WorkModifier`, `+0x3b3` `WorkEnhancement` | работа |
| `+0x3b7` | `LairId` | **логово** |
| `+0x3b9` | `RoomId` | комната |
| `+0x3cb` | `PrisonOwner` | тюрьма |
| `+0x3d7` | `FearlessUntil` | **страх** |
| `+0x3d5` | `ReportingTo` | иерархия/party |
| `+0x3df` | `MyLeadersId`, `+0x3e1` `PositionInParty` | party |
| `+0x3e2` | `GetPaid` | **выплата** |
| `+0x3e6` бит 2 | `AttackAbnormalTargets` | бой |
| `+0x3e6` бит 3/4 | `RebelActive` / `RebelLeader` | бунт/reaction |
| `+0x3e6` бит 7 | `Possessed` | possession |
| `+0x3e6` бит 0xf | `CombatPitFighter` | бой |
| `+0x3e6` бит 0x16..0x19 | `FightProcessCounter` | бой |
| `+0x3e6` бит 0x14 | `CallToArmsOn` | реакция на приказ |
| `+0x3ea` бит 4 | `HasBeenConverted` |обращение |
| `+0x3ea` бит 5 | `SkipNextPayment` | **выплата** |
| `+0x3ea` бит 0xd | `CasinoGamblingFearless` | страх |
| `+0x3ea` бит 0x14 | `Fleeing` | **бегство** |
| `+0x3f7` | `HighPriorityBody` | бой |
| `+0x3f9` | `AttackingId` |бой |
| `+0x3fb` | `GoldHeld` | золото/выплата |
| `+0x3ff` | `ExperiencePoints`, `+0x403` `Level` | рост |
| `+0x419` | `TriggerRoot`, `+0x4f9` `TriggerRootRepeatCount` | триггеры существа |
| `+0x427` | `TimeLastUsedMelee` | бой |
| `+0x443..+0x46d` | `ArmourTimer`, `InvulnerableTimer`, `FreezeTimer`, `ConcealedTimer`, `ChickenTimer`, `ScapegoatTimer`, `InvisibleTimer`, `ObedientTimer`, `TurncoatTimer`, `AngrySpecialTimer`, `SpeedChangeTimer`, `CreatureAlertTimer`, `PoisonedTimer`, `FearedTimer`, `FearedSeverity`, `FearedByCreatureId`, `OnFireCounter`, `LastDroppedCounter` | таймеры/состояния |
| `+0x46f` | `Flags` | флаги fighting (бит 0 проверяется в combat-функциях) |
| `+0x470` | `PaymentNeeded`, `+0x474` `PaymentAttempts` | **выплата** |
| `+0x4bb` | `LastSlapTick`, `+0x4c3` `LastReactionTime`, `+0x4c7` `LastReactionTarget` | **реакции** |
| `+0x4d7` | `WorkFasterTimer`, `+0x4d9` `AttackCreatureHits` |работа/бой |
| `+0x4e5` | `LastHelloTime` | реакция-приветствие |

### 3.1. Структура `Fight` (дамп `FUN_004915d0`, HIGH)

`re/ghidra/decomp/004915d0.c:5-19`:

```c
FUN_005595c0(&DAT_00756ee8,s_Fight__0069da1c);
FUN_005595c0(&DAT_00756ee8,s_AttackRangeSquared__d_0069d9fc,*param_1);
FUN_005595c0(&DAT_00756ee8,s_Attacking__d_0069d9e8,*(undefined2 *)(param_1 + 1));
FUN_005595c0(&DAT_00756ee8,s_TempFear__d_0069d9d4,*(undefined2 *)((int)param_1 + 0x26));
FUN_005595c0(&DAT_00756ee8,s_CurrentFear__d_0069d9bc,*(undefined2 *)(param_1 + 10));
FUN_005595c0(&DAT_00756ee8,s_OverThreatCounter__d_0069d9a0,*(undefined1 *)((int)param_1 + 0x2a));
FUN_005595c0(&DAT_00756ee8,s_AttackType__d_0069d988,*(byte *)((int)param_1 + 0x2b) & 0xf);
FUN_005595c0(&DAT_00756ee8,s_SpellNo__d_0069d974,*(byte *)((int)param_1 + 0x2b) >> 4);
FUN_005595c0(&DAT_00756ee8,s_BackOffTimer__d_0069d928,*(undefined1 *)(param_1 + 0xd));
FUN_005595c0(&DAT_00756ee8,s_ThreatThing__d_0069d910,*(undefined2 *)((int)param_1 + 0x35));
FUN_005595c0(&DAT_00756ee8,s_ThreatThingThreshold__d_0069d8f0,*(undefined2 *)((int)param_1 + 0x37));
FUN_005595c0(&DAT_00756ee8,s_AvoidCreature__d_0069d8d8,*(undefined2 *)((int)param_1 + 0x39));
FUN_005595c0(&DAT_00756ee8,s_AvoidCounter__d_0069d8c0,*(undefined2 *)((int)param_1 + 0x3b));
FUN_005595c0(&DAT_00756ee8,s_RetreatingCreature__d_0069d8a0,*(undefined2 *)((int)param_1 + 0x3d));
```

Плюс два массива по 8 элементов: `RangeAttackers` (+6) и `MeleeAttackers`
(+0x16). Это означает: **до 8 range- и до 8 melee-атакующих отслеживаются
одновременно**; решение о страхе/бегстве принимается по сумме threat, а не по
одному врагу, как сейчас в `DK2.cpp`. Confidence: HIGH для структуры,
MEDIUM для правила принятия решения.

`FUN_0049d310.c:11-17` дампит общий `CThing` (`StateId +0x13a`,
`StateAge +0x13e`, `TriggerRoot +0x144`, `TriggerRootRepeatCount +0x137`) —
HIGH, связывает триггеры напрямую с объектом-существом.

### 3.2. `Anger` / настроение (дамп `FUN_00487210`, HIGH)

`re/ghidra/decomp/00487210.c:5-14`:

```c
FUN_005595c0(&DAT_00756ee8,s_Anger__0069a104,param_1);
FUN_005595c0(&DAT_00756ee8,s_UnhappyAt__d_0069a0f0,param_1[6]);
FUN_005595c0(&DAT_00756ee8,s_State__d_0069a0e0,param_1[7]);
FUN_005595c0(&DAT_00756ee8,s_Anger_Values__0069a0c8);
iVar1 = 6;
do { FUN_005595c0(&DAT_00756ee8,&DAT_0069a0c4,*param_1);
     param_1 = param_1 + 1; iVar1 = iVar1 - 1; } while (iVar1 != 0);
```

То есть original хранит **6 числовых значений anger** плюс порог `UnhappyAt`
и текущее `State`, а не одно число `mood` 0..100. Это важный gap.

---

## 4. Dispatcher состояний `FUN_00478800` (HIGH)

`re/ghidra/decomp/00478800.c:19-102` — свитч по `param_2` (state id), который
вызывает Prozess-функцию состояния и запоминает id в `param_1+100`:

```c
switch(param_2) {
  ...
  case 0x18:
    iVar2 = FUN_0045a290();   /* ExploreState.cpp */
    break;
  ...
}
iVar3 = iVar2;
if (iVar2 != 0) { *(int *)(param_1 + 100) = param_2; }
```

Полное соответствие `case → функция` (все HIGH как адреса вызовов):

| id | func | id | func | id | func |
|---:|---|---:|---|---:|---|
| 1 | `0046f440` | 2 | `004633d0` | 3 | `004759d0` |
| 4 | `00485240` | 5 | `00470080` | 6 | `00462850` |
| 7 | `00483ae0` | 8 | `004728e0` | 9 | `004560f0` |
| 10 | `00459fd0` | 11 | `004582c0` | 12 | `00458690` |
| 13 | `00481fa0` | 14 | `00482690` | 15 | `00482ef0` |
| 16 | `00475170` | 17 | `00482a60` | 18 | `00462310` |
| 19 | `00462490` | 22 | `00486cc0` | 23 | `00471e80` |
| **24 (0x18)** | **`0045a290` = Explore** | 25 | `00481ef0` | 26 | `00457780` |
| 27 | `0046e8e0` | 28 | `00483ac0` | | |

Особый путь (HIGH, `00478800.c:8-17`): таблица `DAT_006da8c7` индексируется
полем `+0x25e`; если результат `0x02`, берётся таблица по полю `+0x32d`; если
итоговое значение `0x16` — dispatch пропускается (`return 0`). Это «глобальный
override» состояния боя поверх обычного state machine.

---

## 5. Подсистемы по требованиям задания

### 5.1. Job selection

* **Факт (HIGH):** в `CState` есть `CurrentlyDoingJobId` (+0x64),
  `ReturnToJobId` (+0x68), `DroppedToDoJobId` (+0x6e) и три флага успешного
  выбора работы по настроению (+0xf9/+0xfd/+0x101). См. §2.1.
* **Факт (HIGH):** у импа есть отдельный 11-полевый планировщик `ImpJobData`
  (§2.1), то есть работа импа выбирается **не** общим job-selection кодом.
* **Факт (MEDIUM):** `ImpState.cpp` атрибутирован функциям `00464240`,
  `00464400`; `ImpUtils.cpp` → `00468d10`.
* **Гипотеза (LOW, по OpenKeeper):** приоритеты работ зависят от вида существа
  и от насыщенности комнат; в бинарнике точный порядок не восстановлен.

### 5.2. Needs: hunger / sleep / pay / lair

| Need | Доказательство | Confidence |
|---|---|---|
| Hunger | `HungerNeed +0x4c`, `LastAteTime +0x48`, `LastAttemptedHunger +0x50`; файл `HungerState.cpp` присутствует в `.data` | HIGH (поля/файл), LOW (алгоритм) |
| Sleep | `SleepNeeded +0x58`, `LastSleptTime +0x54`, `LastAttemptedSleep +0x5c`; `StateBeforeUtilityState +0x40` | HIGH |
| Pay | `PayCheckedTime +0x60`; `PaymentNeeded +0x470`, `PaymentAttempts +0x474`, `GetPaid +0x3e2`, `SkipNextPayment` (бит 5 `+0x3ea`), `GoldHeld +0x3fb`; `PayState.cpp` → `FUN_00472390` | HIGH |
| Lair | `LairId +0x3b7`; `LairCalculated/LairAvailability/Lair +0x105/+0x109/+0x10d`; строка `AutomaticallyBuildBiggerLairWhenPortalTakenOver` (`0x006a3d5c`) в `Player`-дампе | HIGH (поля), MEDIUM ( rule ) |

Цитата из PayState (`re/ghidra/decomp/00472390.c:67-72`) — при отсутствии
доступной комнаты выплаты выбирается переход в state `0x37`:

```c
if (local_c == 0) {
  if (local_5 == '\0') {
    uVar12 = *(undefined2 *)(*(int *)(param_1 + 8) + 0x28);
    uVar11 = 0x37;
    goto LAB_0047266a;
  }
```

### 5.3. Fear / mood / реакции

* Дамп Fight (§3.1): `CurrentFear`, `TempFear`, `OverThreatCounter`,
  `ThreatThing`, `ThreatThingThreshold`, `AvoidCreature`, `AvoidCounter`,
  `RetreatingCreature`, `BackOffTimer`. HIGH.
* Строка-предупреждение оригинала (`0x00698368`, ссылка в
  `re/ghidra/decomp/0045f030.c:214`):
  `"Stats or level issue, creature type %d has fear threshold of %d on this level"`
  → порог страха берётся из **stats/level** и проверяется на валидность. HIGH.
* `FUN_0045cb00` — `SelectAVictim`-путь; проверки при выборе цели
  (`0045cb00.c:52-66`):

```c
if ((*(uint *)(param_1 + 0x3e6) & 0x80) != 0) {  /* possessed */
  FUN_00634db0(...,s_Creature_type__d_tag__d_is_posse_006981a4,...);
}
if (((*(byte *)(param_1 + 0x46f) & 1) == 0) &&
    ((*(uint *)(param_1 + 0x3e6) & 4) == 0)) {  /* not a fighter */
  FUN_00634db0(...,s_Creature_type__d_tag__d_is_not_a_00698134,...);
}
if (*(short *)(param_1 + 0x502) != 0) {         /* уже атакует */
  FUN_00634db0(...,s_Creature_type__d_tag__d_is_alrea_006980b8,...);
}
```

* Реакции: `LastReactionTime +0x4c3`, `LastReactionTarget +0x4c7`,
  `LastSlapTick +0x4bb`, `LastHelloTime +0x4e5` ( HIGH ),
  `RespondToAttack` в строке `0x006a34dc`, `CreaturesCommanded` /
  `CreaturesConverted` в player-дампе. MEDIUM.
* `RunAway.cpp` → `FUN_004764a0`: решение о бегстве проверяет биты
  `+0x3e6 & 0x200000`, `+0x3ea & 0x40`, `+0x3ea & 0x100000` и id
  `0xd2 / 0x107 / 0x108` (`004764a0.c:27-49`); затем считает направление
  через `FUN_0065fae0` (sqrt) — HIGH для кода, MEDIUM для семантики id.

### 5.4. Combat decisions

* `FUN_0045cb00` (Creature.cpp) выбирает жертву, `FUN_0045f030` удерживает
  текущую цель и перепроверяет её; `FUN_004611d0` (`FightWithDrawState.cpp`)
  реализует состояние боя с отступлением.
* В `FUN_004611d0.c:33-70` выбор цели: либо по флагу `+0x46f & 0x10` из
  списка комнат (`FUN_004d0e60`), либо по `+0x3b7` (`LairId`-подобный тег),
  либо через `FUN_004c6460(5,&local_28)` — HIGH для потока управления.
* Атрибуты атаки из дампа Fight: `AttackType` (младшие 4 бита `+0x2b`),
  `SpellNo` (старшие 4 бита), `AttackRangeSquared`, 8 range/8 melee attackers.
* В OpenKeeper есть `CreatureAi`/job-таблицы, но это **переписанная** логика
  (`docs/format-research/OPENKEEPER_ANALYSIS.md:235-252`) —LOW как источник
  для оригинального AI.

---

## 6. Отображение «оригинал → что реализовать в engine/DK2.cpp»

| Оригинал (адрес / источник) | Что сейчас в `engine/DK2.cpp` | Что сделать |
|---|---|---|
| `CState` + `CurrentStateId`/`Age` (`00479200`) | `Creature.state` — один enum `CreatureState` (`DK2.h:71`) | Добавить age/tick-счётчик состояния и отдельный `job_id` (сейчас `CreatureJob`, `DK2.h:72`) |
| Три флага выбора работы по настроению (`00479200.c:96-104`) | один `if/else if` в `update_creature` (`DK2.cpp:462-482`) | Разделить выбор работы на unhappy/happy/angry с отдельными приоритетами и флагами «уже chosen this tick» |
| `ImpJobData` (11 полей, `00479200.c:38-47`) | нет | Отдельный planning-блок для вида Imp; не смешивать с общим job chooser |
| `HungerNeed`/`LastAteTime`/`LastAttemptedHunger` | `c.hunger` + порог 60 (`DK2.cpp:455,466`) | Перейти к паре «need + throttle-attempt», пороги вынести в stats (сейчас `hunger_rate` в `CreatureStats.generated.hpp`) |
| `SleepNeeded`/`LastSleptTime` | `c.fatigue`, порог 75 (`DK2.cpp:456,468`) | Разделить fatigue и sleep-need; добавить `LastSleptTime` |
| `LairId +0x3b7`, `Lair/LairAvailability/LairCalculated` | `find_room_tile(...Lair...)` без закрепления логова (`DK2.cpp:492-494`) | Хранить id закреплённого логова, пересчитывать availability по тику |
| `PayCheckedTime`, `PaymentNeeded`, `PaymentAttempts`, `SkipNextPayment` | нет (только `gold_carried`/`GoldHeld`) | Добавить pay-day state machine сAttempts и флагом пропуска |
| `Anger` (6 значений + `UnhappyAt` + `State`, `00487210`) | `c.mood` float 0..100 (`DK2.h:87`) | Заменить на anger-массив/порог; mood оставить как производную |
| `Fight` (CurrentFear, TempFear, Threat*, 8+8 attackers) | `c.fear` float + один enemy (`DK2.cpp:423-450`) | Добавитьfight-структуру с порогом из stats и учётом нескольких атакующих |
| `SelectAVictim` проверки possessed / not-fighter / already-attacking (`0045cb00.c:52-66`) | нет | Ввести те же guard-проверки перед назначением цели |
| Utility-state path (`MoveTo`, `MoveToRoom`, `MoveWithinRangeOfThing`, `MoveToForNTurns`, `WaitFor`, `FailedMoveState`) | один A* `find_path` + `c.path` (`DK2.cpp:343-397`) | Добавить типы utility-перемещений и `FailedMoveState` с повторным вычислением (`ReCalcNavCounter`) |
| `FUN_00478800` dispatch + battle override | нет явного dispatcher | Вынести switch состояний в отдельную функцию, добавить battle-override |
| `LastReactionTime/LastReactionTarget/LastSlapTick/LastHelloTime` | нет | Добавить counters реакций для slap/hello/attack |
| `TriggerRoot +0x419`, `TriggerRootRepeatCount +0x4f9` | триггеры только на уровне миссии | Дать существу собственный trigger root (см. TRIGGER_SEMANTICS.md) |

---

## 7. Ограничения

* В export **нет имён параметров, типов и RTTI-классов** для большинства
  функций; `__thiscall` — оценка Ghidra. Названия `CState`, `CCreature`, `Fight`,
  `Anger` даны по заголовкам дампов (`s_State__`, `s_Fight__`, `s_Anger__`) и
  по RTTI `.?AVCCreature@@` в бинарнике.
* Пути `.cpp` для 9 state-файлов не имеют обращений в `decomp/`: их функции
  не атрибутированы. Это **не** означает, что кода нет — только что экспорт
  не сохранил ссылку.
* Точные числовые пороги (например, `HungerNeed > N`) не восстановлены:
  константы в коде не привязаны к полям без дампа. Не выдумываются.
* OpenKeeper (`CreatureAi.java` и др.) — переписанная ECS-логика; её нельзя
  использовать как доказательство оригинального поведения, только как
  гипотезу (HIGH/LOW разделены в таблицах выше).
