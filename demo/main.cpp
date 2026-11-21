// InkPages Demo: the library's development and test app (UI tests in Windows Sandbox run against it). It uses only the
// library's public API: the toolbar (German texts, as an app passes its language), a writing surface with A4 pages on
// graph paper, a status bar.
// The demo also places a few colored cards (its "pictures", DemoPictures) that the selection takes along.
// Keys: Ctrl+Z / Ctrl+Y undo/redo, Ctrl+X/C/V/D, Del, Ctrl+A (selection); tools (test hooks): 1/2/3 pens,
//       4/5 highlighters, 0 eraser, X erase whole strokes, 6 select, 7 shapes, S text, 8 ruler, 9 rectangle selection,
//       R recognize drawn shapes;
//       Q/W/E/T zoom 0.5/1/2/4, F fit width, Page Down/Up next/previous page, Home first page.
// Environment: INKPAGES_DEMO_LOG (log file), INKPAGES_DEMO_PAGES (page count, default 4),
//              INKPAGES_DEMO_CHOICES (file keeping the tool choices).
// Taps (mouse click, finger tap) are logged ("demo: tap"), as an app would follow a link there.

#include <windows.h>
#include <commctrl.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <memory>
#include <string>

#include "inkpages/Log.h"
#include "inkpages/UiDispatch.h"
#include "inkpages/Toolbar.h"
#include "inkpages/model/PageMap.h"
#include "inkpages/WritingSurface.h"
#include "DemoPictures.h"

namespace
{
    using Stroke = inkpages::KeyedStroke;

    std::unique_ptr<inkpages::WritingSurface> surface;
    std::unique_ptr<inkpages::Toolbar> toolbar;
    std::unique_ptr<DemoPictures> pictures;
    bool ruler = false;
    HWND status = nullptr;
    std::wstring choicesFile;

    // Lazy pages: the strokes of pages far from the view (page coordinates); used on the ink thread only.
    std::map<int, std::vector<Stroke>> parked;
    std::mutex parkedLock; // page operations remap it from the UI thread

    // Ink thread (pagesRemapped): in order with load/store, so a page parked just before the operation follows it.
    void RemapParked(inkpages::PageMap const& map)
    {
        std::scoped_lock guard(parkedLock);
        std::map<int, std::vector<Stroke>> moved;
        for (auto& [page, strokes] : parked)
        {
            if (int to = map(page); to >= 0) moved[to] = std::move(strokes);
        }
        parked = std::move(moved);
    }

    inkpages::PageSpec A4()
    {
        inkpages::PageSpec a4;
        a4.paper.kind = inkpages::PaperKind::Graph;
        a4.paper.spacing = 5 * 72 / 25.4f; // 5 mm squares (page units are points)
        return a4;
    }

    void InsertPage()
    {
        int at = surface->PageInView().value_or(0) + 1; // after the page in view
        inkpages::PageLayout before = surface->Layout();
        surface->InsertPages(at, { A4() });
        pictures->Remap(inkpages::PageMap::Insert(before.Count(), at, 1), before); // the cards and texts follow their pages
        surface->MarkCurrentPage(surface->Layout().Count() > 1);
        inkpages::Log(std::format("demo: page inserted at {}", at));
    }

    void DeletePage()
    {
        int page = surface->PageInView().value_or(0);
        if (surface->Layout().Count() <= 1) return;
        // Removing a page cannot be undone: ask first (TaskDialog: Windows' own dialog).
        int button = 0;
        HRESULT hr = TaskDialog(surface->Window(), nullptr, L"InkPages Demo", L"Seite löschen?",
            L"Das kann nicht rückgängig gemacht werden.", TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, TD_WARNING_ICON, &button);
        if (FAILED(hr)) inkpages::LogHr("TaskDialog", hr);
        if (button != IDYES) return;
        inkpages::PageLayout before = surface->Layout();
        surface->RemovePage(page);
        pictures->Remap(inkpages::PageMap::Remove(before.Count(), page), before);
        surface->MarkCurrentPage(surface->Layout().Count() > 1);
        inkpages::Log(std::format("demo: page {} removed", page));
    }

    void MovePageDown()
    {
        int page = surface->PageInView().value_or(0);
        if (page + 1 >= surface->Layout().Count()) return;
        inkpages::PageLayout before = surface->Layout();
        surface->MovePage(page, page + 1);
        pictures->Remap(inkpages::PageMap::Move(before.Count(), page, page + 1), before);
        inkpages::Log(std::format("demo: page {} moved down", page));
    }

