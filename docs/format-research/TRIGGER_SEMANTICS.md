# Семантика цепочек TriggerGeneric / TriggerAction — правила для C++

Документ отвечает на вопросы, которые `TRIGGERS_FORMAT.md` оставил открытыми
(§9: «логическая роль `idNext` vs `idChild` не доказана»). здесь различаются:

* **HIGH** — байты и размеры, проверенные на 66 `*Triggers.kld` (см. `TRIGGERS_FORMAT.md`),
  либо прямая ссылка на код оригинального `DKII.EXE`;
* **MEDIUM** — семантика из независимой реализации OpenKeeper. Это
  **эталонная гипотеза**, не байт-доказательство: OpenKeeper — переписанный
  движок, но его `TriggerLoader`/`TriggerControl` воспроизводят структуру
  оригинальных records;
* **LOW** — интерпретация полей, которую OpenKeeper сам помечает `FIXME`,
  и всё, что касается выбора текстов из `.str`.

## 1. Первичные источники

| Источник | Что даёт | Цитата |
|---|---|---|
| `KwdFile.readTriggers` | точный порядок чтения `tdata[8]`, `id`, `idNext`, `idChild` | `research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/map/KwdFile.java:2373-2870` |
| `Trigger` | `hasNext()/hasChildren()` = `id != 0`; `repeatTimes`, 255 = always | `.../tools/convert/map/Trigger.java:32-35,89-103,111-123` |
| `TriggerGeneric` | `ComparisonType` 0..6; `TargetType` 0..74 | `.../tools/convert/map/TriggerGeneric.java:31-57,60-156` |
| `TriggerAction` | `FlagTargetValueActionType` биты; `MakeType`; `ButtonType` | `.../tools/convert/map/TriggerAction.java:100-153,260-288` |
| `TriggerLoader` | превращение плоского массива records в дерево | `.../game/trigger/TriggerLoader.java:48-90` |
| `TriggerControl` | покадровая Evaluate: children, next, actions, repeat | `.../game/trigger/TriggerControl.java:96-139` |
| `TriggerGenericData` | счётчик повторов и detached-флаги | `.../game/trigger/TriggerGenericData.java:71-79` |
| Декомпиляция `DKII.EXE` |  существование trigger-подсистемы в оригинале | `re/ghidra/decomp/00519b20.c`, `00519f90.c`, `0049d310.c` |

### Доказательство из оригинального бинарника (HIGH)

В `DKII.EXE` есть строки `D:\Dev\DK2\Projects\Source\Game\WorldTrigger.cpp`
(`0x006a5c9c`) и `TriggerRoot`/`TriggerRootRepeatCount` в дампе `CThing`:

```c
/* re/ghidra/decomp/0049d310.c:8,14 */
FUN_005595c0(&DAT_00756ee8,s_TriggerRootRepeatCount__d_0069c72c,*(undefined1 *)(param_1 + 0x137));
FUN_005595c0(&DAT_00756ee8,s_TriggerRoot__d_0069cf28,(int)*(short *)(param_1 + 0x144));
```

`TriggerRoot` — поле **объекта-существа** (TAG-индекс), `0` = нет триггера.
`FUN_00519b20` (`WorldTrigger.cpp:0x561`) проверяет тип тайла/door и вызывает
`FUN_004b78d0(tag, ...)` — событийная точка входа; `FUN_00519f90` ходит по
16 action point-слотовым записям (`param_1[uVar5*0x82 + 0x100c]`, шаг 8 байт)
и для каждой вызывает `FUN_00519b20` (`00519f90.c:135-158`). To есть
**action point — это событийный источник, а не просто координата** (HIGH).

---

## 2. Построение дерева из плоских records (MEDIUM-HIGH)

`TriggerLoader.parse` (`TriggerLoader.java:48-90`):

```java
Trigger temp = triggers.get(id);
...
parent.attachChild(trigger);
if (temp.hasChildren()) {
    parse(((TriggerGenericData) trigger), temp.getIdChild());
}
...
if (temp.hasNext()) {
    id = temp.getIdNext();
} else {
    break;
}
```

Правило (детерминированное, HIGH по структуре данных):

