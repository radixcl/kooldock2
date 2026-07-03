# Flash de blur a pantalla completa durante la navegación — investigación completa

> Estado: **causa raíz identificada y confirmada en el fuente de KWin.** No hay
> fix client-side posible en KWin sin `ext_background_effect_v1`. Código en
> baseline (`4b8303c`); todo lo intentado fue revertido.

## Síntoma

Navegando con el mouse sobre el dock, intermitentemente y por un frame el blur
se ve a pantalla completa en lugar de limitarse al pill. El pill "desaparece"
en ese frame (en realidad es el escritorio borroso a pantalla completa lo que
se ve; el pill semitransparente se pierde sobre el fondo).

## Causa raíz (definitiva)

Es una **limitación de KWin con el protocolo viejo `org_kde_kwin_blur`**, no un
bug nuestro. Nuestra región de blur siempre es correcta (≤ ~0.5 del eje largo);
KWin borra la ventana entera por su cuenta.

Cadena exacta, leída del fuente de KWin 6.6 (`Plasma/6.6`):

1. **El efecto solo re-lee la región al *crear* un objeto blur nuevo.**
   `SurfaceInterfacePrivate::setBlur()` (src/wayland/surface.cpp) marca el campo
   `SurfaceState::Field::Blur` como dirty, y eso solo ocurre en
   `org_kde_kwin_blur_manager.create` / `.unset`. Un `set_region`+`commit` sobre
   un objeto existente **se ignora** (no marca el campo → `blurChanged` no se
   emite → el efecto no actualiza). ⇒ **para cambiar la región hay que recrear
   el objeto.**

2. **Crear el objeto deja un instante con región vacía.** El protocolo es
   `create(surface)` (que ya hace `setBlur(objeto)` con `currentRegion` vacía),
   y recién después `set_region` + `commit`. Entre `create` y `commit` el objeto
   tiene región vacía.

3. **Región vacía = ventana entera.** En el efecto (src/plugins/blur/blur.cpp),
   `updateBlurRegion()` hace `content = surface->blur()->region()` (toma la
   región tal cual, vacía incluida) y `blurRegion()` hace
   `if (content->isEmpty()) region = w->contentsRect();` → **toda la ventana**,
   que en nuestro caso es full-screen.

4. **Carrera gui-thread / render-thread.** Nuestras requests de blur salen en el
   gui thread (`afterAnimating`); el `wl_surface.commit()` de Qt sale en el
   render thread. De vez en cuando un commit de superficie cae justo en el
   instante "objeto creado, región todavía vacía" → KWin lee vacío → flash.
   Por eso es **intermitente**, y por eso ocurre **más seguido** cuanto más
   recreamos (más navegación = más creates = más ventanas de carrera).

`KWindowEffects::enableBlurBehind` (KWindowSystem, ruta legacy) hace exactamente
este recreate-por-update, por eso baseline parpadea. El protocolo nuevo
`ext_background_effect_v1` sí permite actualizar la región in-place sobre un
objeto persistente (sin recrear) → no parpadearía, **pero KWin 6.6.4 no lo
expone** (verificado con `wayland-info`: solo `org_kde_kwin_blur_manager` v1).

## Cómo se estableció (experimentos)

- **Blur off → no hay glitch; blur on → glitch.** Es enteramente el blur.
- Instrumentando cada push: la región que enviamos **nunca** supera ~0.5 del eje
  largo. El full-screen no sale de nuestra región.
- **Congelar la región (sin actualizar nunca) → no hay flash.** ⇒ lo dispara la
  *actualización* de región.
- Bajar el ritmo a 15fps **no** lo elimina (y agrega lag). No es "KWin no da
  abasto" / throttle.
- Ocurre **con y sin auto-hide**.

## Qué se intentó y falló

1. **Frame-alignment + rate-limit + dedup** (ya en baseline, commit `4b8303c`):
   reduce pero no elimina.
2. **Fix B: el timer no empuja durante render activo.** Redujo pushes de timer,
   no eliminó el flash (no era el timer).
