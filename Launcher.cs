using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        Native.SetProcessDPIAware();
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new Launcher());
    }
}

internal sealed class Launcher : Form, IMessageFilter
{
    private static readonly Size[] Resolutions =
    {
        Size.Empty, new Size(1024, 768), new Size(1280, 1024),
        new Size(1600, 900), new Size(1920, 1080),
    };

    private static readonly string[] Limits = { "-1", "30", "60", "120", "144", "0" };

    private static readonly VideoFilter[] Filters =
    {
        new VideoFilter("Suave", "auto", "2", @"interpolation\catmull-rom-bilinear.glsl"),
        new VideoFilter("Pixelado", "auto", "0", @"nearest-neighbor.glsl"),
        new VideoFilter("Bilinear", "auto", "1", @"interpolation\bilinear.glsl"),
        new VideoFilter("Lanczos nítido", "opengl", "2", @"interpolation\lanczos2-sharp.glsl"),
        new VideoFilter("FSR", "opengl", "2", @"interpolation\fsr.glsl"),
        new VideoFilter("Jinc2", "opengl", "2", @"interpolation\jinc2-dedither.glsl"),
        new VideoFilter("xBR", "opengl", "2", @"xbr\xbr-lv2-noblend.glsl"),
        new VideoFilter("xBRZ", "opengl", "2", @"xbrz\xbrz-freescale-multipass.glsl"),
        new VideoFilter("Scanlines", "opengl", "2", @"scanlines\scanline.glsl"),
        new VideoFilter("CRT", "opengl", "2", @"crt\crt-lottes-fast-no-warp-bilinear.glsl"),
    };

    private readonly string folder = AppDomain.CurrentDomain.BaseDirectory;
    private readonly ComboBox resolution = new ComboBox();
    private readonly ComboBox language = new ComboBox();
    private readonly ComboBox mode = new ComboBox();
    private readonly ComboBox filter = new ComboBox();
    private readonly ComboBox limit = new ComboBox();
    private readonly CheckBox vsync = new CheckBox { Text = "VSync", AutoSize = true };
    private readonly CheckBox showFps = new CheckBox { Text = "Mostrar FPS no jogo", AutoSize = true };
    private readonly CheckBox wideHud = new CheckBox { AutoSize = true };
    private readonly CheckBox lockCursor = new CheckBox { AutoSize = true };
    private readonly CheckBox wasdCamera = new CheckBox { AutoSize = true };
    private readonly TabControl tabs = new TabControl { Dock = DockStyle.Fill };
    private readonly TabPage videoTab = new TabPage();
    private readonly TabPage controlsTab = new TabPage();
    private readonly TabPage originalTab = new TabPage();
    private readonly Label originalKeys = new Label { AutoSize = true };
    private readonly Label bindingHint = new Label { AutoSize = true, MaximumSize = new Size(470, 0) };
    private readonly Button resetBindings = new Button { AutoSize = true };
    private readonly Label[] bindingLabels = new Label[8];
    private readonly Button[] bindingButtons = new Button[8];
    private readonly int[] bindings = new int[8];
    private int capturing = -1;
    private Keys pendingModifier;
    private static readonly int[] DefaultBindings = {116, 117, 118, 115, 628, 629, 630, 18};
    private readonly CheckBox exclusiveParty = new CheckBox { AutoSize = true };
    private readonly CheckBox clickMarker = new CheckBox { AutoSize = true };
    private readonly CheckBox smoothCamera = new CheckBox { AutoSize = true };
    private readonly CheckBox intro = new CheckBox { Text = "Reproduzir abertura", AutoSize = true };
    private readonly Button play = new Button { Text = "Jogar", AutoSize = true };
    private readonly Label status = new Label { AutoSize = true };
    private readonly Timer monitor = new Timer { Interval = 125 };
    private readonly Stopwatch sample = new Stopwatch();
    private readonly byte[] counterBytes = new byte[4];
    private Process game;
    private FpsBadge badge;
    private uint lastCount;
    private bool hasSample;
    private bool gameEnded;
    private int gameExitCode;
    private readonly Label[] fieldLabels = new Label[5];