1. Root = запись с `id == TriggerRoot` объекта (0 = нет триггера).
2. `idChild` — ребёнок текущей записи: рекурсивный спуск. `0` = нет детей.
3. `idNext` — **следующий брат на том же уровне**: цикл `while (hasNext())`.
4. Порядок обхода братьев = порядок следования `idNext`, а не порядок записей
   в файле и не числовой порядок id.

Важное уточнение (HIGH, проверено по всем 9161 записям): генератор **не**
подменял `0` на `0xFFFF`. `0` = «нет ссылки» (4829 `next_id`, 5432 `child_id`),
а `65535` — **реальный id действия** (9 `next_id` и 50 `child_id` указывают на
записи с `id == 65535`). Все 1299 ненулевых ссылок разрешаются в существующие
записи. Любая проверка `id == 65535` как «сентинла» — ошибка.

---

## 3. Вычисление: `next` НЕ AND, `child` НЕ OR (MEDIUM)

Решающий код — `TriggerControl.updateControl` (`TriggerControl.java:96-139`):

```java
TriggerGenericData next = null;
trigger.subRepeatTimes();

for (int i = trigger.getLastTriggerIndex() + 1; i < trigger.getQuantity(); i++) {
    TriggerData value = trigger.getChild(i);
    switch (value) {
        case TriggerGenericData triggerGenericData -> {
            if (next == null && isActive(triggerGenericData)) {
                trigger.setLastTrigger((TriggerGenericData) value);
                next = (TriggerGenericData) value;
            }
        }
        case TriggerActionData triggerActionData -> {
            doAction(triggerActionData);
            if (!trigger.isRepeateable()) {
                trigger.detachChild(value);
                i--;
            }
        }
    }
}

if (trigger.getQuantity() == 0 && trigger.getParent() != null) {
    next = trigger.getParent();
    trigger.detachFromParent();
}
if (next == null) {
    trigger.setLastTrigger(null);
    trigger = (trigger.getParent() != null) ? trigger.getParent() : root;
} else {
    trigger = next;
}
```

Отсюда **точные правила** (MEDIUM — по OpenKeeper):

| Вопрос | Ответ |
|---|---|
| `next` = AND? | **Нет.** Братья-условия перебираются по порядку, и активируется **только первое активное** (`next == null && isActive(...)`). Это *приоритетный OR / first-match-wins*, а не конъюнкция. |
| `next` = OR? | Почти: для условий — first-match OR; для действий — **все** действия списка выполняются по очереди (ветка `case TriggerActionData` не имеет `if (next == null)`). |
| `child` = AND? | **Да, в sense gating:** children условия посещаются только когда это условие стало активным (`trigger = next` → следующий тик обрабатывает уже его children). То есть «условие И его поддерево». |
| Порядок | conditions и actions в одном списке children обрабатываются в порядке `idNext`; действие не блокирует обход следующих условий. |
| Возврат | Когда у узла не осталось детей (`getQuantity()==0`), он отсоединяется от родителя, и управление возвращается родителю; если активного ребёнка нет — сброс в `root`. |
| Начало с середины | `lastTrigger` хранит последний активный узел; следующий тик начинается с `lastTriggerIndex + 1` — то есть цепочка **продолжается**, а не перезапускается. |

**Прямое следствие для нашего порта.** В `engine/MissionTriggers.generated.hpp:2`
записано «`next = AND-chain, child = branch/action`», а `engine/DK2.cpp:616-625`
требует, чтобы **все** условия в `next`-цепочке были истинны:

```cpp
/* AND-chain: the condition and every node reachable by `next`
   must hold before the child action is fired. */
bool all = condition_true(cond, state_);
unsigned next = cond.next;
for (int depth = 0; all && next != 0 && next != 65535 && depth < 32; ++depth) {
    const DK2TriggerNode* nx = find_node(*set, next);
    if (!nx || nx->is_action) break;
    all = condition_true(*nx, state_) && all;
    next = nx->next;
}
```

Это **противоречит** правилу first-match. Минимальное исправление: заменить
`all && next-observe` на «первое истинное условие в списке братьев → спуск в его
`child`»; действия `next`-цепочки выполнять все подряд, а не требовать их истинности.

### 3.1. Повторяемость (repeat_times)

`TriggerGenericData.java:71-79`:

