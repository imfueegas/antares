// Prueba del motor en tiempo real fuera de JUCE.
// Uso: test_core entrada.f32 salida.f32 sampleRate retuneMs vibAmt [transpose] [formant] [mix]
// Lee audio mono float32 crudo, lo procesa muestra a muestra y escribe la salida compensando la latencia.
#include "../Source/TunerCore.h"
#include <cstdio>
#include <cstdlib>

int main (int argc, char** argv)
{
    if (argc < 6) { std::fprintf (stderr, "uso: test_core in.f32 out.f32 sr retuneMs vibAmt [transpose] [formant] [mix]\n"); return 1; }
    FILE* f = std::fopen (argv[1], "rb");
    if (! f) return 2;
    std::vector<float> x;
    float v;
    while (std::fread (&v, 4, 1, f) == 1) x.push_back (v);
    std::fclose (f);

    entonador::Engine e;
    e.prepare (std::atof (argv[3]));
    entonador::Params p;
    p.retuneMs = (float) std::atof (argv[4]);
    p.vibAmt = (float) std::atof (argv[5]);
    p.humanize = 0.f;
    if (argc > 6) p.transpose = (float) std::atof (argv[6]);
    if (argc > 7) p.formant = (float) std::atof (argv[7]);
    if (argc > 8) p.mix = (float) std::atof (argv[8]);
    e.setParams (p);

    const int lat = e.latency();
    std::vector<float> y;
    y.reserve (x.size());
    for (size_t i = 0; i < x.size() + (size_t) lat; ++i)
    {
        const float o = e.process (i < x.size() ? x[i] : 0.f);
        if (i >= (size_t) lat) y.push_back (o);
    }
    f = std::fopen (argv[2], "wb");
    std::fwrite (y.data(), 4, y.size(), f);
    std::fclose (f);
    std::fprintf (stderr, "latencia %d muestras (%.1f ms), %zu muestras procesadas\n", lat, 1000.0 * lat / std::atof (argv[3]), y.size());
    return 0;
}
