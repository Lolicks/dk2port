# ANIM-анимация: экспорт кадров

Реализовано в `decoder/kmf/anim.c`, CLI: `dk2tool anim <file.kmf> <outdir> [frames]`.

## Алгоритм (источники)

Из `research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/KmfFile.java:425-475`
и `KmfModelLoader.java:325-352`:

```
geomBase   = ITAB[frame >> 7][itab_index]      // ITAB: chunks × indexCount u32
offset     = VGEO[itab_index][frame]           // VGEO: indexCount × frameCount u8
geomIndex  = geomBase + offset
coord      = GEOM[geomIndex]                    // 10 бит на ось, ×scale
```

`GEOM` — 5 байт на запись: u32 (Z10|Y10|X10) + u8 `frameBase`.
`VERT` ANIM-вершина (18 байт): uv u16,u16 + normal 12 + `itab_index` u16 (offset 16).
Интерполяция между соседними GEOM-записями по `frameBase` добавлена как
реконструкция (OpenKeeper использует morph targets с весами, не прямой lerp).

## Проверка

```
$ dk2tool anim build/KMF/Mis-Idle1.kmf build/anim_test 4
anim frames written: 4
$ head -3 build/anim_test/frame_0000.obj
v -0.044137 -0.023154 -0.277122
$ head -3 build/anim_test/frame_0001.obj
v -0.043413 -0.023877 -0.277122     # координаты меняются
```

`g_Maiden-Torture` меняется только на последнем кадре (VGEO-смещение появилось
только на frame 100) — это свойство самого файла, а не ошибка декодера.

## Ограничения

- Экспортируются позиции LOD0; нормали и UV в OBJ не переносятся.
- Прямая интерполяция — реконструкция; для точного совпадения с движком нужен
  morph-target путь из `KmfModelLoader.java` (веса `FrameInfo`).
- Все 1145 ANIM-файлов доступны для экспорта; в iOS-рантайме анимация ещё
  не подключена.
