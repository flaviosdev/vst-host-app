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
//  PluginSettingsComponent
//==============================================================================
class PluginPathListModel : public juce::ListBoxModel
{
public:
    explicit PluginPathListModel(juce::StringArray& values) : paths(values) {}
    int getNumRows() override { return paths.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (selected) g.fillAll(juce::Colours::lightblue);
        g.setColour(juce::Colours::white);
        g.drawText(paths[row], 8, 0, width - 16, height, juce::Justification::centredLeft);
    }
private:
    juce::StringArray& paths;
};

PluginSettingsComponent::PluginSettingsComponent(PluginHostEngine& engineToUse)
    : engine(engineToUse)
{
    infoLabel.setText("Pastas onde o VST Host procura plugins VST2/VST3:", juce::dontSendNotification);
    infoLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(infoLabel);

    addAndMakeVisible(pathList);
    pathModel = std::make_unique<PluginPathListModel>(paths);
    pathList.setModel(pathModel.get());

    addAndMakeVisible(addButton);
    addButton.onClick = [this] { addPath(); };

    addAndMakeVisible(removeButton);
    removeButton.onClick = [this] { removePath(); };

    addAndMakeVisible(scanButton);
    scanButton.onClick = [this] { scan(false); };

    addAndMakeVisible(scanNewButton);
    scanNewButton.onClick = [this] { scan(true); };

    refreshPaths();
}

void PluginSettingsComponent::refreshPaths()
{
    paths = engine.getPluginSearchPaths();
    pathList.updateContent();
    pathList.repaint();
}

void PluginSettingsComponent::addPath()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Selecione a pasta de plugins",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*",
        false);

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        const auto selectedDirectory = chooser.getResult();
        if (selectedDirectory.isDirectory())
        {
            engine.addPluginSearchPath(selectedDirectory);
            refreshPaths();
        }

        fileChooser.reset();
    });
}

void PluginSettingsComponent::removePath()
{
    const auto row = pathList.getSelectedRow();
    if (row >= 0 && row < paths.size())
    {
        engine.removePluginSearchPath(juce::File(paths[row]));
        refreshPaths();
    }
}

void PluginSettingsComponent::scan(bool newOnly)
{
    scanButton.setEnabled(false);
    scanNewButton.setEnabled(false);
    engine.scanPlugins(newOnly);
    scanButton.setEnabled(true);
    scanNewButton.setEnabled(true);
}

void PluginSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(16);
    infoLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    auto buttons = area.removeFromBottom(32);
    addButton.setBounds(buttons.removeFromLeft(100).reduced(2, 0));
    removeButton.setBounds(buttons.removeFromLeft(100).reduced(2, 0));
    scanButton.setBounds(buttons.removeFromLeft(90).reduced(2, 0));
    scanNewButton.setBounds(buttons.removeFromLeft(100).reduced(2, 0));
    area.removeFromBottom(8);
    pathList.setBounds(area);
}

//==============================================================================
//  PreferencesWindow
//==============================================================================
PreferencesWindow::PreferencesWindow(PluginHostEngine& engineToUse)
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
        engineToUse.getDeviceManager(),
        0, 0,       // canais de entrada de audio - nao usamos
        0, 2,       // canais de saida de audio
        false,      // sem seletor de entradas MIDI aqui (tem aba propria)
        false,      // sem seletor de saidas MIDI
        true,       // mostrar pares de canais estereo
        false);     // nao esconder opcoes avancadas atras de botao

    tabs.addTab("Audio", tabBackground, audioSelector, true);
    tabs.addTab("MIDI", tabBackground, new MidiSettingsComponent(engineToUse.getDeviceManager()), true);
    tabs.addTab("Plugins", tabBackground, new PluginSettingsComponent(engineToUse), true);

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
