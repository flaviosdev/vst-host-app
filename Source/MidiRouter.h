#pragma once

#include <JuceHeader.h>

/**
    Estado de roteamento de MIDI por plugin: mute, solo, e um espaço já
    reservado para filtro de canal MIDI no futuro. Fica dentro de
    LoadedPlugin, ao lado de "volume" - é o mesmo tipo de estado por-plugin.
*/
struct PluginMidiRoute
{
    bool muted = false;
    bool solo = false;

    // Preparado para o futuro: 0 = todos os canais (comportamento de hoje).
    // 1-16 filtraria só aquele canal MIDI - a lógica de filtro entra dentro
    // de MidiRouter::route() quando essa funcionalidade for implementada,
    // sem precisar mudar PluginHostEngine nem a UI.
    int midiChannelFilter = 0;
};

/**
    Decide o que cada plugin deve efetivamente receber de MIDI a cada bloco
    de áudio, dado o estado de mute/solo/canal dele e se algum outro plugin
    do host está em modo solo. Não conhece nada de UI nem de AudioProcessor -
    só recebe e devolve buffers de MIDI.
*/
class MidiRouter
{
public:
    // source: o MIDI original recebido do(s) dispositivo(s) MIDI habilitados.
    // destination: buffer de saída, específico de um plugin - deve estar
    // vazio (clear()) antes de chamar route(), já que essa função só adiciona.
    // anySoloActive: true se QUALQUER plugin do host está em solo agora -
    // usado para saber se plugins não-solo devem ser silenciados mesmo sem
    // estarem mutados individualmente.
    static void route(const PluginMidiRoute& route,
                       bool anySoloActive,
                       const juce::MidiBuffer& source,
                       int numSamples,
                       juce::MidiBuffer& destination);
};