```java
public void subRepeatTimes() {
    if (repeatTimes != 255 && repeatTimes > 0) {
        repeatTimes--;
    }
}
public boolean isRepeateable() {
    return (repeatTimes > 0);
}
```

Правила:

* `255` = «always»: счётчик не уменьшается, узел живёт вечно (232 записи в каталоге).
* `0` = одноразовый: `isRepeateable()` false → **действие отсоединяется после
  первого выполнения** (`TriggerControl.java:117-120`); условие с 0 тоже не
  повторяется (decrement не происходит, но узел остаётся до опустошения children).
* `1` (6875 записей) — decrement до 0 на первом же тике, затем действие
  выполняется один раз и удаляется.
* `2/5/10/30` (14 записей всего) — действие выполняется N раз в N тиков.
* Всё это **игнорируется** в `DK2.cpp:evaluate_triggers` — condition проверяется
  каждый `step()` и действие может срабатывать бесконечно (HIGH по коду).

### 3.2. Операторы сравнения (HIGH по enum, MEDIUM по runtime)

`TriggerGeneric.java:31-57`:

| Значение | Оператор | Проверка в `TriggerControl.compare` (`:356-380`) |
|---:|---|---|
| 0 | NONE | false + warning |
| 1 | `<` | `target < value` |
| 2 | `≤` | `target <= value` |
| 3 | `=` | `target == value` |
| 4 | `>` | `target > value` |
| 5 | `≥` | `target >= value` |
| 6 | `!=` | `target != value` |

Наш `condition_true` (`DK2.cpp:585-597`) жёстко кодирует `>=` для всех типов —
заменить на таблицу операторов из record.

### 3.3. Flag / Timer: источник сравниваемого значения (MEDIUM)

`KwdFile.java:2564-2585` (FLAG) и `:2587-2605` (TIMER) читают:
`comparison:u8`, `targetId:u8`, `flag:u8`, `flagId/timerId:u8`, `value:u32`.
`TriggerControl.isActive` (`:148-190`):

```java
case FLAG:
    ... target = (targetId == 128) ? levelInfo.getLevelScore()
                                   : levelInfo.getFlag(targetId);
    if ((Short) trigger.getUserData("flag") == 1)
        value = (Integer) trigger.getUserData("value");
    else
        value = levelInfo.getFlag((Short) trigger.getUserData("flagId"));
    break;
case TIMER:
    ... target = (targetId == 16) ? timeLimitSeconds : (int) levelInfo.getTimer(targetId).getTime();
    if ((Short) trigger.getUserData("flag") == 1)
        value = (Integer) trigger.getUserData("value");
    else
        value = (int) Math.floor(levelInfo.getTimer((Short) trigger.getUserData("timerId")).getTime());
    break;
```

Правила для C++: `flag == 1` ⇒ сравнивать с литералом `value`; иначе ⇒
с текущим значением другого flag/timer. Идентификаторы **1-based в редакторе**,
но `KwdFile` читает как 0-based байт (в `TriggerGeneric.toString` для FLAG/TIMER
делается `targetId + 1`). Special ids: flag 128 = level score, timer 16 = time limit.

**Action FLAG** использует другую кодировку битов: `TriggerAction.java:112-118`
`VALUE=0x002, TARGET=0x004, EQUAL=0x008, PLUS=0x010, MINUS=0x020`.
`KwdFile.java:2685-2690` комментирует «flag = Equal = 12 | Plus = 20 | Minus = 36»
(десятичные 12=0x0C=8|4, 20=0x14=16|4, 36=0x24=32|4), то есть в редакторских
файлах бит `TARGET` выставлен вместе с оператором. `getTargetValue`
(`TriggerControl.java:384-395`): EQUAL → присвоить, PLUS → `base+value`,
MINUS → `base-value`; если бит TARGET — `value` сначала заменяется на
`levelInfo.getFlag(value)`. LOW: расхождение между комментарием KwdFile и enum
OpenKeeper не разрешено бинарником.

---

## 4. Специфические действия (кодировка полей HIGH, поведение MEDIUM/LOW)

Все смещения ниже — внутри 8-байтового `tdata` record `+0x08..+0x0f`,
цитируются по `KwdFile.java` (номера строк в таблице).

