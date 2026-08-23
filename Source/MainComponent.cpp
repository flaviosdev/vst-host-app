#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    // ------------------------------------------------------------------
    // Registra os formatos de plugin suportados (VST3, habilitado no CMake)
    // ------------------------------------------------------------------
    formatManager.addDefaultFormats();

    // ------------------------------------------------------------------
    // Inicializa o gerenciador de áudio. Sem entradas de áudio por padrão
    // (não precisamos de entrada de áudio para tocar um VSTi), 2 saídas.
    // O usuário escolhe o driver ASIO na tela de configurações.
    // ------------------------------------------------------------------
    deviceManager.initialiseWithDefaultDevices(0, 2);
    deviceManager.addChangeListener(this);

    // Liga o "player" (que recebe áudio e MIDI e os envia ao plugin) ao
    // dispositivo de áudio atual.
    deviceManager.addAudioCallback(&audioProcessorPlayer);

    // Conecta os dispositivos MIDI já habilitados.
    connectMidiInputs();

    // ------------------------------------------------------------------
    // UI
    // ------------------------------------------------------------------
    addAndMakeVisible(audioSettingsButton);
    audioSettingsButton.onClick = [this] { showAudioSettings(); };

    addAndMakeVisible(loadPluginButton);
    loadPluginButton.onClick = [this] { loadPlugin(); };

    addAndMakeVisible(showEditorButton);
    showEditorButton.onClick = [this] { openPluginEditor(); };
    showEditorButton.setEnabled(false);

    pluginNameLabel.setText("Nenhum plugin carregado", juce::dontSendNotification);
    pluginNameLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(pluginNameLabel);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    addAndMakeVisible(statusLabel);

    addAndMakeVisible(factoryProgramLabel);
    addAndMakeVisible(factoryProgramBox);
    factoryProgramBox.setEnabled(false);
    factoryProgramBox.onChange = [this]
    {
        if (plugin != nullptr)
        {
            const auto index = factoryProgramBox.getSelectedId() - 1;
            if (index >= 0)
                plugin->setCurrentProgram(index);
        }
    };

    addAndMakeVisible(userPresetLabel);
    addAndMakeVisible(userPresetBox);

    addAndMakeVisible(savePresetButton);
    savePresetButton.onClick = [this] { saveCurrentAsPreset(); };
    savePresetButton.setEnabled(false);

    addAndMakeVisible(loadPresetButton);
    loadPresetButton.onClick = [this] { loadSelectedPreset(); };
    loadPresetButton.setEnabled(false);

    addAndMakeVisible(deletePresetButton);
    deletePresetButton.onClick = [this] { deleteSelectedPreset(); };
    deletePresetButton.setEnabled(false);

    setSize(640, 420);
}

MainComponent::~MainComponent()
{
    deviceManager.removeChangeListener(this);
    unloadPlugin();
    deviceManager.removeAudioCallback(&audioProcessorPlayer);
    deviceManager.closeAudioDevice();
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    audioSettingsButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);

    loadPluginButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);

    pluginNameLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);

    showEditorButton.setBounds(area.removeFromTop(30));
    area.removeFromTop(16);

    {
        auto row = area.removeFromTop(24);
        factoryProgramLabel.setBounds(row.removeFromLeft(160));
        factoryProgramBox.setBounds(row);
    }
    area.removeFromTop(16);

    {
        auto row = area.removeFromTop(24);
        userPresetLabel.setBounds(row.removeFromLeft(160));
        userPresetBox.setBounds(row);
    }
    area.removeFromTop(8);

    {
        auto row = area.removeFromTop(30);
        const int buttonWidth = row.getWidth() / 3;
        savePresetButton.setBounds(row.removeFromLeft(buttonWidth).reduced(4, 0));
        loadPresetButton.setBounds(row.removeFromLeft(buttonWidth).reduced(4, 0));
        deletePresetButton.setBounds(row.reduced(4, 0));
    }

    area.removeFromTop(16);
    statusLabel.setBounds(area.removeFromTop(24));
}

//==============================================================================
void MainComponent::setStatus(const juce::String& message)
{
    statusLabel.setText(message, juce::dontSendNotification);
}