    std::wstring Environment(wchar_t const* name)
    {
        wchar_t value[MAX_PATH]{};
        DWORD length = GetEnvironmentVariableW(name, value, MAX_PATH);
        return length > 0 && length < MAX_PATH ? std::wstring(value, length) : std::wstring();
    }

    // The toolbar in German (what the Xournal Studio and MatheTrainer apps pass).
    inkpages::ToolbarTexts German()
    {
        inkpages::ToolbarTexts t;
        t.pen = L"Stift";
        t.highlighter = L"Textmarker";
        t.presetTip = L"nochmal tippen für Farbe und Stärke";
        t.eraser = L"Radierer";
        t.eraserTip = L"Radierer – die Radierer-Taste des Stifts radiert auch, mit jedem Werkzeug";
        t.wholeStrokeErase = L"Ganze Striche radieren";
        t.wholeStrokeEraseTip = L"Ganze Striche radieren: Radierer und Radierer-Taste des Stifts löschen jeden Strich, "
            L"den sie berühren. Aus: nur, was im Kreis liegt. Der Pfeil: Größe des Kreises";
        t.eraserRadius = L"Größe";
        t.select = L"Auswählen";
        t.selectTip = L"Auswählen: Lasso oder Rechteck (der Pfeil); ziehen verschiebt, Ecken skalieren. Mit dem Stift geht "
            L"das Lasso mit jedem Werkzeug: mit gehaltener Stifttaste ziehen";
        t.lasso = L"Lasso";
        t.rectangle = L"Rechteck";
        t.shapes = L"Formen";
        t.shapesTip = L"Formen: Linie, Pfeil, Rechteck, Ellipse (der Pfeil); gezeichnete Formen können erkannt werden";
        t.text = L"Text";
        t.textTip = L"Text: auf die Seite tippen und schreiben (Tastatur), einen Text antippen zum Ändern, daneben tippen "
            L"zum Beenden. Der Pfeil: Schrift, Größe und Farbe";
        t.shapeKinds = { L"Linie", L"Pfeil", L"Rechteck", L"Ellipse" };
        t.recognizeShapes = L"Gezeichnete Formen erkennen";
        t.ruler = L"Lineal";
        t.protractor = L"Geodreieck";
        t.rulerTip = L"Lineal / Geodreieck (der Pfeil): Striche daran entlang werden gerade";
        t.cut = L"Auswahl ausschneiden (Strg+X)";
        t.copy = L"Auswahl kopieren (Strg+C)";
        t.paste = L"Einfügen (Strg+V)";
        t.duplicate = L"Auswahl duplizieren (Strg+D)";
        t.deleteSelection = L"Auswahl löschen (Entf)";
        t.undo = L"Rückgängig (Strg+Z)";
        t.redo = L"Wiederholen (Strg+Y)";
        t.color = L"Farbe";
        t.width = L"Stärke";
        t.widthFormat = L"{} pt";
        t.decimalSeparator = L',';
        t.penColors = { L"Schwarz", L"Blau", L"Rot", L"Grün", L"Grau", L"Hellblau", L"Hellgrün", L"Magenta", L"Orange", L"Gelb" };
        t.highlighterColors = { L"Gelb", L"Grün", L"Hellblau", L"Magenta", L"Orange", L"Rot", L"Rosa", L"Lavendel",
            L"Blau", L"Türkis", L"Mint", L"Apricot" };
        return t;
    }

    // The demo's cursors (demo/tools/make_cursors.py): a dot per palette color x width step.
    UINT CursorFor(inkpages::Tool tool, inkpages::PenPreset const& preset)
    {
        switch (tool)
        {
        case inkpages::Tool::Pen: return 1000 + preset.color * 10 + inkpages::PenWidths::StepOf(preset.width);
        case inkpages::Tool::Highlighter: return 2000 + preset.color * 10 + inkpages::HighlighterWidths::StepOf(preset.width);
        default: return 0; // the eraser: Windows' cursor (the exact eraser shows its circle)
        }
    }

    inkpages::ToolChoices LoadChoices()
    {
        if (choicesFile.empty()) return {};
        std::ifstream file(choicesFile);
        std::string line;
        std::getline(file, line);
        return inkpages::ToolChoices::Parse(line);
    }