| Действие | Кодировка | Поведение | Confidence |
|---|---|---|---|
| `ZOOM_TO_ACTION_POINT` (26) | `actionPointId:u8, -:7` (`:2699-2704`) | камера к AP; `DK2.cpp` не реализует | HIGH bytes / LOW runtime |
| `DISPLAY_OBJECTIVE` (4) | `objectiveId:u32, actionPointId:u8, -:3` (`:2707-2711`) | `objectiveId` — числовой text id; если `actionPointId != 0` — simultанный zoom | HIGH / LOW |
| `PLAY_SPEECH` (24) | `speechId:u32, text:u8, introduction:u8, pathId:u16` (`:2713-2718`) | `speechId ≤ 32767`; `pathId` — camera path; `text=0` ⇒ показывать субтитры | HIGH / LOW |
| `DISPLAY_TEXT_STRING` (25) | `textId:u32, -:4` (`:2720-2724`) | text id, не строка; резолвинг в `.str` не восстановлен | HIGH / LOW |
| `FOLLOW_CAMERA_PATH` (31) | `pathId:u8, actionPointId:u8, available:u8, -:5` (`:2750-2755`) | `available`: 0 = показывать потолок, !0 = скрывать; path начинается с `actionPointId` | HIGH / MEDIUM |
| `ROTATE_AROUND_ACTION_POINT` (27) | `actionPointId:u8, available:u8, angle:u16, time:u32` (`:2778-2783`) | `available`: 0 = относительный, !0 = абсолютный угол; time в секундах | HIGH / MEDIUM |
| `FLASH_ACTION_POINT` (16) | `actionPointId:u8, available:u8, -:2, value:u32` (`:2761-2767`) | `value` = секунды; `value==0 && available!=0` = «до выключения» | HIGH / MEDIUM |
| `REVEAL_ACTION_POINT` (34) | `actionPointId:u8, available:u8, -:6` (`:2769-2774`) | 0 = reveal, !0 = conceal | HIGH / MEDIUM |
| `CREATE_HERO_PARTY` (14) | `partyId:u8, type:u8, -:2, actionPointId:u8, -:3` (`:2801-2809`) | `partyId + 1`; `type`: 0 none, 1 IP, 2 IP random; спавн в AP. В OpenKeeper: `spawnHeroParty(partyId, partyType, ActionPointToVector2f(ap))` (`TriggerControl.java:279-287`) | HIGH / MEDIUM |
| `SEND_TO_AP` (3) | `-:4, actionPointId:u8, -:3` (`:2827-2832`) | отправить объект/существо в AP | HIGH / LOW |
| `INFORMATION` (5) | `informationId:u32, actionPointId:u8, -:3` (`:2821-2825`) | info-id + AP | HIGH / LOW |
| `CREATE_CREATURE` (1) | `creatureId, playerId, level, flag:u8; posX,posY:u16` (`:2810-2818`) | `flag` = `CreatureFlag`; работает в OpenKeeper `TriggerControl.java:200-212` | HIGH / MEDIUM |
| `MAKE` (6) | `playerId, type, targetId, available:u8, -:4` (`:2678-2684`) | type = MakeType 0..5; availability флаг | HIGH / MEDIUM |
| `SET_ALLIANCE` (18) | `playerOneId, playerTwoId, available:u8, -:5` (`:2653-2659`) | original comment: 0 = create, !0 = break; OpenKeeper инвертирует (`available == 0` → create) | HIGH bytes / LOW runtime |

### Где искать text ids

`game/Data/text/` содержит `.str` и `.txt` банки; `strings.json` в Ghidra-export
пуст, поэтому numeric text id из trigger record **не может** быть сопоставлен с
конкретной строкой без запуска оригинальной игры. Это остаточный unknowns,
а не «текст отсутствует».

---

## 5. Готовые правила для реализации на C++

