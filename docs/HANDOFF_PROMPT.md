# Handoff prompt (paste into next chat)

Copy from “=== START ===” inclusive.

=== START ===
Контекст проекта: Exploration Lite RE в /home/workdir/artifacts/ExplorationLite_RE/.

Уже сделан полный аудит реализации vs бинарник (2026-11-02). Не перегоняй анализ с нуля.

Читай в первую очередь:
- docs/AUDIT_FULL_2026-11-02.md
- docs/COMPATIBILITY.md
- docs/TODO.md
- docs/HANDOFF_PROMPT.md (этот файл)

## Текущий приоритет пользователя (строго)
1. Нормальный **оригинальный рендеринг HUD** (UIDocument, hud_*, Font, ortho overlay, hit-test).
2. Починить и сделать **функциональным инвентарь / меню** (palette, SelectPaletteItem/Page, message routing, UpdatePaletteUI-подобные пути).

Всё остальное (SetCell neighbours, NeedFace 2/3, AddBlockFace, camera, water tables, trees) — **позже**, только по явной просьбе.

## Краткий статус self-made / critical gaps (из аудита)
- SetCell: thin write; нет 16× neighbour SetState |=0xC000 и gate this+0xf0 → **Incorrect side-effects**
- NeedFace: type-id heuristic; return 2/3 Missing
- AddBlockFace: Missing → EmitFace Self-made
- Camera: FPS yaw/pitch **Incorrect** vs UpdateCameraTransform 0x5b580
- IsFlowAllowed: control flow OK; special tables Self-made fallback
- Min/MaxWaterHeight, FillCells, EncodeWorldPos, LCG, tree IDs/height: Verified
- HUD/inventory: Partial — hit-test и SelectPalette* есть; полного меню/state machine нет

## Правила работы
- Не изобретай «чтобы заработало» поверх оригинала, если есть recovery в pseudocode/recovered/ или pdg_key/.
- Любую самодеятельность явно помечай в коде и в docs.
- Перед крупным рефактором core-world — спроси; сейчас фокус UI.
- Рабочая директория: /home/workdir/artifacts/

Начни с текущего состояния HUD + inventory (UIElement, Game message paths, Scene/UIDocument, Font) и скажи, что именно чинить первым для оригинального HUD и рабочего инвентаря.
=== END ===
