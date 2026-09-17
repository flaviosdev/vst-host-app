#pragma once

#include <JuceHeader.h>
#include <vector>

enum class MidiTriggerAction
{
    toggleMute,
    toggleSolo
};

struct MidiTriggerBinding
{
    int noteNumber = -1;     // 0-127
    int midiChannel = 0;     // 1-16
    MidiTriggerAction action = MidiTriggerAction::toggleMute;
    int targetPluginId = -1;
};

/**
    Mapeia mensagens MIDI (nota + canal) para ações de mute/solo em plugins
    específicos - o "MIDI Learn". Não conhece nada de UI, AudioProcessor ou
    de como o mute/solo é aplicado; só sabe combinar uma mensagem recebida
    com um binding e dizer qual ação disparar.

    Cada tecla/pad só pode ter UM binding por vez: aprender um novo binding
    pra uma tecla já usada substitui o antigo (ver learnFrom()).
*/
class MidiActionMap
{
public:
    // Modo de captura: enquanto ativo, a próxima mensagem de Note On
    // observada por learnFrom() vira o binding, e o modo é desligado
    // automaticamente.
    void startLearning(MidiTriggerAction action, int targetPluginId);
    void cancelLearning();
    bool isLearning() const noexcept { return learning; }

    // Só fazem sentido quando isLearning() é true - dizem qual botão da UI
    // foi quem pediu a captura, pra ele poder continuar "aceso" enquanto
    // aguarda a tecla.
    MidiTriggerAction getPendingAction() const noexcept { return pendingAction; }
    int getPendingPluginId() const noexcept { return pendingPluginId; }

    // Chamado para cada mensagem de Note On recebida enquanto isLearning()
    // é true. Cria (ou substitui) o binding e desliga o modo de captura.
    // Retorna true se a mensagem foi consumida pelo aprendizado.
    bool learnFrom(int noteNumber, int midiChannel);

    // Verifica se uma mensagem corresponde a algum binding e, se sim,
    // retorna um ponteiro pra ele (válido até o próximo learnFrom/remove).
    // nullptr se a mensagem não aciona nada.
    const MidiTriggerBinding* findBinding(int noteNumber, int midiChannel) const;

    void removeBinding(int noteNumber, int midiChannel);
    void removeBindingsForPlugin(int pluginId);

    // Remove o binding de uma ação específica de um plugin (ex.: "esquece
    // o mute desse plugin"), sem precisar saber qual nota/canal era.
    // Não faz nada se não houver binding pra essa combinação.
    void removeBindingForAction(MidiTriggerAction action, int pluginId);
    void clearAll() { bindings.clear(); }

    juce::Array<MidiTriggerBinding> getBindingsForPlugin(int pluginId) const;

    // Atalho barato pra quem processa áudio em tempo real poder pular todo
    // o resto do trabalho quando o recurso nem está em uso.
    bool hasAnyBindings() const noexcept { return !bindings.empty(); }

private:
    std::vector<MidiTriggerBinding> bindings;

    bool learning = false;
    MidiTriggerAction pendingAction = MidiTriggerAction::toggleMute;
    int pendingPluginId = -1;
};