```cpp
// 1. Загрузить все 0xD5/0xD6 как плоский массив; 0 = "нет ссылки".
// 2. Построить дерево: root = TriggerRoot tagged object;
//    child(idChild) рекурсивно; sibling(idNext) — итеративно в том же родителе.
// 3. На каждом тике для текущего узла:
//    -- repeat != 255 && repeat > 0: --repeat;
//    -- обойти children, начиная с lastActiveIndex + 1:
//         если condition и ещё не найден active: computeActive();
//              при true → lastActive = this, next = this;
//         если action → execute() сразу; если repeat == 0 → detach action;
//    -- если children пуст и есть parent: next = parent, detach;
//    -- если next == nullptr: lastActive = -1, current = parent ? parent : root;
//       иначе current = next.
// 4. computeActive: target = flag/timer/level-time/...;
//    value = (flag == 1) ? literal : flagOrTimer(flagId/timerId);
//    применить comparison 1..6; comparison == 0 → false.
// 5. special: flag 128 = level score, timer 16 = time limit.
// 6. Действия с next: выполнять ВСЕ по порядку; не требовать, чтобы
//   чтобы условия были истинны.
```

Правила §5 реализованы в отдельном от движка evaluators'е
`engine/TriggerRuntime.h/.cpp` (см. §7). Текущий `engine/DK2.cpp` остался без
изменений: интеграция — задача вызывающей стороны.

| Файл | Статус |
|---|---|
| `engine/MissionTriggers.generated.hpp` | перегенерирован: все поля record, first-match комментарий, `findDK2TriggerNode`, `dk2MissionTriggersSelfTest` |
| `engine/TriggerRuntime.h/.cpp` | **новое**: first-match OR, child gating, repeat, comparison 0..6, flag literal/indirect, action requests |
| `engine/DK2.cpp:1451-1541` | не тронут; `condition_true` и `evaluate_triggers` всё ещё используют старую AND-логику и `>=` для всех типов |

---

## 6. Остаточные неизвестности

* AND/OR зафиксирован по OpenKeeper (MEDIUM), но **не доказан байт-в-байт**
  по оригинальному `TriggerControl`: в `DKII.EXE` не найдена function, которая
  однозначно читает `repeatTimes` и comparison-оператор из record. Ссылка на
  `WorldTrigger.cpp` доказывает существование подсистемы, но не её алгоритм.
* `flag` action-записей: конфликт кодировок (§3.3).
* Названия/содержимое text ids (`DISPLAY_TEXT_STRING`, `PLAY_SPEECH`,
  `INFORMATION`, `DISPLAY_OBJECTIVE`) не нарыты.
* Точная семантика `type` 1/2 в `CREATE_HERO_PARTY` (IP vs IP random)
  взята из комментария KwdFile; в `DKII.EXE` не проверена.

---

## 7. Состояние реализации (после добавления TriggerRuntime)

### 7.1. HIGH-confidence — зафиксировано и проверено на 9161 записи

| Факт | Проверка |
|---|---|
| Все поля record: `id`, `next`, `child`, `tag`, `repeat_times`, `type`, `tdata[8]`, `comparison`, `flag`, `value_id`, `text_id`/`speech_id`/`objective_id`, `path_id`, `action_point_id`, `player`, `target_id`, `pos_x/y`, `available` | `docs/triggers.json`; round-trip тест `python3 tools/gen_engine_data.py --self-test` сравнивает **все 21 поле каждой из 9161 записи** с сгенерированным C++ (66 карт, 3753 условия / 5408 действий) |
| `0` = «нет ссылки», `65535` = валидный id действия | 1299/1299 ненулевых `next`/`child` разрешаются; 9 `next` и 50 `child` указывают на `id == 65535` |
| Дерево: `child` = первый ребёнок, `next` = следующий брат в том же родителе | `TriggerLoader.parse`; все ссылки разрешимы; `chain_stats.roots` = 1099 вершин, не имеющих входящих ссылок |
| Первое активное условие в списке братьев выигрывает; действия в списке выполняются все | `TriggerControl.updateControl:96-139`; реализовано и покрыто тестами (приоритетный OR, child gating) |
| `repeat`: 255 = всегда, 0/1 = отсоединить действие после первого выполнения, N = N тиков | `TriggerGenericData.subRepeatTimes/isRepeateable`; тесты на 255/0/1/2 |
| Операторы 0..6, `NONE` → false | `TriggerGeneric.ComparisonType` + `TriggerControl.compare`; тесты на все 7 значений |
| `flag == 1` → литерал `value`, иначе косвенный `flagId/timerId` | `TriggerControl.isActive:157-161,172-176`; в данных 1677/1678 записей используют литерал, 1 (`level3`, `flag=2`, `value_id=4`) — косвенный |
| LEVEL_TIME всегда сравнивает с литералом `value` | `TriggerControl.java:179-182`; у type 34 нет байта `flag` |
| Special ids: flag 128 = level score, timer 16 = time limit | `TriggerControl.java:50-51`; константы в API |
| FLAG-действие: `EQUAL/PLUS/MINUS` + `TARGET` (0x008/0x010/0x020 + 0x004) | `TriggerControl.getTargetValue:384-396`; в данных биты 10 (VALUE\|EQUAL), 18 (VALUE\|PLUS), 12 (TARGET\|EQUAL), 20 (TARGET\|PLUS); эталонная реализация `applyFlagAction()` |
| C++ self-test таблицы | `dk2MissionTriggersSelfTest()` в сгенерированном заголовке: 66 / 9161 / 3753 / 5408 + проверка всех ссылок |