//==============================================================================
//  Áudio / MIDI
//==============================================================================
void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManager,
        0, 0,       // canais de entrada de áudio (min/max) - não usamos
        0, 2,       // canais de saída de áudio (min/max)
        true,       // mostrar seletor de entradas MIDI
        false,      // mostrar seletor de saídas MIDI
        true,       // mostrar pares de canais estéreo
        false);     // esconder opções avançadas atrás de um botão

    selector->setSize(500, 450);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector);
    options.dialogTitle = "Configuracoes de Audio e MIDI";
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::connectMidiInputs()
{
    // Remove callbacks antigos e reconecta com base no que está habilitado
    // agora no AudioDeviceManager. Isso é chamado na inicialização e sempre
    // que a configuração de dispositivos mudar (ex.: usuário habilitou um
    // teclado MIDI na tela de configurações).
    for (const auto& midiInput : juce::MidiInput::getAvailableDevices())
    {
        const bool isEnabled = deviceManager.isMidiInputDeviceEnabled(midiInput.identifier);

        // addMidiInputDeviceCallback ignora silenciosamente se o dispositivo
        // não estiver habilitado, e removeMidiInputDeviceCallback ignora se
        // o callback não estiver registrado - então é seguro chamar sempre.
        deviceManager.removeMidiInputDeviceCallback(midiInput.identifier, &audioProcessorPlayer);

        if (isEnabled)
            deviceManager.addMidiInputDeviceCallback(midiInput.identifier, &audioProcessorPlayer);
    }
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &deviceManager)
        connectMidiInputs();
}

//==============================================================================
//  Plugin VST3
//==============================================================================
void MainComponent::loadPlugin()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Selecione um plugin VST3",
        juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory),
        "*.vst3");

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File{})
            return; // usuário cancelou

        unloadPlugin();

        juce::OwnedArray<juce::PluginDescription> typesFound;

        // Varre o arquivo/bundle .vst3 selecionado em busca dos formatos
        // registrados (VST3). Um único arquivo pode conter mais de um plugin.
        for (int i = 0; i < formatManager.getNumFormats(); ++i)
        {
            auto* format = formatManager.getFormat(i);
            knownPluginList.scanAndAddFile(file.getFullPathName(), true, typesFound, *format);
        }

        if (typesFound.isEmpty())
        {
            setStatus("Nao foi possivel identificar um plugin valido nesse arquivo.");
            return;
        }

        const auto& description = *typesFound.getFirst();

        juce::String errorMessage;
        auto* device = deviceManager.getCurrentAudioDevice();
        const double sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;
        const int blockSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;

        plugin = formatManager.createPluginInstance(description, sampleRate, blockSize, errorMessage);

        if (plugin == nullptr)
        {
            setStatus("Erro ao carregar plugin: " + errorMessage);
            return;
        }

        plugin->prepareToPlay(sampleRate, blockSize);
        audioProcessorPlayer.setProcessor(plugin.get());

        pluginNameLabel.setText("Plugin carregado: " + description.name, juce::dontSendNotification);
        setStatus("Plugin carregado com sucesso.");
        showEditorButton.setEnabled(true);
        savePresetButton.setEnabled(true);
        loadPresetButton.setEnabled(true);
        deletePresetButton.setEnabled(true);

        presetManager.setActivePlugin(description.name);
        refreshFactoryProgramBox();
        refreshUserPresetBox();
    });
}

void MainComponent::unloadPlugin()
{
    closePluginEditorWindow();

    audioProcessorPlayer.setProcessor(nullptr);

    if (plugin != nullptr)
    {
        plugin->releaseResources();
        plugin.reset();
    }

    pluginNameLabel.setText("Nenhum plugin carregado", juce::dontSendNotification);
    showEditorButton.setEnabled(false);
    savePresetButton.setEnabled(false);
    loadPresetButton.setEnabled(false);
    deletePresetButton.setEnabled(false);
    factoryProgramBox.clear();
    factoryProgramBox.setEnabled(false);
    userPresetBox.clear();
}