    private string WrapperIni { get { return Path.Combine(folder, "ddraw.ini"); } }
    private string LauncherIni { get { return Path.Combine(folder, "Akuma.ini"); } }

    public Launcher()
    {
        Text = "Akuma — Demon Spawn";
        Font = new Font("Segoe UI", 10);
        AutoScaleMode = AutoScaleMode.Dpi;
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        ClientSize = new Size(535, 550);
        Padding = new Padding(16);
        string gameFile = Path.Combine(folder, "AkumaHD.exe");
        Icon = File.Exists(gameFile) ? Icon.ExtractAssociatedIcon(gameFile) ?? SystemIcons.Application : SystemIcons.Application;

        var shell = new TableLayoutPanel
        {
            ColumnCount = 1, RowCount = 3, Dock = DockStyle.Fill,
        };
        shell.RowStyles.Add(new RowStyle(SizeType.Absolute, 46));
        shell.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        shell.RowStyles.Add(new RowStyle(SizeType.Absolute, 49));
        Controls.Add(shell);
        var heading = new Label { Text = "Akuma · Demon Spawn", Dock = DockStyle.Fill,
            Font = new Font("Segoe UI", 15, FontStyle.Bold),
            TextAlign = ContentAlignment.MiddleLeft };
        shell.Controls.Add(heading, 0, 0);
        tabs.ImageList = new ImageList { ImageSize = new Size(18, 18), ColorDepth = ColorDepth.Depth32Bit };
        tabs.ImageList.Images.Add(SystemIcons.Application);
        tabs.ImageList.Images.Add(SystemIcons.Shield);
        tabs.ImageList.Images.Add(SystemIcons.Information);
        tabs.TabPages.AddRange(new[] {videoTab, controlsTab, originalTab});
        for (int i = 0; i < tabs.TabCount; ++i) tabs.TabPages[i].ImageIndex = i;
        tabs.SelectedIndexChanged += delegate { CancelCapture(); };
        shell.Controls.Add(tabs, 0, 1);
        var footer = new Panel { Dock = DockStyle.Fill, Padding = new Padding(0, 8, 0, 0) };
        status.AutoSize = false;
        status.Dock = DockStyle.Fill;
        status.TextAlign = ContentAlignment.MiddleLeft;
        play.AutoSize = false;
        play.Dock = DockStyle.Right;
        play.Width = 108;
        footer.Controls.Add(status);
        footer.Controls.Add(play);
        shell.Controls.Add(footer, 0, 2);
        var layout = PageLayout(videoTab);
        var controls = PageLayout(controlsTab);
        AddWide(PageLayout(originalTab), originalKeys);

        fieldLabels[0] = AddChoice(layout, "Idioma", language, new object[] { "Português", "English", "한국어" });
        fieldLabels[1] = AddChoice(layout, "Modo", mode, new object[] { "Janela", "Tela cheia sem bordas" });
        fieldLabels[2] = AddChoice(layout, "Resolução da janela", resolution, new object[] {
            "Original · 640 × 480", "1024 × 768", "1280 × 1024", "1600 × 900", "1920 × 1080"
        });
        fieldLabels[3] = AddChoice(layout, "Filtro gráfico", filter, Array.ConvertAll(Filters, item => (object)item.Label));
        fieldLabels[4] = AddChoice(layout, "Limite de FPS", limit, new object[] {
            "Taxa do monitor", "30", "60", "120", "144", "Sem limite"
        });
        AddWide(layout, wideHud);
        AddWide(layout, vsync);
        AddWide(layout, showFps);
        AddWide(layout, intro);
        for (int i = 0; i < bindingButtons.Length; ++i)
        {
            int slot = i;
            int row = controls.RowCount++;
            controls.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
            bindingLabels[i] = new Label { AutoSize = true, Anchor = AnchorStyles.Left };
            bindingButtons[i] = new Button { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter };
            bindingButtons[i].Click += delegate { capturing = slot; pendingModifier = Keys.None; ShowBindings(); };
            controls.Controls.Add(bindingLabels[i], 0, row);
            controls.Controls.Add(bindingButtons[i], 1, row);
        }
        AddWide(controls, lockCursor);
        AddWide(controls, wasdCamera);
        AddWide(controls, exclusiveParty);
        AddWide(controls, clickMarker);
        AddWide(controls, smoothCamera);
        AddWide(controls, resetBindings);
        AddWide(controls, bindingHint);
        resetBindings.Click += delegate { Array.Copy(DefaultBindings, bindings, bindings.Length); CancelCapture(); };
        AcceptButton = play;

        LoadSettings();
        language.SelectedIndexChanged += delegate { ApplyLanguage(); };
        ApplyLanguage();
        mode.SelectedIndexChanged += delegate { resolution.Enabled = mode.SelectedIndex == 0; };
        play.Click += StartGame;
        monitor.Tick += UpdateGame;
        Application.AddMessageFilter(this);
    }