3. **Bajar ritmo a 15fps + cuantizar región.** No eliminó; agregó lag.
4. **Objeto `org_kde_kwin_blur` persistente** (vía KWayland, y luego vía binding
   nativo de QtWayland): **no renderiza** — KWin ignora el `set_region` in-place
   (ver causa raíz #1). Descarta de cuajo el enfoque "persistente" en este KWin.
5. **Recrear el objeto + release diferido del viejo.** Renderiza y sigue al pill,
   pero el flash **persiste e incluso aumenta** — la carrera no es el release del
   viejo (causa raíz #4) sino el create-con-región-vacía del nuevo.

## Qué se descartó (no re-investigar)

- `m_blurTimer` phase-drift / región stale (sus valores son correctos).
- El camino del slide de auto-hide (el flash ocurre con auto-hide off).
- El ritmo de push / "KWin no da abasto".
- Encoger la ventana: **imposible**, debe ser full-screen para los tooltips
  dinámicos (se cortarían) y para arrastrar iconos fuera del dock.

## Opciones restantes

> Actualización 2026-07-02: `ext_background_effect_v1` **ya se mergeó en KWin**
> (MR !4890, 2026-01-28, milestone Plasma 6.7) y **Plasma 6.7 salió el
> 2026-06-16**. KWindowSystem implementó el lado cliente en KF ~6.22
> (2026-02-13); el KF 6.24 instalado en esta máquina **ya lo trae** (verificado
> con `strings` sobre `KF6WindowSystemKWaylandPlugin.so`). O sea:
> `enableBlurBehind` (lo que ya usa baseline) pasa a actualizar la región
> in-place apenas KWin ≥ 6.7 exponga el global → el flash desaparece **sin
> tocar código**. Ubuntu 26.04 arm64 sigue en KWin 6.6.5 y el PPA
> kubuntu-ppa/backports aún no empaquetó 6.7 (verificado 2026-07-02).

1. **Esperar el backport de Plasma 6.7** (kubuntu-ppa/backports) y no tocar
   nada. Cero código, cero riesgo; la espera es acotada (semanas).
2. **Aceptar el flash** mientras tanto (estado actual / baseline).
3. **Self-blur en QML** — `ShaderEffect` sobre el wallpaper detrás del pill.
   Elimina el flash hoy, pero es mantenimiento permanente para un beneficio que
   expira cuando llegue 6.7. Solo si el flash resulta insoportable.
4. Compilar KWin 6.7 a mano en 26.04: posible pero pesado (arrastra deps de
   Plasma 6.7). No recomendado.
5. **Hack A: push desde el render thread** ← **implementado 2026-07-02, en
   prueba.** Ver sección siguiente.
6. **Hack B: ventana de blur separada** (no implementado, plan B). Ver sección
   siguiente.

## Hacks client-side (2026-07-02)

Releyendo la causa raíz #4, la carrera es puramente de *hilos del cliente*: el
triple `create → set_region → commit(blur)` sale del gui thread mientras el
`wl_surface.commit` de Qt sale del render thread (en `swapBuffers`). libwayland
lockea por request, así que un commit puede colarse *entre* el `create` (región
vacía) y el `blur.commit`. La ventana de carrera es nuestra, no de KWin ⇒ se
puede cerrar del lado cliente sin protocolo nuevo.

### Hack A — emitir el triple desde el render thread (implementado)

Hablar `org_kde_kwin_blur` crudo (binding generado de
`plasma-wayland-protocols/blur.xml`) y emitir `create → set_region → commit`
desde el **render thread**, en `QQuickWindow::afterRendering`
(`Qt::DirectConnection`) — justo antes del `wl_surface.commit` del swap de ese
mismo hilo. Mismo hilo ⇒ ningún commit puede partir el triple ⇒ KWin nunca ve
el instante "objeto nuevo, región vacía". Determinista, no probabilístico.

Detalles que importan:

- **Release diferido**: el objeto blur viejo se libera recién en el push
  *siguiente* (lección del intento #5 — liberar en el mismo batch puede dejar
  el estado current de la superficie apuntando a un objeto muerto un frame).
- El gui thread solo deja `region + wl_surface* + flags` bajo mutex
  (`queueBlurPush`) y fuerza un frame con `m_view->update()` para garantizar
  que el push y su commit salgan (cubre el trailing flush del timer y el path
  de disable).
- El lookup del `wl_surface` (`KWayland::Client::Surface::fromWindow`) se hace
  en el gui thread; el render thread recibe el puntero crudo.
- El registry se bindea una vez en el gui thread (patrón de
  `waylandwindowtasks.cpp`); si `org_kde_kwin_blur_manager` no está, cae al
  path viejo de `KWindowEffects::enableBlurBehind`.
- Throttle/dedup existentes quedan intactos (siguen siendo necesarios: cada
  recreate hace regenerar el backbuffer de blur de KWin).

Código: `initBlurProtocol()` / `queueBlurPush()` / `renderPushBlur()` en
`src/kooldock.cpp`; blur.xml agregado a la generación de protocolos en
`src/CMakeLists.txt`. Log de activación: "render-thread blur push active".

Nota: cuando llegue KWin 6.7 convendría preferir de nuevo
`KWindowEffects::enableBlurBehind` (usa `ext_background_effect_v1`, sin
recreates ni regeneración de backbuffer). El fallback ya existe; bastaría
invertir la preferencia o borrar el hack.

### Hack B — ventana de blur separada (no implementado, plan B)

Una segunda ventana chica, del tamaño exacto del pill, detrás del dock, con
blur de **región vacía intencional**: KWin trata región vacía como "toda la
ventana" (`contentsRect()`), así que el blur sigue solo con mover/redimensionar
la ventana — **cero recreates del objeto blur** ⇒ sin carrera. Contras que lo
degradan a plan B: dos superficies wayland no tienen sincronía de frames, así
que durante el zoom (pill cambiando a 60fps) el blur se desalinearía del pill
visible; más gestión de stacking/input. Solo intentar si el Hack A falla.

## Punteros al fuente

- KWin efecto: `src/plugins/blur/blur.cpp` — `updateBlurRegion()`, `blurRegion()`
  (`content->isEmpty() → contentsRect()`).
- KWin protocolo server: `src/wayland/blur.cpp` (`BlurInterfacePrivate`,
  `org_kde_kwin_blur_commit` → `currentRegion = pendingRegion`),
  `src/wayland/surface.cpp` (`setBlur()` marca `Field::Blur`).
- Cliente (recreate-per-update): KWindowSystem
  `src/platforms/wayland/windoweffects.cpp` (`installBlur()`).
