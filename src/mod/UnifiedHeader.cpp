#include "pch.h"
#include "UnifiedHeader.h"

namespace xpx::mod
{
    namespace
    {
        namespace mux = winrt::Microsoft::UI::Xaml;
        namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
        namespace muxm = winrt::Microsoft::UI::Xaml::Media;
        namespace muxma = winrt::Microsoft::UI::Xaml::Media::Animation;

        // Le parti dell'intestazione che contano, cercate per nome con una sola visita dell'albero.
        struct Parts
        {
            mux::FrameworkElement navigationBand{ nullptr }; // Grid #NavigationBarControlGrid (fascia degli indirizzi)
            mux::FrameworkElement commandBar{ nullptr };     // CommandBar #FileExplorerCommandBar
            mux::FrameworkElement secondaryBar{ nullptr };   // CommandBar #FileExplorerSecondaryCommandBar (Anteprima, Dettagli)
            mux::FrameworkElement tabContainer{ nullptr };   // Grid #TabContainerGrid (striscia delle schede)
        };

        Parts FindParts(mux::DependencyObject const& root)
        {
            Parts parts;
            std::vector<mux::DependencyObject> pending{ root };
            while (!pending.empty())
            {
                const auto node = pending.back();
                pending.pop_back();
                if (const auto element = node.try_as<mux::FrameworkElement>())
                {
                    const auto name = element.Name();
                    if (name.empty())
                    {
                    }
                    else if (!parts.navigationBand && name == L"NavigationBarControlGrid")
                    {
                        parts.navigationBand = element;
                    }
                    else if (!parts.commandBar && name == L"FileExplorerCommandBar")
                    {
                        parts.commandBar = element;
                    }
                    else if (!parts.secondaryBar && name == L"FileExplorerSecondaryCommandBar")
                    {
                        parts.secondaryBar = element;
                    }
                    else if (!parts.tabContainer && name == L"TabContainerGrid")
                    {
                        parts.tabContainer = element;
                    }
                }
                const int count = muxm::VisualTreeHelper::GetChildrenCount(node);
                for (int i = count - 1; i >= 0; --i)
                {
                    pending.push_back(muxm::VisualTreeHelper::GetChild(node, i));
                }
            }
            return parts;
        }

        // Ancora vivo e nell'albero (Esplora file può ricreare una parte e lasciare la vecchia staccata).
        bool InTree(winrt::weak_ref<mux::FrameworkElement> const& weak)
        {
            const auto element = weak.get();
            return element && muxm::VisualTreeHelper::GetParent(element) != nullptr;
        }

        // La proprietà dello sfondo, secondo il tipo dell'elemento.
        mux::DependencyProperty BackgroundOf(mux::DependencyObject const& element)
        {
            if (element.try_as<muxc::Border>())
            {
                return muxc::Border::BackgroundProperty();
            }
            if (element.try_as<muxc::Panel>())
            {
                return muxc::Panel::BackgroundProperty();
            }
            if (element.try_as<muxc::Control>())
            {
                return muxc::Control::BackgroundProperty();
            }
            return nullptr;
        }
    }

