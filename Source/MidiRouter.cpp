#include "MidiRouter.h"

namespace
{
    // Mensagens que "desligam" som em andamento. Essas sempre atravessam,
    // mesmo com o plugin mutado/fora do solo - senão uma nota tocada antes
    // do mute fica pendurada pra sempre, porque o plugin nunca recebe o
    // Note Off (ou o Sustain Off, ou o All Notes Off) correspondente.
    bool isNoteOffLikeMessage(const juce::MidiMessage& message) noexcept
    {
        if (message.isNoteOff())
            return true;

        // Muita gente toca com o pedal de sustain (CC 64) segurado; se ele
        // for solto depois do mute, o "desligar sustain" também precisa
        // passar, senão a nota fica presa pelo pedal em vez do note-on.
        if (message.isSustainPedalOff())
            return true;

        if (message.isAllNotesOff() || message.isAllSoundOff())
            return true;

        return false;
    }
}

void MidiRouter::route(const PluginMidiRoute& route,
                        bool anySoloActive,
                        const juce::MidiBuffer& source,
                        int numSamples,
                        juce::MidiBuffer& destination)
{
    juce::ignoreUnused(numSamples);

    // Regra de silenciamento: um plugin fica mudo se
    //   (a) ele está mutado diretamente, OU
    //   (b) existe algum OUTRO plugin em solo, e este aqui não está em solo.
    // Um plugin em solo sempre toca, independente do mute dele (solo tem
    // prioridade - é o comportamento padrão em DAWs).
    const bool shouldBeSilent = !route.solo && (route.muted || anySoloActive);

    for (const auto metadata : source)
    {
        const auto message = metadata.getMessage();

        // Enquanto silenciado, só deixa passar o que desliga som em curso -
        // bloqueia Note On e qualquer outra coisa que dispararia áudio novo.
        if (shouldBeSilent && !isNoteOffLikeMessage(message))
            continue;

        if (route.midiChannelFilter > 0
            && message.getChannel() != 0
            && message.getChannel() != route.midiChannelFilter)
            continue;

        destination.addEvent(message, metadata.samplePosition);
    }
}