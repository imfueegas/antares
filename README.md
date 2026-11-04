# Entonador Vocal

Corrector de afinación para voz solista. Detecta el tono, lo lleva a la nota más cercana de la escala que elijas y vuelve a sintetizar la voz conservando el timbre. Esta carpeta trae dos versiones del mismo motor:

| Carpeta | Qué es |
|---|---|
| `Source/`, `CMakeLists.txt` | Plugin **VST3** en tiempo real (C++ con JUCE 8) para FL Studio y cualquier otro DAW |
| `Web/` | Versión de navegador: carga un audio, lo corrige y lo descarga. Abre `entonador.html` con doble clic |
| `Tests/` | Prueba del motor en tiempo real sin JUCE |
| `.github/workflows/build.yml` | Compila el plugin automáticamente en Windows y macOS |

## Importante: el `.vst3` hay que compilarlo

Un VST es un programa compilado y yo no puedo generar el `.dll` de Windows desde mi entorno. Lo que sí he comprobado:

- El motor en tiempo real (`Source/TunerCore.h`) se compiló y se probó con la voz de ejemplo: el error medio de afinación bajó de unos 32 cents a unos 3 cents, a 44,1 y 48 kHz.
- El código JUCE (`PluginProcessor`, `PluginEditor`) pasa la comprobación de sintaxis contra las cabeceras reales de JUCE 8.0.6, sin errores ni avisos.
- **No** lo he compilado para Windows ni cargado en FL Studio. Si algo falla al compilar o en el DAW, dime el mensaje de error y lo corrijo.

## Conseguir el .vst3 (opción fácil, sin instalar nada)

1. Crea una cuenta en GitHub y un repositorio nuevo (puede ser privado).
2. Sube el contenido de esta carpeta al repositorio, incluida la carpeta oculta `.github`.
3. Abre la pestaña **Actions**. El flujo "Compilar Entonador Vocal" arranca solo (tarda unos 10 a 15 minutos la primera vez).
4. Cuando termine, entra en la ejecución y descarga el artefacto **Entonador-Windows**. Dentro está `VST3/Entonador Vocal.vst3`. Hay también una versión `Standalone` para probarlo sin DAW.

## Compilar en tu PC con Windows

Necesitas Visual Studio 2022 (carga de trabajo "Desarrollo para el escritorio con C++"), CMake y Git.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

El plugin queda en `build/Entonador_artefacts/Release/VST3/`. La primera compilación descarga JUCE (unos 100 MB).

## Instalar en FL Studio

1. Copia la carpeta **`Entonador Vocal.vst3`** completa (es una carpeta, no un solo archivo) a `C:\Program Files\Common Files\VST3\`.
2. En FL Studio: **Options > Manage plugins > Find installed plugins** (FL Studio 20.8 o posterior para VST3).
3. Pon el plugin en una ranura de efectos del mezclador, en la pista de la voz. Aparece en la lista como **Entonador Vocal**.

El plugin informa de su latencia (unos 43 ms a 44,1 kHz) y FL Studio la compensa solo si tienes activada la compensación automática de retardo.

## Uso

- **Tonalidad y escala:** elige la tónica y la escala. Pulsa las teclas para permitir o quitar notas sueltas (pasa a "Personalizada").
- **Velocidad:** 0 ms da el efecto robot. De 20 a 60 ms suena natural.
- **Humanizar:** retrasa la corrección en notas largas.
- **Vibrato natural:** 100 % conserva el vibrato del cantante, 0 % lo elimina y 200 % lo exagera.
- **Vibrato añadido:** forma, profundidad, velocidad y retardo.
- **Transposición, afinación fina, formantes, referencia de La y mezcla.**
- El gráfico muestra la entrada (naranja), la salida (azul) y la nota objetivo de los últimos segundos.

## Límites

- Solo una voz o instrumento monofónico por instancia. Con acordes o varias voces no funciona bien.
- Entrada estéreo se mezcla a mono y la salida es la misma en ambos canales.
- La corrección es causal (no puede mirar al futuro), así que se comporta algo distinto de la versión web, que analiza el archivo entero.
- Úsalo con la voz sola, sin música de fondo.

## Licencia

JUCE se distribuye con licencia AGPLv3 o comercial. Para uso personal no hay problema. Si vas a distribuir o vender el plugin, tienes que cumplir la AGPL (publicar el código) o comprar una licencia de JUCE. "Auto-Tune" es marca registrada de Antares: este proyecto es independiente y no usa su código ni su interfaz.
