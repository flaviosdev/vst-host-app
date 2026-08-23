#include "PreferencesWindow.h"

//==============================================================================
//  MidiSettingsComponent
//==============================================================================
MidiSettingsComponent::MidiSettingsComponent(juce::AudioDeviceManager& deviceManagerToUse)
    : deviceManager(deviceManagerToUse)
{
    infoLabel.setText("Dispositivos MIDI de entrada detectados:", juce::dontSendNotification);
    infoLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(infoLabel);

    rebuildDeviceListIfChanged();

    // Fica checando periodicamente se algum dispositivo MIDI foi plugado
    // ou desplugado, pra atualizar a lista sozinho.
    startTimer(1500);
}

MidiSettingsComponent::~MidiSettingsComponent()
{
    stopTimer();
}

void MidiSettingsComponent::timerCallback()
{
    rebuildDeviceListIfChanged();
}

void MidiSettingsComponent::rebuildDeviceListIfChanged()
{
    const auto available = juce::MidiInput::getAvailableDevices();

    juce::StringArray currentIds;
    for (const auto& device : available)
        currentIds.add(device.identifier);

    if (currentIds == lastKnownDeviceIds)
        return; // nada mudou, evita reconstruir a UI sem necessidade

    lastKnownDeviceIds = currentIds;
    toggles.clear();

    if (available.isEmpty())
    {
        infoLabel.setText("Nenhum dispositivo MIDI detectado.", juce::dontSendNotification);
    }
    else
    {
        infoLabel.setText("Dispositivos MIDI de entrada detectados:", juce::dontSendNotification);

        for (const auto& device : available)
        {
            auto* toggle = new juce::ToggleButton(device.name);
            toggle->setToggleState(deviceManager.isMidiInputDeviceEnabled(device.identifier),
                                    juce::dontSendNotification);

            const auto deviceId = device.identifier;
            toggle->onClick = [this, deviceId, toggle]
            {
                deviceManager.setMidiInputDeviceEnabled(deviceId, toggle->getToggleState());
            };

            addAndMakeVisible(toggle);
            toggles.add(toggle);
        }
    }

    resized();
}

void MidiSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(16);

    infoLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    for (auto* toggle : toggles)
    {
        toggle->setBounds(area.removeFromTop(28));
        area.removeFromTop(4);
    }
}

//==============================================================================
//  PreferencesWindow
//==============================================================================
PreferencesWindow::PreferencesWindow(juce::AudioDeviceManager& deviceManagerToUse)
    : DocumentWindow("Preferencias",
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour(juce::ResizableWindow::backgroundColourId),
                      juce::DocumentWindow::closeButton)
{
    auto tabBackground = juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour(juce::ResizableWindow::backgroundColourId);

    // Aba de Audio: reaproveita o AudioDeviceSelectorComponent do JUCE, mas
    // sem a parte de MIDI (que agora tem aba própria).
    auto* audioSelector = new juce::AudioDeviceSelectorComponent(
        deviceManagerToUse,
        0, 0,       // canais de entrada de audio - nao usamos
        0, 2,       // canais de saida de audio
        false,      // sem seletor de entradas MIDI aqui (tem aba propria)
        false,      // sem seletor de saidas MIDI
        true,       // mostrar pares de canais estereo
        false);     // nao esconder opcoes avancadas atras de botao

    tabs.addTab("Audio", tabBackground, audioSelector, true);
    tabs.addTab("MIDI", tabBackground, new MidiSettingsComponent(deviceManagerToUse), true);

    setUsingNativeTitleBar(true);
    setContentNonOwned(&tabs, true);
    centreWithSize(520, 480);
    setResizable(true, false);
    setVisible(true);
}

PreferencesWindow::~PreferencesWindow() = default;

void PreferencesWindow::closeButtonPressed()
{
    if (onCloseRequested)
        onCloseRequested();
}
