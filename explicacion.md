Para detectar cuándo conviene pasar de un fondo monetario a uno de renta fija de 2 años, establecería un cuadro de control mensual con 5 indicadores principales.

Mi objetivo no sería adivinar el día exacto en que los tipos alcanzan su máximo, sino detectar cuándo la relación entre rentabilidad esperada y riesgo empieza a favorecer a la renta fija de 2 años.
1. Los cinco indicadores que seguiría
Indicador	Qué observar	Señal favorable para entrar
1. Tipo del bono a 2 años	Variación en 1 y 3 meses	Deja de subir y empieza a estabilizarse
2. Pendiente 2 años − 3 meses	Nivel y evolución de la curva	Se estabiliza o aumenta por descenso de tipos cortos
3. Expectativas del BCE	Tipos implícitos a 6 y 12 meses	Desaparecen las expectativas de subidas
4. Prima frente al monetario	Rendimiento esperado del bono frente al €STR	Compensa el riesgo adicional de duración
5. Tendencia de tipos a 10 años	Variación en 1 y 3 meses	Se estabilizan los tipos largos

La combinación es más importante que cualquiera de los indicadores individualmente.
2. Cómo interpretaría cada indicador
Indicador 1 — Tendencia del rendimiento del bono a 2 años
Es el que más peso le daría.
- Si el rendimiento sube de forma sostenida, el fondo puede seguir perdiendo valor.
- Si se estabiliza durante varias semanas, disminuye la presión sobre el precio.
- Si comienza a bajar, el fondo se revaloriza, aunque parte de la oportunidad puede haber pasado.
Utilizaría la variación en 3 meses como señal principal y en 1 mes como confirmación.
Indicador 2 — Pendiente de la curva a corto plazo
\[
S_{2a-3m}=Y_{2a}-Y_{3m}
\]

Es especialmente útil porque estás valorando pasar de instrumentos monetarios a bonos de 2 años.

Por ejemplo, una pendiente que pase de +20 a +50 puntos básicos podría ser favorable, pero solo si comprendemos por qué ha aumentado. Si ambos rendimientos suben y el de 2 años sube más, puede ser una señal desfavorable.

No compraría simplemente porque la curva se positivice.
Indicador 3 — Tipos futuros descontados por el mercado
Compararía el €STR actual con los tipos forward OIS a 6 y 12 meses.

Una reducción progresiva de las expectativas de subidas sería una señal favorable, aunque el BCE todavía no haya empezado a bajar tipos.

El mercado de futuros es más útil para este objetivo que esperar a los anuncios oficiales.
Indicador 4 — Rentabilidad adicional frente al monetario
No utilizaría únicamente la pendiente de los bonos. Calcularía la rentabilidad del fondo a 2 años frente a la rentabilidad estimada del monetario durante los próximos 12 meses.
\[
\text{Ventaja estimada} =
Y_{\text{fondo}}-\text{Gastos}-R_{\text{monetario esperado}}
\]

A esto habría que incorporar el posible efecto de cambios en los rendimientos de los bonos. Una prima aparentemente atractiva puede desaparecer con una subida moderada de tipos.
Indicador 5 — Comportamiento del tramo de 10 años
Aunque no pretendas invertir en bonos a 10 años, los seguiría como indicador de contexto.

Si el rendimiento a 2 años se estabiliza pero el de 10 años continúa subiendo considerablemente, podría indicar persistencia de riesgos de inflación, primas por plazo o presión de oferta de deuda.

No impediría automáticamente comprar bonos a 2 años, pero aconsejaría prudencia.
3. Cuadro de control con umbrales
Para convertirlo en una regla de seguimiento, utilizaría este sistema inicial de puntuación. Los umbrales son heurísticos, pendientes de validar con un backtest, no niveles históricamente optimizados.
Evaluador mensual de entrada
Selecciona el estado de cada indicador según los datos observados. No está precargado con datos actuales.

1. Tendencia del 2 años

DesfavorableNeutralFavorable

Variación en 3 meses ≤ −15 pb

2. Pendiente 2 años − 3 meses

DesfavorableNeutralFavorable

Estable sin dirección clara

3. Expectativas de tipos del BCE

DesfavorableNeutralFavorable

Tipos estables previstos

4. Prima frente al monetario

DesfavorableNeutralFavorable

Entre 0 y +0,50 pp

5. Tendencia del 10 años

DesfavorableNeutralFavorable

Superior a +25 pb
Puntuación total
5 / 10

Entrada parcial



Existen señales mixtas. Puede justificarse una entrada escalonada, evitando comprometer todo el capital.

Restablecer evaluación
4. Dónde consultar los datos
El BCE permite obtener prácticamente toda la información necesaria:
Dato	Fuente
Rendimiento a 2 años AAA	BCE · Bono 2 años
Curva completa a 3 meses, 2 y 10 años	BCE · Curvas de tipos
€STR para referencia monetaria	BCE · €STR
Tipos futuros	Curva OIS €STR, mediante datos de mercado

El BCE publica las curvas de deuda diariamente desde septiembre de 2004. Esto permite comparar los valores actuales con una serie histórica de más de 20 años. 
European Central Bank+1


Como referencia, el último €STR publicado el 16 de noviembre de 2026 es del 2,437 %, correspondiente al 13 de noviembre. 
European Central Bank

5. Mi propuesta de seguimiento
Haría dos controles:
- Semanal, de 5 minutos: observar el rendimiento a 2 años y su tendencia. Sirve para detectar cambios importantes.
- Mensual, completo: registrar los cinco indicadores, calcular la puntuación y revisar si ha cambiado el escenario de inversión.
Guardaría cada observación en una tabla con la fecha, los tipos, las pendientes, la puntuación y la rentabilidad posterior de los fondos. Con el tiempo podríamos comprobar si las señales funcionan realmente.

Añadiría una sexta métrica para la decisión de inversión real: la duración modificada y la TIR neta del fondo concreto. Dos fondos comercializados como renta fija a corto plazo pueden reaccionar de manera bastante diferente si su duración, calidad crediticia o composición no coinciden.

Mi recomendación sería utilizar estas cinco métricas como primera versión, pero antes de asignar dinero según una puntuación de 8/10, realizar un backtest desde 2004. Eso permitiría sustituir los umbrales propuestos por niveles empíricos y comprobar si habrían permitido superar a los monetarios a 6, 12 y 24 meses.

Así tendríamos un sistema basado en evidencias históricas, no únicamente en una interpretación visual de la curva.