### 7.2. MEDIUM — эталонная гипотеза, реализованная как есть

* Точная последовательность обхода: текущий узел → дети (начиная с `lastTriggerIndex+1`)
  → первый активный ребёнок становится текущим на следующем тике; при пустом
  списке детей узел отсоединяется и управление возвращается родителю; при
  отсутствии активного ребёнка сброс `lastTrigger` и возврат к родителю/руту.
  Воспроизведено в `TriggerRuntime::step()`.
* Родительские `repeat_times` управляют отсоединением **дочерних действий**
  (`trigger.isRepeateable()` в `TriggerControl:117`), а не `repeat_times` самой
  action-записи. Это следует из кода, но не доказано байт-в-байт по `DKII.EXE`.
* Начальное состояние — синтетический root с детьми = цепочка `root id → next`.
  В оригинале root привязан к тегу объекта (`TriggerRoot`), в `triggers.json`
  таких тегов нет, поэтому по умолчанию `deriveRoots()` берёт вершины без
  входящих ссылок; 6 пустых карт не имеют корней вовсе.

### 7.3. Остаточные unknowns (HIGH-confidence пока недостижимо)

* **Текстовые id.** Numeric id из `PLAY_SPEECH` (`speech_id`), `DISPLAY_OBJECTIVE`
  (`objective_id`), `INFORMATION` (`information_id`) и `DISPLAY_TEXT_STRING`
  (`text_id`) не сопоставлены со строками: `game/Data/text/*.str` не дают
  однозначного сопоставления без запуска оригинальной игры. Runtime передаёт id
  как число в `ActionRequest::text_id`, резолвинг — за игрой.
* **Action points / camera paths.** Числовые id есть, но реестр AP и таблица
  путей камеры живут в рантайме оригинала; `ITriggerContext::resolveActionPoint()`
  — необязательный хук для игры. Actions 26/27/31 требуют AP registry, которого
  в статических данных нет.
* **Алгоритм оригинального `DKII.EXE`.** Функция, однозначно читающая
  `repeatTimes` и comparison-оператор из record, не найдена; строка
  `WorldTrigger.cpp` и поля `TriggerRoot`/`TriggerRootRepeatCount` доказывают
  существование подсистемы, но не её алгоритм. Поэтому §3 и §7.2 остаются
  MEDIUM по отношению к бинарнику, даже будучи HIGH по отношению к OpenKeeper.
* **Конфликт кодировки `flag` action** (§3.3), семантика `type` 1/2 в
  `CREATE_HERO_PARTY`, поведение `SET_ALLIANCE` (OpenKeeper инвертирует
  комментарий KwdFile) — требуют проверки на оригинальном рантайме.

---

## 8. Публичный API `engine/TriggerRuntime.h` и точки интеграции

### 8.1. Сигнатуры