    private static TableLayoutPanel PageLayout(TabPage page)
    {
        page.UseVisualStyleBackColor = true;
        page.AutoScroll = true;
        var layout = new TableLayoutPanel { AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink,
            ColumnCount = 2, Padding = new Padding(12), Dock = DockStyle.Top };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 51));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 49));
        page.Controls.Add(layout);
        return layout;
    }

    private void CancelCapture()
    {
        capturing = -1;
        pendingModifier = Keys.None;
        ShowBindings();
    }

    private void ShowBindings()
    {
        for (int i = 0; i < bindingButtons.Length; ++i)
        {
            if (bindingButtons[i] == null) continue;
            int value = bindings[i];
            string key = value == 0 ? TextInLanguage("Desativado", "Disabled", "사용 안 함") :
                value == (int)Keys.Menu ? "Alt" : value == (int)Keys.ControlKey ? "Ctrl" :
                new KeysConverter().ConvertToString((Keys)((value & 255) | ((value & 1792) << 8)));
            bindingButtons[i].Text = i == capturing ? TextInLanguage("Pressione uma tecla…", "Press a key…", "키를 누르세요…") : key;
        }
        bindingHint.Text = TextInLanguage("Clique em um atalho para editar. Backspace limpa e Esc cancela.",
            "Click a shortcut to edit. Backspace clears it and Esc cancels.", "단축키를 클릭해 편집하세요. Backspace로 지우고 Esc로 취소합니다.");
    }

    private static bool ReservedBinding(int binding)
    {
        const int alt = 1024, control = 512;
        if (binding == (alt | (int)Keys.Enter) || binding == (control | (int)Keys.Tab)) return true;
        if ((binding >= (int)Keys.F8 && binding <= (int)Keys.F10) || binding == (int)Keys.F12) return true;
        if (binding >= (int)Keys.D0 && binding <= (int)Keys.D9) return true;
        return Array.IndexOf(new[] {(int)Keys.Tab, (int)Keys.Enter, (int)Keys.Space, (int)Keys.ShiftKey, (int)Keys.Left,
            (int)Keys.Right, (int)Keys.Up, (int)Keys.Down, 'A', 'C', 'D', 'I', 'M', 'S', 'V', 'W'}, binding) >= 0;
    }

    public bool PreFilterMessage(ref Message msg)
    {
        if (capturing < 0 || Form.ActiveForm != this) return false;
        bool down = msg.Msg == 0x100 || msg.Msg == 0x104;
        bool up = msg.Msg == 0x101 || msg.Msg == 0x105;
        if (!down && !up) return false;
        Keys key = (Keys)msg.WParam.ToInt32() & Keys.KeyCode;
        if (key == Keys.Escape) { CancelCapture(); return true; }
        bool modifier = key == Keys.ControlKey || key == Keys.ShiftKey || key == Keys.Menu;
        if (modifier && down)
        {
            pendingModifier = capturing == 7 && ModifierKeys ==
                (key == Keys.ControlKey ? Keys.Control : key == Keys.Menu ? Keys.Alt : Keys.Shift) ? key : Keys.None;
            return true;
        }
        if (up && !(modifier && key == pendingModifier)) return true;
        int binding = key == Keys.Back ? 0 : (int)key | (modifier ? 0 : (((int)ModifierKeys >> 8) & 1792));
        pendingModifier = Keys.None;
        if (ReservedBinding(binding) || (binding != 0 && Array.Exists(bindings, value => value == binding)
            && bindings[capturing] != binding))
        {
            bindingHint.Text = TextInLanguage("Tecla em uso. Escolha outra combinação.",
                "That shortcut is in use. Choose another combination.", "이미 사용 중인 단축키입니다.");
            return true;
        }
        bindings[capturing] = binding;
        CancelCapture();
        return true;
    }

    private static Label AddChoice(TableLayoutPanel layout, string label, ComboBox choice, object[] items)
    {
        int row = layout.RowCount++;
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var caption = new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left };
        layout.Controls.Add(caption, 0, row);
        choice.DropDownStyle = ComboBoxStyle.DropDownList;
        choice.Dock = DockStyle.Fill;
        choice.Items.AddRange(items);
        layout.Controls.Add(choice, 1, row);
        return caption;
    }

    private static void AddWide(TableLayoutPanel layout, Control control)
    {
        int row = layout.RowCount++;
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.Controls.Add(control, 0, row);
        layout.SetColumnSpan(control, 2);
    }

    private void LoadSettings()
    {
        language.SelectedIndex = ReadIndex("language", language.Items.Count);
        Rectangle desktop = Screen.PrimaryScreen.WorkingArea;
        int defaultResolution = desktop.Width >= 1600 && desktop.Height >= 940 ? 3 : 1;
        resolution.SelectedIndex = ReadIndex("resolution", Resolutions.Length, defaultResolution);
        wideHud.Checked = ReadIndex("wide_hud", 2, 1) != 0;
        lockCursor.Checked = ReadIndex("lock_cursor", 2, 1) != 0;
        wasdCamera.Checked = ReadIndex("wasd_camera", 2, 0) != 0;
        int previousProfile = ReadIndex("party_keys", 3);
        for (int i = 0; i < bindings.Length; ++i)
        {
            int fallback = DefaultBindings[i];
            if (i < 4 && previousProfile == 1) fallback = (int)Keys.NumPad1 + i;
            if (i < 4 && previousProfile == 2) fallback = 0;
            bindings[i] = ReadIndex("binding_" + i, 2048, fallback);
        }
        exclusiveParty.Checked = ReadIndex("exclusive_party", 2) != 0;
        clickMarker.Checked = ReadIndex("click_marker", 2, 0) != 0;
        smoothCamera.Checked = ReadIndex("smooth_camera", 2, 0) != 0;
        mode.SelectedIndex = ReadIndex("mode", mode.Items.Count);
        filter.SelectedIndex = ReadIndex("filter", Filters.Length);
        limit.SelectedIndex = ReadIndex("limit", Limits.Length);
        vsync.Checked = ReadIndex("vsync", 2) != 0;
        showFps.Checked = Native.ReadIni(LauncherIni, "video", "show_fps", "1") == "1";
        resolution.Enabled = mode.SelectedIndex == 0;
        intro.Checked = ReadIndex("intro", 2) != 0;

    }

    private int ReadIndex(string key, int count, int fallback = 0)
    {
        int value;
        return Int32.TryParse(Native.ReadIni(LauncherIni, "video", key, fallback.ToString(CultureInfo.InvariantCulture)), out value)
            && value >= 0 && value < count ? value : fallback;
    }

    private string TextInLanguage(string portuguese, string english, string korean)
    {
        if (language.SelectedIndex == 2) return korean;
        if (language.SelectedIndex == 1) return english;
        return portuguese;
    }

    private void ApplyLanguage()
    {
        fieldLabels[0].Text = TextInLanguage("Idioma", "Language", "언어");
        fieldLabels[1].Text = TextInLanguage("Modo", "Display mode", "화면 모드");
        fieldLabels[2].Text = TextInLanguage("Resolução da janela", "Window resolution", "창 해상도");
        fieldLabels[3].Text = TextInLanguage("Filtro gráfico", "Graphics filter", "그래픽 필터");
        fieldLabels[4].Text = TextInLanguage("Limite de FPS", "FPS limit", "FPS 제한");
        videoTab.Text = TextInLanguage("Vídeo", "Video", "화면");
        controlsTab.Text = TextInLanguage("Controles", "Controls", "조작");
        originalTab.Text = TextInLanguage("Referência", "Reference", "참고");
        string[] actions = new[] {
            TextInLanguage("Selecionar retrato", "Select portrait", "초상화 선택"),
            TextInLanguage("Alternar seleção de todos", "Toggle entire party", "파티 전체 선택 전환"),
            TextInLanguage("Ir até o retrato", "Switch to portrait", "초상화로 이동"),
            TextInLanguage("Mostrar itens no chão (segurar)", "Show ground items (hold)", "바닥 아이템 표시 (누르기)") };
        for (int i = 0; i < 3; ++i)
        {
            bindingLabels[i].Text = actions[0] + " " + (i + 1);
            bindingLabels[i + 4].Text = actions[2] + " " + (i + 1);
        }
        bindingLabels[3].Text = actions[1];
        bindingLabels[7].Text = actions[3];
        resetBindings.Text = TextInLanguage("Restaurar atalhos", "Restore shortcuts", "단축키 복원");
        originalKeys.Text = TextInLanguage(
            "Akuma\n\n0–9    Itens rápidos\nC       Personagem\nI        Itens\nM      Missões\nW      Mapa mundial (com WASD desligado)\nEsc   Menu ou voltar\nSetas   Câmera\n\nRemaster\n\nF8    Zoom\nF9    Minimapa\nF10  HUD\nAlt+Enter    Tela cheia\nCtrl+Tab     Soltar mouse",
            "Akuma\n\n0–9    Quick items\nC       Character\nI        Items\nM      Quests\nW      World map (WASD off)\nEsc   Menu or back\nArrows   Camera\n\nRemaster\n\nF8    Zoom\nF9    Minimap\nF10  HUD\nAlt+Enter    Fullscreen\nCtrl+Tab     Release mouse",
            "아쿠마\n\n0–9    빠른 아이템\nC       캐릭터\nI        아이템\nM      임무\nW      세계 지도 (WASD 끔)\nEsc   메뉴 / 뒤로\n방향키   카메라\n\n리마스터\n\nF8    확대/축소\nF9    미니맵\nF10  HUD\nAlt+Enter    전체 화면\nCtrl+Tab     마우스 해제");
        ShowBindings();
        exclusiveParty.Text = TextInLanguage("Selecionar um personagem por vez",
            "Select one character at a time", "캐릭터 한 명씩 선택");
        smoothCamera.Text = TextInLanguage("Câmera suave (60 fps)", "Smooth camera (60 fps)", "부드러운 카메라 (60fps)");
        clickMarker.Text = TextInLanguage("Marcador ao clicar no chão", "Ground click marker", "바닥 클릭 표시");
        showFps.Text = TextInLanguage("Mostrar FPS no jogo", "Show FPS in game", "게임에서 FPS 표시");
        wideHud.Text = TextInLanguage("HUD lateral (16:9)", "Widescreen HUD (16:9)", "와이드 HUD (16:9)");
        wasdCamera.Text = TextInLanguage("Mover câmera com WASD", "Move camera with WASD", "WASD로 카메라 이동");
        lockCursor.Text = TextInLanguage("Prender mouse na janela (Ctrl+Tab solta)",
            "Capture mouse in window (Ctrl+Tab releases)", "창에 마우스 고정 (Ctrl+Tab으로 해제)");
        intro.Text = TextInLanguage("Reproduzir abertura", "Play intro videos", "오프닝 영상 재생");
        play.Text = TextInLanguage("Jogar", "Play", "게임 시작");

        int modeSelection = mode.SelectedIndex;
        int resolutionSelection = resolution.SelectedIndex;
        int filterSelection = filter.SelectedIndex;
        int limitSelection = limit.SelectedIndex;
        mode.Items[0] = TextInLanguage("Janela", "Windowed", "창 모드");
        mode.Items[1] = TextInLanguage("Tela cheia sem bordas", "Borderless fullscreen", "테두리 없는 전체 화면");
        resolution.Items[0] = TextInLanguage("Original", "Original", "원본") + " · 640 × 480";
        filter.Items[0] = TextInLanguage("Suave", "Smooth", "부드럽게");
        filter.Items[1] = TextInLanguage("Pixelado", "Pixelated", "픽셀 유지");
        filter.Items[2] = TextInLanguage("Bilinear", "Bilinear", "선형 보간");
        filter.Items[3] = TextInLanguage("Lanczos nítido", "Lanczos sharp", "선명한 Lanczos");
        limit.Items[0] = TextInLanguage("Taxa do monitor", "Monitor refresh rate", "모니터 주사율");
        limit.Items[5] = TextInLanguage("Sem limite", "Unlimited", "제한 없음");
        mode.SelectedIndex = modeSelection;
        resolution.SelectedIndex = resolutionSelection;
        filter.SelectedIndex = filterSelection;
        limit.SelectedIndex = limitSelection;
        RefreshStatus();
    }

    private void RefreshStatus()
    {
        if (game != null)
            status.Text = TextInLanguage("Jogo em execução.", "Game is running.", "게임 실행 중.");
        else if (!gameEnded)
            status.Text = TextInLanguage("Alt+Enter alterna tela cheia. F8 muda o zoom.",
                "Alt+Enter toggles fullscreen. F8 changes zoom.", "Alt+Enter는 전체 화면, F8은 확대/축소 전환.");
        else if (gameExitCode == 0)
            status.Text = TextInLanguage("Jogo encerrado.", "Game closed.", "게임을 종료했습니다.");
        else
            status.Text = String.Format(TextInLanguage("Jogo encerrado com código 0x{0:X8}.",
                "Game exited with code 0x{0:X8}.", "게임 종료 코드 0x{0:X8}."), gameExitCode);
    }

    private void SaveSettings()
    {
        var preset = Resolutions[resolution.SelectedIndex];
        var effect = Filters[filter.SelectedIndex];
        Native.WriteIni(WrapperIni, "ddraw", "windowed", "true");
        Native.WriteIni(WrapperIni, "ddraw", "fullscreen", mode.SelectedIndex == 1 ? "true" : "false");
        Native.WriteIni(WrapperIni, "ddraw", "width", preset.Width);
        Native.WriteIni(WrapperIni, "ddraw", "height", preset.Height);
        Native.WriteIni(WrapperIni, "ddraw", "renderer", effect.Renderer);
        Native.WriteIni(WrapperIni, "ddraw", "d3d9_filter", effect.D3DFilter);
        Native.WriteIni(WrapperIni, "ddraw", "shader", @"Shaders\" + effect.Shader);
        Native.WriteIni(WrapperIni, "ddraw", "maxfps", Limits[limit.SelectedIndex]);
        Native.WriteIni(WrapperIni, "ddraw", "vsync", vsync.Checked ? "true" : "false");
        Native.WriteIni(WrapperIni, "ddraw", "devmode", lockCursor.Checked ? "false" : "true");
        Native.WriteIni(WrapperIni, "ddraw", "lock_surfaces", "true");

        Native.WriteIni(LauncherIni, "video", "language", language.SelectedIndex);
        Native.WriteIni(LauncherIni, "video", "resolution", resolution.SelectedIndex);
        Native.WriteIni(LauncherIni, "video", "mode", mode.SelectedIndex);
        Native.WriteIni(LauncherIni, "video", "filter", filter.SelectedIndex);
        Native.WriteIni(LauncherIni, "video", "limit", limit.SelectedIndex);
        Native.WriteIni(LauncherIni, "video", "vsync", vsync.Checked ? "1" : "0");
        Native.WriteIni(LauncherIni, "video", "show_fps", showFps.Checked ? "1" : "0");
        Native.WriteIni(LauncherIni, "video", "wide_hud", wideHud.Checked ? "1" : "0");
        Native.WriteIni(LauncherIni, "video", "lock_cursor", lockCursor.Checked ? "1" : "0");
        Native.WriteIni(LauncherIni, "video", "wasd_camera", wasdCamera.Checked ? "1" : "0");
        for (int i = 0; i < bindings.Length; ++i)
            Native.WriteIni(LauncherIni, "video", "binding_" + i, bindings[i]);
        Native.WriteIni(LauncherIni, "video", "exclusive_party", exclusiveParty.Checked ? "1" : "0");
        Native.WriteIni(LauncherIni, "video", "click_marker", clickMarker.Checked ? "1" : "0");
        Native.WriteIni(LauncherIni, "video", "smooth_camera", smoothCamera.Checked ? "1" : "0");
        Native.WriteIni(LauncherIni, "video", "intro", intro.Checked ? "1" : "0");
    }

    private void StartGame(object sender, EventArgs args)
    {
        try
        {
            SaveSettings();
            var start = new ProcessStartInfo(Path.Combine(folder, "AkumaHD.exe"))
            {
                WorkingDirectory = folder,
                UseShellExecute = false,
                Arguments = (intro.Checked ? "-show-intro " : "-skip-intro ") + new[] { "pt", "en", "ko" }[language.SelectedIndex],
            };
            // Keep each extracted copy bound to its own wrapper configuration.
            start.EnvironmentVariables["CNC_DDRAW_CONFIG_FILE"] = WrapperIni;
            start.EnvironmentVariables["AKUMA_WIDE_HUD"] = wideHud.Checked ? "1" : "0";
            start.EnvironmentVariables["AKUMA_WASD_CAMERA"] = wasdCamera.Checked ? "1" : "0";
            start.EnvironmentVariables["AKUMA_BINDINGS"] = String.Join(",", Array.ConvertAll(bindings,
                value => value.ToString(CultureInfo.InvariantCulture)));
            start.EnvironmentVariables["AKUMA_LANGUAGE"] = new[] { "pt", "en", "ko" }[language.SelectedIndex];
            start.EnvironmentVariables["AKUMA_EXCLUSIVE_PARTY"] = exclusiveParty.Checked ? "1" : "0";
            start.EnvironmentVariables["AKUMA_CLICK_MARKER"] = clickMarker.Checked ? "1" : "0";
            start.EnvironmentVariables["AKUMA_SMOOTH_CAMERA"] = smoothCamera.Checked ? "1" : "0";
            start.EnvironmentVariables["AKUMA_PIXEL_ZOOM"] = filter.SelectedIndex == 1 ? "1" : "0";
            game = Process.Start(start);
            play.Enabled = false;
            hasSample = false;
            sample.Reset();
            RefreshStatus();
            WindowState = FormWindowState.Minimized;
            monitor.Start();
        }
        catch (Exception error)
        {
            MessageBox.Show(this, error.Message, TextInLanguage("Falha ao abrir Akuma", "Could not start Akuma", "아쿠마 실행 실패"), MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void UpdateGame(object sender, EventArgs args)
    {
        if (game.HasExited)
        {
            gameExitCode = game.ExitCode;
            gameEnded = true;
            monitor.Stop();
            game.Dispose();
            game = null;
            if (badge != null)
            {
                badge.Dispose();
                badge = null;
            }
            play.Enabled = true;
            WindowState = FormWindowState.Normal;
            RefreshStatus();
            return;
        }

        if (!showFps.Checked)
        {
            if (badge != null) badge.Hide();
            return;
        }

        IntPtr active = Native.GetForegroundWindow();
        uint pid;
        Native.GetWindowThreadProcessId(active, out pid);
        Native.Rect rect;
        var origin = new Native.Point();
        if (pid != (uint)game.Id || !Native.GetClientRect(active, out rect)
            || rect.Right < 320 || rect.Bottom < 200 || !Native.ClientToScreen(active, ref origin))
        {
            if (badge != null) badge.Hide();
            hasSample = false;
            return;
        }

        IntPtr bytesRead;
        if (!Native.ReadProcessMemory(game.Handle, new IntPtr(0x4D3000), counterBytes, counterBytes.Length, out bytesRead)
            || bytesRead.ToInt64() != counterBytes.Length)
        {
            if (badge != null) badge.Hide();
            return;
        }

        uint count = BitConverter.ToUInt32(counterBytes, 0);
        if (!hasSample)
        {
            lastCount = count;
            hasSample = true;
            sample.Restart();
        }
        else if (sample.ElapsedMilliseconds >= 1000)
        {
            double fps = unchecked(count - lastCount) / sample.Elapsed.TotalSeconds;
            if (badge == null) badge = new FpsBadge();
            badge.Value.Text = String.Format(CultureInfo.InvariantCulture, "{0:0} FPS", fps);
            lastCount = count;
            sample.Restart();
        }

        if (badge != null)
        {
            badge.Location = new Point(origin.X + rect.Right - badge.Width - 10, origin.Y + 10);
            if (!badge.Visible) badge.Show();
        }
    }

    protected override void OnFormClosed(FormClosedEventArgs args)
    {
        Application.RemoveMessageFilter(this);
        monitor.Dispose();
        if (badge != null) badge.Dispose();
        if (game != null) game.Dispose();
        base.OnFormClosed(args);
    }
}

internal sealed class VideoFilter
{
    public readonly string Label, Renderer, D3DFilter, Shader;

    public VideoFilter(string label, string renderer, string d3dFilter, string shader)
    {
        Label = label;
        Renderer = renderer;
        D3DFilter = d3dFilter;
        Shader = shader;
    }
}

internal sealed class FpsBadge : Form
{
    public readonly Label Value = new Label
    {
        Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter,
        ForeColor = Color.WhiteSmoke, Font = new Font("Segoe UI", 10, FontStyle.Bold),
    };

    public FpsBadge()
    {
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        ShowInTaskbar = false;
        TopMost = true;
        ClientSize = new Size(85, 29);
        BackColor = Color.FromArgb(30, 24, 22);
        Opacity = 0.86;
        Controls.Add(Value);
    }

    protected override bool ShowWithoutActivation { get { return true; } }

    protected override CreateParams CreateParams
    {
        get
        {
            var parameters = base.CreateParams;
            parameters.ExStyle |= 0x08000000 | 0x00000020 | 0x00000080;
            return parameters;
        }
    }
}

internal static class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Point { public int X, Y; }

    [DllImport("user32.dll")]
    internal static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")]
    internal static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    internal static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")]
    internal static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")]
    internal static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("kernel32.dll", SetLastError = true)]
    internal static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, int size, out IntPtr read);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern uint GetPrivateProfileString(string section, string key, string fallback,
        StringBuilder value, int capacity, string file);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool WritePrivateProfileString(string section, string key, string value, string file);

    internal static string ReadIni(string file, string section, string key, string fallback)
    {
        var value = new StringBuilder(32);
        GetPrivateProfileString(section, key, fallback, value, value.Capacity, file);
        return value.ToString();
    }

    internal static void WriteIni(string file, string section, string key, object value)
    {
        if (!WritePrivateProfileString(section, key, Convert.ToString(value, CultureInfo.InvariantCulture), file))
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }
}



