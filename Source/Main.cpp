#include <JuceHeader.h>
#include "MainComponent.h"

//==============================================================================
class VSTHostApplication : public juce::JUCEApplication
{
public:
    VSTHostApplication() = default;

    const juce::String getApplicationName() override    { return "VST Host App"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }

    // Instância única: dois processos deste app rodando ao mesmo tempo
    // brigariam pelo mesmo driver ASIO, pelos mesmos arquivos de
    // configuração em %APPDATA% e possivelmente pelo mesmo plugin de
    // 32-bit - efeitos colaterais que não vale a pena arriscar.
    bool moreThanOneInstanceAllowed() override { return false; }

    // Chamado NA INSTÂNCIA JÁ ABERTA quando o usuário tenta abrir uma
    // segunda (ex.: clicou duas vezes no atalho sem perceber). Em vez de
    // simplesmente ignorar a segunda tentativa, traz a janela existente
    // pra frente - assim o usuário entende o que aconteceu.
    void anotherInstanceStarted(const juce::String&) override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->toFront(true);
            mainWindow->grabKeyboardFocus();
        }
    }

    void initialise(const juce::String&) override
    {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    //==========================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour(juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);

            centreWithSize(getWidth(), getHeight());
            setResizable(true, true);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(VSTHostApplication)
