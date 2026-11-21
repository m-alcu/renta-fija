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

La señal se da para un **fondo real**: por omisión el iShares Euro Investment
Grade Corporate Bond Index Fund (IE) Institutional Acc EUR (`IE00B67T5G21`).
Sus valores liquidativos diarios se bajan de Finect en la misma actualización,
como en `msmoney` y `fondos-openbank`, y se guardan en
`data/series/fondo_<ISIN>.csv`; el backtest compra y vende ese fondo con su
valor liquidativo desde el primero publicado (febrero de 2012).

## Compilar y ejecutar

```sh
cmake -S . -B build && cmake --build build -j
./build/rentafija          # la aplicación (vale lanzarla desde cualquier carpeta)
ctest --test-dir build     # tests del motor, sin red ni ventana
./build/rf_probe           # lo mismo que la app, por terminal, descargando del BCE
./build/rf_probe data      # ídem con la caché de la app
```

Hace falta SDL3 (por `pkg-config`), `curl` en el PATH y la fuente DejaVu Sans.
`config.ini` (opcional) dice dónde va la carpeta de datos, si se actualiza al
arrancar y qué fondo se sigue (`fondo = <ISIN>`, o `no` para quedarse solo con
el bono AAA sintético). `./build/rf_probe data <ISIN>` hace lo mismo con otro
fondo.

## Las pestañas (`1`-`6`, `F5` actualiza)

- **Monitor** — el cuadro de control de hoy: los cinco indicadores con su
  estado (desfavorable 0, neutral 1, favorable 2), la lectura en palabras, la
  puntuación sobre 10 y la decisión. Incluye el control semanal del 2 años, la
  subida de tipos que anularía la prima, el fondo (valor liquidativo,
  variación a 1 y 3 meses y a 1 y 3 años, rendimiento estimado) y **«si entras
  hoy: qué pasó otras veces»**: el exceso medio del fondo sobre el monetario a
  6, 12 y 24 meses, y cuántas veces ganó, en las revisiones pasadas con la
  misma puntuación y con la misma decisión que hoy.
- **Histórico** — tipos, pendiente, expectativas, prima, tendencias y los
  fondos (el monetario, el fondo real y el bono AAA que mejor lo imita), con la
  puntuación diaria debajo.
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
- **Datos** — estado de las once series del BCE y de los VL del fondo, y el
  cuaderno de observaciones, exportable a CSV (`data/observaciones.csv`).

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

- **Monetario**: sintético, el €STR capitalizado a diario (act/360) menos
  gastos. Antes de octubre de 2019, EONIA − 8,5 pb, el diferencial fijo de
  conversión del BCE.
- **Renta fija, fondo real** (por omisión): el valor liquidativo del fondo de
  `config.ini`, ya neto de gastos, en los días con curva del BCE (un día sin VL
  conserva el anterior). El backtest empieza con su primer VL y termina con el
  último, así que con `IE00B67T5G21` cubre de 2012-02 en adelante.
- **Renta fija, bono AAA sintético**: un bono cupón cero de la duración
  indicada (2 años por omisión) renovado cada día sobre la curva AAA —cupón,
  descenso por la curva y variación de precio—, más un diferencial opcional,
  menos gastos. Cubre desde 2004.

Se elige en el panel «Fondo de renta fija» de Señal, Estrategia o Umbrales.

Con el fondo real, la rentabilidad sale del VL, pero la prima (indicador 4) y
el margen de seguridad necesitan una TIR que Finect no publica. Se estima como
el tipo AAA a la duración del fondo más un diferencial, menos gastos. El botón
«Ajustar al histórico del fondo» los calcula del propio VL:

- **duración**: la del bono AAA cupón cero cuya variación de tipos explica las
  rentabilidades semanales del fondo con una pendiente igual a esa misma
  duración. `IE00B67T5G21` se mueve como un AAA a 4,25 años (R² 0,53: el resto
  es el diferencial de crédito, que se mueve por su cuenta).
- **diferencial**: lo que el fondo le ha sacado a ese bono AAA en todo su
  histórico (+1,83 pp al año en `IE00B67T5G21`), más los gastos (0,15 %).

Dos avisos: el diferencial no es constante (−2,4 pp en 2022, +3,3 pp al año en
2012-14) y fijarlo con todo el histórico mira al futuro, así que el indicador 4
del fondo real describe el fondo, no lo que se sabía en cada fecha. Y el método
está pensado para un fondo a 2 años de deuda pública: un fondo de crédito a
4 años cae más con las subidas de tipos y también cuando se amplían los
diferenciales (marzo de 2020), algo que ninguno de los cinco indicadores mide.

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
finect.h/.cpp   valores liquidativos del fondo real, de Finect por ISIN
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
