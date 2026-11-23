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

Важное следствие для `engine/DK2.cpp`: `next = 65535` в сгенерированном
`MissionTriggers.generated.hpp` означает «нет ссылки», потому что генератор
заменил `0` на `0xFFFF`; в оригинале `0` = нет. HIGH.

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

Минимальные изменения в текущем порту ( без изменения кода в этой задаче ):

| Файл | Что исправить |
|---|---|
| `engine/MissionTriggers.generated.hpp:2` | убрать комментарий «next = AND-chain»; хранить `comparison`, `flag`, `flagId`, `repeat_times`, `text_id`, `path_id` (сейчас их нет в `DK2TriggerNode`) |
| `engine/DK2.cpp:585-597` | `condition_true` должен принимать `comparison` и источник `value` |
| `engine/DK2.cpp:616-625` | first-match по братьям вместо «все conditions должны быть true» |
| `engine/DK2.cpp:613-651` | учитывать `repeat_times`; выполнять `child` conditions (вложенные), а не только прямое действие |
| `engine/DK2.cpp` (новое) | action point registry и camera path/party spawn для действий §4 |

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
