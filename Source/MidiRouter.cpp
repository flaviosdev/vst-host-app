#include "MidiRouter.h"

void MidiRouter::route(const PluginMidiRoute& route,
                        bool anySoloActive,
                        const juce::MidiBuffer& source,
                        int numSamples,
                        juce::MidiBuffer& destination)
{
    // Regra de silenciamento: um plugin fica mudo se
    //   (a) ele está mutado diretamente, OU
    //   (b) existe algum OUTRO plugin em solo, e este aqui não está em solo.
    // Um plugin em solo sempre toca, independente do mute dele (solo tem
    // prioridade - é o comportamento padrão em DAWs).
    const bool shouldBeSilent = !route.solo && (route.muted || anySoloActive);

    if (shouldBeSilent)
        return; // destination já está vazio (clear() foi chamado por quem nos chamou)

    // Sem filtro de canal definido: repassa tudo, igual ao comportamento
    // de hoje antes do router existir.
    if (route.midiChannelFilter <= 0)
    {
        destination.addEvents(source, 0, numSamples, 0);
        return;
    }

    // Com filtro de canal ativo: só repassa eventos daquele canal (e eventos
    // "globais" sem canal, como System Exclusive, sempre passam).
    for (const auto metadata : source)
    {
        const auto message = metadata.getMessage();
        if (!message.getChannel() || message.getChannel() == route.midiChannelFilter)
            destination.addEvent(message, metadata.samplePosition);
    }
}
