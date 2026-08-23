#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    engine.addListener(this);

    addAndMakeVisible(audioSettingsButton);
    audioSettingsButton.onClick = [this] { showPreferences(); };

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
        const auto index = factoryProgramBox.getSelectedId() - 1;
        if (index >= 0)
            engine.setCurrentFactoryProgram(index);
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
    // Se desregistra da Engine antes de qualquer coisa: a partir daqui, nada
    // mais deve chamar pluginChanged()/audioDeviceChanged() nesta instância.
    engine.removeListener(this);

    closePluginEditorWindow();
    preferencesWindow.reset();

    // A Engine (membro declarado depois dos widgets) só é destruída depois
    // do corpo deste destrutor, e salva o estado de áudio/MIDI sozinha.
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
//  PluginHostEngine::Listener
//==============================================================================
void MainComponent::pluginChanged()
{
    if (engine.hasPluginLoaded())
    {
        pluginNameLabel.setText("Plugin carregado: " + engine.getPluginName(), juce::dontSendNotification);
        showEditorButton.setEnabled(true);
        savePresetButton.setEnabled(true);
        loadPresetButton.setEnabled(true);
        deletePresetButton.setEnabled(true);
    }
    else
    {
        pluginNameLabel.setText("Nenhum plugin carregado", juce::dontSendNotification);
        showEditorButton.setEnabled(false);
        savePresetButton.setEnabled(false);
        loadPresetButton.setEnabled(false);
        deletePresetButton.setEnabled(false);
        closePluginEditorWindow();
    }

    refreshFactoryProgramBox();
    refreshUserPresetBox();
}

void MainComponent::audioDeviceChanged()
{
    // Reservado para o dia em que algum widget precisar refletir mudanças
    // de dispositivo de áudio/MIDI. Hoje nenhum widget depende disso.
}

//==============================================================================
//  Preferências
//==============================================================================
void MainComponent::showPreferences()
{
    if (preferencesWindow != nullptr)
    {
        preferencesWindow->toFront(true);
        return;
    }

    preferencesWindow = std::make_unique<PreferencesWindow>(engine.getDeviceManager());
    preferencesWindow->onCloseRequested = [this]
    {
        engine.saveAudioDeviceState(); // salva já ao fechar as preferências, não só ao sair do app
        preferencesWindow.reset();
    };
}

//==============================================================================
//  Plugin
//==============================================================================
void MainComponent::loadPlugin()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Selecione um plugin VST2 (.dll) ou VST3 (.vst3)",
        juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory),
        "*.vst3;*.dll");

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File{})
            return; // usuário cancelou

        auto result = engine.loadPluginFromFile(file);

        setStatus(result.success
                       ? "Plugin carregado com sucesso."
                       : "Erro ao carregar plugin: " + result.errorMessage);
    });
}

void MainComponent::openPluginEditor()
{
    if (!engine.hasPluginLoaded())
        return;

    if (pluginEditorWindow != nullptr)
    {
        pluginEditorWindow->toFront(true);
        return;
    }

    auto* editor = engine.createPluginEditorIfNeeded();
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

    auto* window = new EditorWindow(engine.getPluginName(), [this] { closePluginEditorWindow(); });
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

    auto names = engine.getFactoryProgramNames();
    if (names.isEmpty())
    {
        factoryProgramBox.setEnabled(false);
        return;
    }

    for (int i = 0; i < names.size(); ++i)
        factoryProgramBox.addItem(names[i], i + 1);

    factoryProgramBox.setSelectedId(engine.getCurrentFactoryProgram() + 1, juce::dontSendNotification);
    factoryProgramBox.setEnabled(true);
}

void MainComponent::refreshUserPresetBox()
{
    userPresetBox.clear();

    auto presets = engine.getUserPresetNames();
    int id = 1;
    for (const auto& name : presets)
        userPresetBox.addItem(name, id++);
}

void MainComponent::saveCurrentAsPreset()
{
    if (!engine.hasPluginLoaded())
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

        if (result != 1)
            return;

        auto name = owned->getTextEditorContents("presetName").trim();
        if (name.isEmpty())
            return;

        if (engine.saveUserPreset(name))
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
    auto name = userPresetBox.getText();
    if (name.isEmpty())
        return;

    if (engine.loadUserPreset(name))
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

    if (engine.deleteUserPreset(name))
    {
        setStatus("Preset \"" + name + "\" excluido.");
        refreshUserPresetBox();
    }
    else
    {
        setStatus("Falha ao excluir o preset.");
    }
}