void MainComponent::openPluginEditor()
{
    if (plugin == nullptr)
        return;

    if (pluginEditorWindow != nullptr)
    {
        pluginEditorWindow->toFront(true);
        return;
    }

    auto* editor = plugin->createEditorIfNeeded();
    if (editor == nullptr)
    {
        setStatus("Este plugin nao possui interface grafica propria.");
        return;
    }

    pluginEditorComponent.reset(editor);

    class EditorWindow : public juce::DocumentWindow
    {
    public:
        EditorWindow(const juce::String& name, std::function<void()> onCloseCb)
            : DocumentWindow(name, juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
              onClose(std::move(onCloseCb))
        {
        }

        void closeButtonPressed() override
        {
            if (onClose)
                onClose();
        }

    private:
        std::function<void()> onClose;
    };

    auto* window = new EditorWindow(plugin->getName(), [this] { closePluginEditorWindow(); });
    pluginEditorWindow.reset(window);

    window->setUsingNativeTitleBar(true);
    window->setContentNonOwned(pluginEditorComponent.get(), true);
    window->centreWithSize(pluginEditorComponent->getWidth(), pluginEditorComponent->getHeight());
    window->setResizable(pluginEditorComponent->isResizable(), false);
    window->setVisible(true);
}

void MainComponent::closePluginEditorWindow()
{
    pluginEditorWindow.reset();
    pluginEditorComponent.reset();
}

//==============================================================================
//  Presets
//==============================================================================
void MainComponent::refreshFactoryProgramBox()
{
    factoryProgramBox.clear();

    if (plugin == nullptr || plugin->getNumPrograms() <= 0)
    {
        factoryProgramBox.setEnabled(false);
        return;
    }

    for (int i = 0; i < plugin->getNumPrograms(); ++i)
    {
        auto name = plugin->getProgramName(i);
        factoryProgramBox.addItem(name.isNotEmpty() ? name : ("Programa " + juce::String(i + 1)), i + 1);
    }

    factoryProgramBox.setSelectedId(plugin->getCurrentProgram() + 1, juce::dontSendNotification);
    factoryProgramBox.setEnabled(true);
}

void MainComponent::refreshUserPresetBox()
{
    userPresetBox.clear();

    auto presets = presetManager.listPresets();
    int id = 1;
    for (const auto& name : presets)
        userPresetBox.addItem(name, id++);
}

void MainComponent::saveCurrentAsPreset()
{
    if (plugin == nullptr)
        return;

    auto* window = new juce::AlertWindow("Salvar Preset",
                                          "Digite um nome para o preset:",
                                          juce::AlertWindow::NoIcon);
    window->addTextEditor("presetName", "", "Nome do preset:");
    window->addButton("Salvar", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancelar", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true, juce::ModalCallbackFunction::create([this, window](int result)
    {
        std::unique_ptr<juce::AlertWindow> owned(window);

        if (result != 1 || plugin == nullptr)
            return;

        auto name = owned->getTextEditorContents("presetName").trim();
        if (name.isEmpty())
            return;

        if (presetManager.savePreset(*plugin, name))
        {
            setStatus("Preset \"" + name + "\" salvo.");
            refreshUserPresetBox();
        }
        else
        {
            setStatus("Falha ao salvar o preset.");
        }
    }), true);
}

void MainComponent::loadSelectedPreset()
{
    if (plugin == nullptr)
        return;

    auto name = userPresetBox.getText();
    if (name.isEmpty())
        return;

    if (presetManager.loadPreset(*plugin, name))
    {
        setStatus("Preset \"" + name + "\" carregado.");
        refreshFactoryProgramBox(); // o preset pode ter mudado o programa atual
    }
    else
    {
        setStatus("Falha ao carregar o preset.");
    }
}

void MainComponent::deleteSelectedPreset()
{
    auto name = userPresetBox.getText();
    if (name.isEmpty())
        return;

    if (presetManager.deletePreset(name))
    {
        setStatus("Preset \"" + name + "\" excluido.");
        refreshUserPresetBox();
    }
    else
    {
        setStatus("Falha ao excluir o preset.");
    }
}
