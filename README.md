# renta-fija

Seguimiento y backtesting del **momento de pasar de un fondo monetario a uno de
renta fija a 2 años**, con el cuadro de cinco indicadores de
[explicacion.md](explicacion.md). C++17 con SDL3 y Dear ImGui (vendorizado en
`src/vendor/imgui`), como `msmoney`, `pensionplan` y `dca`.

Los indicadores se recogen solos: al arrancar, el programa pide al
[portal de datos del BCE](https://data.ecb.europa.eu) lo publicado desde la
última vez (curva de deuda AAA de la zona euro, €STR y EONIA), en un hilo
aparte, y lo guarda en `data/series/*.csv`. La primera descarga son unos 6 MB
con todo el histórico desde septiembre de 2004; las siguientes, unas líneas.

## Compilar y ejecutar

```sh
cmake -S . -B build && cmake --build build -j
./build/rentafija          # la aplicación (vale lanzarla desde cualquier carpeta)
ctest --test-dir build     # tests del motor, sin red ni ventana
./build/rf_probe           # lo mismo que la app, por terminal, descargando del BCE
./build/rf_probe data      # ídem con la caché de la app
```

Hace falta SDL3 (por `pkg-config`), `curl` en el PATH y la fuente DejaVu Sans.
`config.ini` (opcional) dice dónde va la carpeta de datos y si se actualiza al
arrancar.

## Las pestañas (`1`-`6`, `F5` actualiza)

- **Monitor** — el cuadro de control de hoy: los cinco indicadores con su
  estado (desfavorable 0, neutral 1, favorable 2), la lectura en palabras, la
  puntuación sobre 10 y la decisión. Incluye el control semanal del 2 años y la
  subida de tipos que anularía la prima.
- **Histórico** — tipos, pendiente, expectativas, prima, tendencias y los dos
  fondos sintéticos, con la puntuación diaria debajo.
- **Señal** — el backtest que pide el método: en cada revisión mensual (o
  semanal) se anota la puntuación y lo que hizo después la renta fija menos el
  monetario a 6, 12 y 24 meses. Tablas por puntuación, por estado de cada
  indicador y por quintiles del valor bruto de cada indicador (los «niveles
  empíricos» con los que sustituir los umbrales heurísticos).
- **Estrategia** — la regla de decisión operada de verdad contra «siempre
  monetario», «siempre renta fija» y 50/50: TAE, caída máxima, volatilidad,
  tiempo en renta fija y lista de operaciones.
- **Umbrales** — la estrategia para cada pareja (puntuación de entrada,
  puntuación de salida), en un mapa de calor; un clic aplica la pareja. En el
  panel izquierdo se editan los umbrales de cada indicador.
- **Datos** — estado de las once series y el cuaderno de observaciones,
  exportable a CSV (`data/observaciones.csv`).

Todos los parámetros se guardan en `data/renta-fija.cfg` y todo se recalcula al
cambiar cualquiera de ellos.

## Cómo se calcula cada indicador

Todos los tipos son los rendimientos al contado (cupón cero, capitalización
continua) de la curva de deuda pública AAA de la zona euro del BCE.

| # | Indicador | Cifra | Favorable (2) | Desfavorable (0) |
|---|---|---|---|---|
| 1 | Tendencia del 2 años | variación en 3 meses | ≤ −15 pb, y el último mes no sube más de 10 pb | ≥ +15 pb |
| 2 | Pendiente 2 a − 3 m | variación en 3 meses y su causa | aumenta ≥ 10 pb porque baja el 3 meses | aumenta porque el 2 años sube ≥ 10 pb, o se reduce con el 3 meses y el 2 años subiendo |
| 3 | Expectativas del BCE | tipo a 3 meses implícito a 6 y 12 meses menos el de hoy | ≤ −25 pb a 12 meses | ≥ +25 pb a 6 o 12 meses |
| 4 | Prima frente al monetario | rendimiento neto del fondo − monetario esperado a 12 meses | > +0,50 pp | < 0 |
| 5 | Tendencia del 10 años | variación en 3 meses | ≤ +10 pb | > +25 pb |

Decisión: **≥ 8 entrada completa, 5-7 entrada parcial, ≤ 4 seguir en
monetario**. Los umbrales de los indicadores 1, 4 y 5 y los tramos de decisión
salen del evaluador de `explicacion.md`; los de 2 y 3, que allí solo se
describen con palabras, son una traducción a números. Todos son editables.

Dos cosas se apartan de la letra del método porque los datos no existen gratis:

- **Indicador 3.** Los forward OIS del €STR son datos de mercado de pago. Se
  usa el tipo forward implícito en la curva AAA (`f₁₂ = (1,25·y₁₅ₘ − 1·y₁₂ₘ) /
  0,25`), comparado con el 3 meses de la misma curva. Mide lo mismo —qué
  movimiento del BCE descuenta el mercado— pero sobre deuda pública.
- **Indicador 4.** El monetario esperado es el €STR de hoy más el movimiento
  que la curva descuenta entre 3 y 12 meses (`y₁ₐ − y₃ₘ`), menos gastos.

## Los fondos del backtest

No se usan valores liquidativos de fondos reales (no hay ninguno con historia
limpia desde 2004), sino dos índices sintéticos:

- **Monetario**: el €STR capitalizado a diario (act/360) menos gastos. Antes de
  octubre de 2019, EONIA − 8,5 pb, el diferencial fijo de conversión del BCE.
- **Renta fija**: un bono cupón cero de la duración indicada (2 años por
  omisión) renovado cada día sobre la curva AAA —cupón, descenso por la curva y
  variación de precio—, más un diferencial opcional, menos gastos.

Para acercarlo a un fondo concreto (la «sexta métrica» del método), se pone su
duración modificada, sus gastos y, como diferencial, la TIR de su cartera menos
el tipo AAA al mismo plazo. Un fondo con crédito o deuda periférica rinde más
y cae más en las crisis de lo que dice este modelo.

Las órdenes se ejecutan al día siguiente de la revisión: el BCE publica cada
curva la mañana posterior, así que la señal no se habría podido operar el día
que lleva por fecha.

## Cómo leer los resultados

- Las ventanas a 12 y 24 meses se solapan: 250 revisiones mensuales son unos
  veinte periodos independientes. Una diferencia de dos décimas no es evidencia.
- Desde 2004 hay pocos ciclos de tipos completos, y uno de ellos (2022) pesa
  mucho. Para ajustar umbrales sin engañarse, se acota el periodo con «Hasta»,
  se eligen, y se comprueba con «Desde» en los años restantes.
- La puntuación del día puede moverse con el ruido del ajuste diario de la
  curva; la decisión se toma en las revisiones, no a diario.

## Fuentes

```
model.h/.cpp    calendario, series en caché, tabla diaria de mercado
engine.h/.cpp   indicadores, puntuación, fondos sintéticos y backtests (con tests)
ecb.h/.cpp      descarga del portal de datos del BCE (por curl)
updater.h/.cpp  actualización en segundo plano
ui.h/.cpp       estado, paleta, gráfica y marco de la ventana
tabs.cpp        las seis vistas
main.cpp        SDL y el bucle de eventos
tools/rf_probe.cpp, tests/engine_tests.cpp
```

Variables de depuración: `RF_SHOT=fichero.bmp` guarda una captura y sale,
`RF_TAB=0..5` abre en esa pestaña, `RF_NO_UPDATE=1` no toca la red.

No es asesoramiento financiero: es una herramienta para comprobar con datos si
una regla habría funcionado.