    void SaveChoices(inkpages::ToolChoices const& choices)
    {
        if (choicesFile.empty()) return;
        std::ofstream file(choicesFile, std::ios::trunc);
        file << choices.Serialize() << '\n';
        if (!file) inkpages::LogError("demo: the tool choices could not be saved");
    }

    void UpdateStatus()
    {
        if (!status || !surface) return;
        auto page = surface->PageInView();
        std::wstring text = std::format(L"Zoom {:.0f} %   Seite {} / {}   {}", surface->CurrentView().zoom * 100,
            page ? *page + 1 : 0, surface->Layout().Count(), surface->AtSavePoint() ? L"" : L"geändert");
        SendMessageW(status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }

    void Layout(HWND window)
    {
        RECT rc;
        GetClientRect(window, &rc);
        SendMessageW(status, WM_SIZE, 0, 0);
        RECT sr;
        GetWindowRect(status, &sr);
        int top = toolbar ? toolbar->Height() : 0;
        if (toolbar) MoveWindow(toolbar->Window(), 0, 0, rc.right, top, TRUE);
        if (surface) MoveWindow(surface->Window(), 0, top, rc.right, rc.bottom - top - (sr.bottom - sr.top), TRUE);
    }

    void ChooseTool(inkpages::Tool tool, int preset)
    {
        auto choices = toolbar->Choices();
        choices.tool = tool;
        choices.preset = preset;
        toolbar->Choose(choices);
    }

    void OnKey(WPARAM key)
    {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        int page = surface->PageInView().value_or(0);
        if (ctrl && key == 'Z') surface->Undo();
        else if (ctrl && key == 'Y') surface->Redo();
        else if (ctrl && key == 'X') surface->Cut();
        else if (ctrl && key == 'C') surface->Copy();
        else if (ctrl && key == 'V') surface->Paste();
        else if (ctrl && key == 'D') surface->Duplicate();
        else if (ctrl && key == 'A') surface->SelectAll();
        else if (ctrl) return;
        else if (key == VK_DELETE) surface->DeleteSelection();
        else if (key == '6') ChooseTool(inkpages::Tool::Select, 0);
        else if (key == '9')
        {
            auto choices = toolbar->Choices();
            choices.tool = inkpages::Tool::Select;
            choices.selectMode = inkpages::SelectMode::Rectangle;
            toolbar->Choose(choices);
        }
        else if (key == '7')
        {
            auto choices = toolbar->Choices();
            if (!inkpages::IsDrawingTool(choices.tool)) return;
            choices.ToggleShape(choices.shape);
            toolbar->Choose(choices);
        }
        else if (key == 'S') ChooseTool(inkpages::Tool::Text, 0);
        else if (key == '8') surface->SetRuler(ruler = !ruler);
        else if (key == 'R')
        {
            auto choices = toolbar->Choices();
            choices.recognizeShapes = !choices.recognizeShapes;
            toolbar->Choose(choices);
        }
        else if (key >= '1' && key <= '3') ChooseTool(inkpages::Tool::Pen, static_cast<int>(key - '1'));
        else if (key == '4' || key == '5') ChooseTool(inkpages::Tool::Highlighter, static_cast<int>(key - '4'));
        else if (key == '0') ChooseTool(inkpages::Tool::Eraser, 0);
        else if (key == 'X')
        {
            auto choices = toolbar->Choices();
            choices.wholeStrokeErasing = !choices.wholeStrokeErasing;
            toolbar->Choose(choices);
        }
        else if (key == 'Q') surface->ZoomTo(0.5f);
        else if (key == 'W') surface->ZoomTo(1);
        else if (key == 'E') surface->ZoomTo(2);
        else if (key == 'T') surface->ZoomTo(4);
        else if (key == 'F') surface->FitWidth();
        else if (key == VK_NEXT) surface->ScrollToPage(page + 1);
        else if (key == VK_PRIOR) surface->ScrollToPage(page - 1);
        else if (key == VK_HOME) surface->ScrollToPage(0);
        else return;
        inkpages::Log(std::format("demo: key 0x{:X}{}", key, ctrl ? " with Ctrl" : ""));
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
    {
        try
        {
            switch (message)
            {
            case WM_SIZE:
                Layout(window);
                return 0;
            case WM_KEYDOWN:
                if (surface && toolbar) OnKey(wp);
                return 0;
            case WM_CLOSE:
                if (surface) surface->EndTextEditing();
                break;
            case WM_DESTROY:
                toolbar.reset();
                if (surface) surface->SetTextHost(nullptr); // ends editing while the texts are still there
                if (surface) surface->SetPictureHost(nullptr);
                pictures.reset();
                surface.reset();
                PostQuitMessage(0);
                return 0;
            }
        }
        catch (...)
        {
            inkpages::LogException(std::format("demo: message 0x{:X}", message));
        }
        return DefWindowProcW(window, message, wp, lp);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    inkpages::SetLogFile(Environment(L"INKPAGES_DEMO_LOG"));
    inkpages::Log("demo: start");
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        inkpages::UiDispatcherScope dispatcher;

        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
        if (!InitCommonControlsEx(&controls)) inkpages::LogError("InitCommonControlsEx failed");

        WNDCLASSEXW windowClass{ sizeof(windowClass) };
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = L"InkPagesDemo";
        winrt::check_bool(RegisterClassExW(&windowClass));

        HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"InkPages Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, nullptr);
        winrt::check_pointer(window);
        status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, instance, nullptr);

        surface = inkpages::WritingSurface::Create(window);
        surface->viewChanged = UpdateStatus;
        surface->tapped = [](int page, inkpages::PointF at, inkpages::PointerKind kind) {
            inkpages::Log(std::format("demo: tap ({}) on page {} at {:.0f},{:.0f}",
                kind == inkpages::PointerKind::Mouse ? "mouse" : "touch", page, at.x, at.y));
        };
        surface->historyChanged = UpdateStatus; // before the toolbar: it chains this handler
        surface->load = [](int page) {
            std::scoped_lock guard(parkedLock);
            std::vector<Stroke> strokes;
            if (auto it = parked.find(page); it != parked.end())
            {
                strokes = std::move(it->second);
                parked.erase(it);
            }
            return strokes;
        };
        surface->pagesRemapped = RemapParked;
        surface->store = [](int page, std::vector<Stroke> strokes) {
            std::scoped_lock guard(parkedLock);
            if (!strokes.empty()) parked[page] = std::move(strokes);
        };

        choicesFile = Environment(L"INKPAGES_DEMO_CHOICES");
        std::vector<inkpages::ToolbarButton> pageButtons{
            { 1, 0, L"Seite einfügen (nach der Seite im Blick)", InsertPage, L"Seite einfügen", {}, 20, false, false, inkpages::ToolbarSymbol::InsertPage },
            { 2, 0, L"Seite löschen (fragt vorher)", DeletePage, L"Seite löschen", {}, 20, false, false, inkpages::ToolbarSymbol::DeletePage },
            { 3, 0xE74B, L"Seite nach unten verschieben", MovePageDown, L"Seite nach unten" },
        };
        toolbar = inkpages::Toolbar::Create(window, *surface, LoadChoices(), German(), CursorFor, pageButtons);
        toolbar->choicesChanged = SaveChoices;

        int count = 4;
        if (auto value = Environment(L"INKPAGES_DEMO_PAGES"); !value.empty()) count = std::max(1, _wtoi(value.c_str()));
        surface->SetPages(std::vector<inkpages::PageSpec>(count, A4()));
        surface->MarkCurrentPage(count > 1); // as an app does: the page in view outlined when there are several
        pictures = std::make_unique<DemoPictures>(*surface);
        pictures->Add({ 84, 444, 244, 524 }, { 0xFF, 0x7F, 0xC8, 0xF0 });
        pictures->Add({ 300, 444, 400, 524 }, { 0xFF, 0xF0, 0xC0, 0x7F });
        surface->SetPictureHost(pictures.get());
        surface->SetTextHost(pictures.get());

        ShowWindow(window, show == SW_SHOWDEFAULT ? SW_SHOWMAXIMIZED : show);
        Layout(window);
        surface->FitWidth();
        UpdateStatus();
        inkpages::Log(std::format("demo: window shown, DPI {}, {} pages", GetDpiForWindow(window), count));

        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        inkpages::Log("demo: end");
        return static_cast<int>(message.wParam);
    }
    catch (...)
    {
        inkpages::LogException("demo");
        return 1;
    }
}