```cpp
namespace dk2trigger {

enum class Comparison : std::uint8_t {
    kNone = 0, kLess = 1, kLessEqual = 2, kEqual = 3,
    kGreater = 4, kGreaterEqual = 5, kNotEqual = 6,
};
bool compareValues(float target, Comparison op, float value);

struct ActionRequest {
    unsigned node_id, type, flag, flag_id, value_id, text_id, path_id,
             action_point_id, player, target_id, available, repeat_times;
    float value, pos_x, pos_y;
};

struct ITriggerContext {
    virtual float getFlag(unsigned id) = 0;
    virtual float getTimer(unsigned id) = 0;
    virtual float getLevelTime() = 0;
    virtual float getTargetValue(unsigned target_type, unsigned target_id,
                                 unsigned player) = 0;
    virtual float getTimeLimit();
    virtual float getLevelScore();
    virtual bool resolveActionPoint(unsigned id, float& x, float& y);
    virtual void onAction(const ActionRequest& request);
    virtual void onUnsupported(unsigned node_id, unsigned type, const char* what);
    virtual void setFlag(unsigned id, float value);
    virtual void setTimer(unsigned id, float value);
    virtual void setLevelScore(float value);
    virtual ~ITriggerContext();
};

class FunctionalTriggerContext final : public ITriggerContext {
public:
    std::function<float(unsigned)> get_flag, get_timer;
    std::function<float()> get_level_time, get_time_limit, get_level_score;
    std::function<float(unsigned, unsigned, unsigned)> get_target_value;
    std::function<bool(unsigned, float&, float&)> resolve_action_point;
    std::function<void(const ActionRequest&)> on_action;
    std::function<void(unsigned, unsigned, const char*)> on_unsupported;
    std::function<void(unsigned, float)> set_flag, set_timer;
    std::function<void(float)> set_level_score;
};

float resolveFlagActionValue(const ActionRequest& request, ITriggerContext& ctx);
void applyFlagAction(const ActionRequest& request, ITriggerContext& ctx);

class TriggerRuntime {
public:
    explicit TriggerRuntime(const DK2MissionTriggers& set);
    TriggerRuntime(const DK2MissionTriggers& set, std::vector<unsigned> roots);
    static std::vector<unsigned> deriveRoots(const DK2MissionTriggers& set);
    void reset();
    void setRoots(std::vector<unsigned> roots);
    std::vector<ActionRequest> evaluate(ITriggerContext& ctx);
    void evaluateAndDispatch(ITriggerContext& ctx);
    const DK2MissionTriggers& set() const;
    const std::vector<unsigned>& roots() const;
    const RuntimeStats& stats() const;
    unsigned currentNode(unsigned root_id) const;
};

}  // namespace dk2trigger
```

`RuntimeStats` (ReadOnly): `ticks`, `condition_checks`, `actions_emitted`,
`nodes_detached`, `unsupported`.

В сгенерированном заголовке доступны:
`findDK2TriggerNode(set, id)` и
`dk2MissionTriggersSelfTest(&maps, &nodes, &conditions, &actions)`.

### 8.2. Интеграция в `engine/DK2.cpp` (без изменений в этой задаче)

1. `#include "TriggerRuntime.h"` рядом с `#include "MissionTriggers.generated.hpp"`.
2. В `Game` заменить `std::set<unsigned> fired_actions_` на
   `std::unique_ptr<dk2trigger::TriggerRuntime> triggers_;` (или `std::optional`).
3. При загрузке карты выбрать Set по `state_.mission_name` (как сейчас в
   `evaluate_triggers`), создать `TriggerRuntime(set, TriggerRuntime::deriveRoots(set))`.
   Если известен `TriggerRoot` из тега объекта — передать явный список root id.
4. Реализовать адаптер `ITriggerContext` (можно `FunctionalTriggerContext`) поверх
   `GameState`: `getFlag`→`s.flags[id]`, `getTimer`→`s.timers[id]`,
   `getLevelTime`→`s.elapsed`, `getTimeLimit`→`s.time_limit`,
   `getTargetValue`→PLAYER_GOLD/PLAYER_CREATURES/PLAYER_ROOMS/PLAYER_KILLED и др.,
   `setFlag`/`setTimer`→те же массивы.
5. В `Game::step()` заменить вызов `evaluate_triggers()` на
   `triggers_->evaluateAndDispatch(*context_)`; `onAction` повторяет текущий
   `switch (action->type)` из `DK2.cpp:1508-1539`, но работает с
   `ActionRequest` (для `FLAG` можно вызвать `applyFlagAction()`).
6. Удалить статические `find_node`/`condition_true` и `fired_actions_`: реестр
   сработавших действий теперь хранится внутри рантайма как `detached`-узлы и
   `repeat_left`.

Порядок полей `DK2TriggerNode` стабилен и используется и генератором, и
Python self-test'ом (`TRIGGER_FIELDS` в `tools/gen_engine_data.py`).
