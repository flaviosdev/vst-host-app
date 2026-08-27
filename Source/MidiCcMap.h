#pragma once

#include <JuceHeader.h>
#include <vector>

struct MidiCcVolumeBinding
{
    int ccNumber = -1;    // 0-127
    int midiChannel = 0;  // 1-16
    int targetPluginId = -1;
};

/**
    Mapeia mensagens de Control Change (CC) para o volume de um plugin
    específico - o "MIDI Learn" do slider de volume. Deliberadamente
    simples: não distingue fader, botão ou qualquer outro tipo de
    controlador físico - um CC é só um número de 0 a 127, e esse valor é
    escalado direto pra faixa de volume (0.0-2.0). Se o controlador manda
    0/127 (um botão) em vez de uma faixa contínua (um fader), o volume só
    vai pular entre mínimo e máximo - funciona, só não é "suave", e não faz
    diferença nenhuma pra este mapa, que não sabe nem precisa saber disso.

    Mesmo padrão do MidiActionMap: cada CC+canal só pode ter UM binding por
    vez, e não conhece nada de UI ou de como o volume é de fato aplicado.
*/
class MidiCcMap
{
public:
    void startLearning(int targetPluginId);
    void cancelLearning();
    bool isLearning() const noexcept { return learning; }
    int getPendingPluginId() const noexcept { return pendingPluginId; }

    // Chamado para cada mensagem de Control Change recebida enquanto
    // isLearning() é true. Cria (ou substitui) o binding e desliga o modo
    // de captura. Retorna true se a mensagem foi consumida pelo aprendizado.
    bool learnFrom(int ccNumber, int midiChannel);

    const MidiCcVolumeBinding* findBinding(int ccNumber, int midiChannel) const;

    void removeBindingsForPlugin(int pluginId);
    void clearAll() { bindings.clear(); }
    const MidiCcVolumeBinding* getBindingForPlugin(int pluginId) const;

    bool hasAnyBindings() const noexcept { return !bindings.empty(); }

private:
    std::vector<MidiCcVolumeBinding> bindings;

    bool learning = false;
    int pendingPluginId = -1;
};