    size_t UnifiedHeader::Apply(mux::UIElement const& anchor)
    {
        if (!anchor)
        {
            return 0;
        }
        // Elementi che non esistono più: niente da tenere.
        std::erase_if(m_entries, [](Entry const& entry) { return !entry.element.get(); });
        if (Complete())
        {
            return Refresh();
        }
        if (!m_clear)
        {
            m_clear = muxm::SolidColorBrush(winrt::Microsoft::UI::Colors::Transparent());
        }
        mux::DependencyObject root = anchor;
        while (const auto parent = muxm::VisualTreeHelper::GetParent(root))
        {
            root = parent;
        }
        const Parts parts = FindParts(root);
        size_t covered = 0;
        const auto cover = [&](mux::DependencyObject const& element) {
            if (element && Cover(element))
            {
                ++covered;
            }
        };
        cover(parts.navigationBand);
        mux::FrameworkElement commandBand{ nullptr };
        if (parts.commandBar)
        {
            // La fascia dei comandi è il Grid che contiene le due barre (con le linee sopra e sotto, che restano).
            commandBand = muxm::VisualTreeHelper::GetParent(parts.commandBar).try_as<muxc::Grid>();
            cover(commandBand);
            cover(parts.commandBar);
        }
        cover(parts.secondaryBar);
        if (parts.tabContainer)
        {
            // Le linee sotto la striscia delle schede: le figlie dirette del contenitore (le omonime dentro
            // l'elenco delle schede sono già invisibili con lui).
            const int count = muxm::VisualTreeHelper::GetChildrenCount(parts.tabContainer);
            for (int i = 0; i < count; ++i)
            {
                if (const auto line = muxm::VisualTreeHelper::GetChild(parts.tabContainer, i).try_as<muxc::Border>())
                {
                    if (line.Name() == L"LeftBottomBorderLine" || line.Name() == L"RightBottomBorderLine")
                    {
                        cover(line);
                    }
                }
            }
        }
        m_navigationBand = parts.navigationBand ? winrt::make_weak(parts.navigationBand) : winrt::weak_ref<mux::FrameworkElement>{};
        m_commandBand = commandBand ? winrt::make_weak(commandBand) : winrt::weak_ref<mux::FrameworkElement>{};
        m_commandBar = parts.commandBar ? winrt::make_weak(parts.commandBar) : winrt::weak_ref<mux::FrameworkElement>{};
        return covered;
    }

    bool UnifiedHeader::Complete() const
    {
        // Le due fasce e la barra dei comandi trovate e ancora al loro posto: il resto è stato fatto
        // insieme a loro. Finché manca qualcosa (barra dei comandi non ancora caricata) si cerca ancora.
        return InTree(m_navigationBand) && InTree(m_commandBand) && InTree(m_commandBar);
    }

    bool UnifiedHeader::Cover(mux::DependencyObject const& element)
    {
        const auto property = BackgroundOf(element);
        if (!property || std::any_of(m_entries.begin(), m_entries.end(), [&](Entry const& entry) { return entry.element.get() == element; }))
        {
            return false;
        }
        // Animazione a durata zero che resta ferma sull'ultimo fotogramma (HoldEnd): sfondo trasparente
        // finché non la si ferma.
        muxma::DiscreteObjectKeyFrame frame;
        frame.KeyTime(muxma::KeyTimeHelper::FromTimeSpan(winrt::Windows::Foundation::TimeSpan{ 0 }));
        frame.Value(m_clear);
        muxma::ObjectAnimationUsingKeyFrames animation;
        animation.KeyFrames().Append(frame);
        muxma::Storyboard::SetTarget(animation, element);
        muxma::Storyboard::SetTargetProperty(animation, L"Background");
        muxma::Storyboard storyboard;
        storyboard.FillBehavior(muxma::FillBehavior::HoldEnd);
        storyboard.Children().Append(animation);
        storyboard.Begin();
        m_entries.push_back(Entry{ winrt::make_weak(element), property, storyboard });
        return true;
    }

    size_t UnifiedHeader::Refresh()
    {
        // Un'animazione può fermarsi (elemento tolto e rimesso nell'albero): si ricomincia. Si controlla
        // solo qui, a distanza di secondi dall'avvio: appena avviata l'animazione si applica al fotogramma dopo.
        size_t restarted = 0;
        for (auto const& entry : m_entries)
        {
            const auto element = entry.element.get();
            if (!element)
            {
                continue;
            }
            const bool stopped = entry.storyboard.GetCurrentState() == muxma::ClockState::Stopped;
            if (stopped || element.GetValue(entry.property) != m_clear)
            {
                if (!stopped)
                {
                    entry.storyboard.Stop();
                }
                entry.storyboard.Begin();
                ++restarted;
            }
        }
        return restarted;
    }

    void UnifiedHeader::Restore()
    {
        for (auto const& entry : m_entries)
        {
            try
            {
                if (entry.element.get())
                {
                    entry.storyboard.Stop(); // torna il valore di Esplora file, quello del tema attuale
                }
            }
            catch (...)
            {
            }
        }
        m_entries.clear();
        m_navigationBand = nullptr;
        m_commandBand = nullptr;
        m_commandBar = nullptr;
    }
}
