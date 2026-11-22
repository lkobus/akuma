using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Net.Sockets;
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
    private readonly TabPage onlineTab = new TabPage();
    private readonly CheckBox online = new CheckBox { AutoSize = true };
    private readonly ComboBox relay = new ComboBox();
    private readonly TextBox netName = new TextBox { MaxLength = 32 };
    private readonly TextBox netPass = new TextBox { MaxLength = 32, UseSystemPasswordChar = true };
    private readonly TextBox nick = new TextBox { MaxLength = 32 };
    private readonly Button testRelay = new Button { AutoSize = true };
    private readonly Label onlineInfo = new Label { AutoSize = true, MaximumSize = new Size(470, 0) };
    private readonly Label[] onlineLabels = new Label[4];
    private readonly List<string[]> relays = new List<string[]>();
    private readonly Label originalKeys = new Label { AutoSize = true };
    private readonly Label bindingHint = new Label { AutoSize = true, MaximumSize = new Size(470, 0) };
    private readonly Button resetBindings = new Button { AutoSize = true };
    private const int CustomBindings = 8;
    // Akuma's own keyboard actions, in the order of the game's key tables (WM_KEYDOWN/WM_KEYUP).
    // The game ignores everything else on the keyboard, so these are all it can be asked to remap.
    private static readonly int[] NativeKeys = {27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
        'C', 'I', 'M', 'W', 'A', 'S', 32};
    private static readonly string[][] NativeNames = {
        new[] {"Menu / voltar", "Menu / back", "메뉴 / 뒤로", "選單 / 返回"},
        new[] {"Item rápido 1", "Quick item 1", "빠른 아이템 1", "快捷道具 1"},
        new[] {"Item rápido 2", "Quick item 2", "빠른 아이템 2", "快捷道具 2"},
        new[] {"Item rápido 3", "Quick item 3", "빠른 아이템 3", "快捷道具 3"},
        new[] {"Item rápido 4", "Quick item 4", "빠른 아이템 4", "快捷道具 4"},
        new[] {"Item rápido 5", "Quick item 5", "빠른 아이템 5", "快捷道具 5"},
        new[] {"Item rápido 6", "Quick item 6", "빠른 아이템 6", "快捷道具 6"},
        new[] {"Item rápido 7", "Quick item 7", "빠른 아이템 7", "快捷道具 7"},
        new[] {"Item rápido 8", "Quick item 8", "빠른 아이템 8", "快捷道具 8"},
        new[] {"Item rápido 9", "Quick item 9", "빠른 아이템 9", "快捷道具 9"},
        new[] {"Item rápido 10", "Quick item 10", "빠른 아이템 10", "快捷道具 10"},
        new[] {"Personagem", "Character", "캐릭터", "角色資訊"},
        new[] {"Itens", "Items", "아이템", "道具"},
        new[] {"Missões", "Quests", "임무", "任務"},
        new[] {"Mapa mundial", "World map", "세계 지도", "世界地圖"},
        new[] {"Aba de armas", "Weapons tab", "무기 탭", "武器分頁"},
        new[] {"Aba de itens", "Items tab", "아이템 탭", "道具分頁"},
        new[] {"Ação nativa (Espaço)", "Native action (Space)", "기본 동작 (스페이스)", "原生動作 (空白鍵)"} };
    private readonly Label[] bindingLabels = new Label[CustomBindings + NativeKeys.Length];
    private readonly Button[] bindingButtons = new Button[CustomBindings + NativeKeys.Length];
    private readonly int[] bindings = new int[CustomBindings + NativeKeys.Length];
    private readonly Label nativeHeader = new Label { AutoSize = true, Font = new Font("Segoe UI", 10, FontStyle.Bold) };
    private int capturing = -1;
    private Keys pendingModifier;
    private static readonly int[] DefaultBindings = MakeDefaults();
    private static int[] MakeDefaults()
    {
        var all = new int[CustomBindings + NativeKeys.Length];
        Array.Copy(new[] {116, 117, 118, 115, 628, 629, 630, 18}, all, CustomBindings);
        Array.Copy(NativeKeys, 0, all, CustomBindings, NativeKeys.Length);
        return all;
    }
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
    private readonly Label[] fieldLabels = new Label[5];   // [0] não é mais usado (idioma virou bandeiras)
    private static readonly string[] LanguageNames = { "Português (Brasil)", "English", "한국어", "繁體中文" };
    private readonly FlagButton[] flags = new FlagButton[4];
    private readonly ToolTip flagTips = new ToolTip();

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
        // Bandeiras no cabeçalho: um clique escolhe o idioma, em qualquer aba.
        var header = new Panel { Dock = DockStyle.Fill };
        var flagBar = new FlowLayoutPanel { Dock = DockStyle.Right, AutoSize = true, WrapContents = false,
            FlowDirection = FlowDirection.LeftToRight, Padding = new Padding(0, 6, 0, 0) };
        for (int i = 0; i < flags.Length; ++i)
        {
            int index = i;
            flags[i] = new FlagButton(i);
            flags[i].Click += delegate { language.SelectedIndex = index; };
            flagBar.Controls.Add(flags[i]);
        }
        header.Controls.Add(heading);
        header.Controls.Add(flagBar);
        shell.Controls.Add(header, 0, 0);
        tabs.ImageList = new ImageList { ImageSize = new Size(18, 18), ColorDepth = ColorDepth.Depth32Bit };
        tabs.ImageList.Images.Add(SystemIcons.Application);
        tabs.ImageList.Images.Add(SystemIcons.Shield);
        tabs.ImageList.Images.Add(SystemIcons.Information);
        tabs.ImageList.Images.Add(SystemIcons.Question);
        tabs.TabPages.AddRange(new[] {videoTab, controlsTab, originalTab, onlineTab});
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
        var onlineLayout = PageLayout(onlineTab);
        AddWide(onlineLayout, online);
        relay.DropDownStyle = ComboBoxStyle.DropDown;
        onlineLabels[0] = AddField(onlineLayout, relay);
        onlineLabels[1] = AddField(onlineLayout, netName);
        onlineLabels[2] = AddField(onlineLayout, netPass);
        onlineLabels[3] = AddField(onlineLayout, nick);
        AddWide(onlineLayout, testRelay);
        AddWide(onlineLayout, onlineInfo);
        testRelay.Click += TestRelay;
        online.CheckedChanged += delegate { UpdateOnlineEnabled(); };

        // O idioma é escolhido pelas bandeiras do cabeçalho; o ComboBox só guarda o estado.
        language.Items.AddRange(LanguageNames);
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
        for (int i = 0; i < CustomBindings; ++i)
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
        AddWide(controls, nativeHeader);
        for (int i = CustomBindings; i < bindingButtons.Length; ++i)
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
                KeyText(value);
            bindingButtons[i].Text = i == capturing ? TextInLanguage("Pressione uma tecla…", "Press a key…", "키를 누르세요…") : key;
        }
        bindingHint.Text = TextInLanguage("Clique em um atalho para editar. Backspace limpa e Esc cancela.",
            "Click a shortcut to edit. Backspace clears it and Esc cancels.", "단축키를 클릭해 편집하세요. Backspace로 지우고 Esc로 취소합니다.");
    }

    private static string KeyText(int value)
    {
        int key = value & 255;
        string prefix = ((value & 512) != 0 ? "Ctrl+" : "") + ((value & 1024) != 0 ? "Alt+" : "") + ((value & 256) != 0 ? "Shift+" : "");
        if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) return prefix + (char)key;
        if (key == 27) return prefix + "Esc";
        string name = new KeysConverter().ConvertToString((Keys)key);
        return prefix + name;
    }

    // Keys the launcher or the game window keep for themselves, whatever action asks for them.
    private static bool SystemKey(int binding)
    {
        const int alt = 1024, control = 512;
        if (binding == (alt | (int)Keys.Enter) || binding == (control | (int)Keys.Tab)) return true;
        int key = binding & 255;
        if (key == (int)Keys.F8 || key == (int)Keys.F9 || key == (int)Keys.F10 || key == (int)Keys.F12) return true;
        return Array.IndexOf(new[] {(int)Keys.Tab, (int)Keys.Enter, (int)Keys.ShiftKey, (int)Keys.ControlKey,
            (int)Keys.Menu, (int)Keys.Left, (int)Keys.Right, (int)Keys.Up, (int)Keys.Down, 'D', 'V'}, key) >= 0;
    }

    // A key is taken when another action already has it, so the game's own keys (A, C, 1…) are
    // free to move as soon as their action is moved; only system keys are always off limits.
    private static bool ReservedBinding(int binding)
    {
        return SystemKey(binding);
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
        if (ReservedBinding(binding)
            || (binding != 0 && Array.Exists(bindings, value => value == binding)
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

    private static Label AddField(TableLayoutPanel layout, Control field)
    {
        int row = layout.RowCount++;
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        var caption = new Label { AutoSize = true, Anchor = AnchorStyles.Left };
        layout.Controls.Add(caption, 0, row);
        field.Dock = DockStyle.Fill;
        layout.Controls.Add(field, 1, row);
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
        // Primeira execução: usa o idioma do Windows (pt → Português, ko → 한국어, resto → English).
        CultureInfo culture = CultureInfo.CurrentUICulture;
        string system = culture.TwoLetterISOLanguageName;
        bool traditional = system == "zh" && (culture.Name == "zh-TW" || culture.Name == "zh-HK" ||
            culture.Name == "zh-MO" || culture.Name.IndexOf("Hant", StringComparison.OrdinalIgnoreCase) >= 0);
        language.SelectedIndex = ReadIndex("language", language.Items.Count, system == "pt" ? 0 : system == "ko" ? 2 : traditional ? 3 : 1);
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
        LoadRelays();
        online.Checked = Native.ReadIni(LauncherIni, "online", "enabled", "0") == "1";
        relay.Text = Native.ReadIni(LauncherIni, "online", "relay", "");
        netName.Text = Native.ReadIni(LauncherIni, "online", "network", "");
        netPass.Text = Native.ReadIni(LauncherIni, "online", "password", "");
        nick.Text = Native.ReadIni(LauncherIni, "online", "nick", "");
        UpdateOnlineEnabled();

    }

    private static string RelayLabel(string[] entry)
    {
        return entry[0] + (entry[1].Length > 0 ? " · " + entry[1] : "") + " · " + entry[2];
    }

    // relays.txt: "nome regiao host:porta" por linha, # comenta. Fica ao lado do Jogar.exe.
    private void LoadRelays()
    {
        relays.Clear();
        relay.Items.Clear();
        string path = Path.Combine(folder, "relays.txt");
        if (!File.Exists(path)) return;
        foreach (string raw in File.ReadAllLines(path))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line[0] == '#') continue;
            string[] part = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            if (part.Length < 2) continue;
            var entry = part.Length == 2 ? new[] { part[0], "", part[1] } : new[] { part[0], part[1], part[2] };
            relays.Add(entry);
            relay.Items.Add(RelayLabel(entry));
        }
    }

    // O que foi digitado ou escolhido na lista vira host:porta.
    private string RelayAddress()
    {
        string text = relay.Text.Trim();
        foreach (string[] entry in relays)
            if (String.Equals(text, RelayLabel(entry), StringComparison.OrdinalIgnoreCase) ||
                String.Equals(text, entry[0], StringComparison.OrdinalIgnoreCase)) return entry[2];
        return text;
    }

    private static bool ParseAddress(string address, out string host, out int port)
    {
        host = address;
        port = 47700;
        int colon = address.LastIndexOf(':');
        if (colon >= 0)
        {
            host = address.Substring(0, colon);
            if (!Int32.TryParse(address.Substring(colon + 1), out port) || port < 1 || port > 65535) return false;
        }
        return host.Length > 0 && host.IndexOf(' ') < 0;
    }

    private void UpdateOnlineEnabled()
    {
        relay.Enabled = netName.Enabled = netPass.Enabled = nick.Enabled = testRelay.Enabled = online.Checked;
    }

    private void TestRelay(object sender, EventArgs args)
    {
        string host;
        int port;
        if (!ParseAddress(RelayAddress(), out host, out port))
        {
            onlineInfo.Text = TextInLanguage("Endereço inválido. Use nome ou IP, e opcionalmente :porta.",
                "Invalid address. Use a name or IP, optionally with :port.", "잘못된 주소입니다. 이름 또는 IP[:포트]를 입력하세요.");
            return;
        }
        onlineInfo.Text = TextInLanguage("Testando…", "Testing…", "테스트 중…");
        onlineInfo.Refresh();
        var watch = Stopwatch.StartNew();
        try
        {
            using (var client = new TcpClient())
            {
                IAsyncResult result = client.BeginConnect(host, port, null, null);
                if (!result.AsyncWaitHandle.WaitOne(4000)) throw new TimeoutException();
                client.EndConnect(result);
                onlineInfo.Text = String.Format(TextInLanguage("Relay respondeu em {0} ms.", "Relay answered in {0} ms.", "릴레이 응답 {0} ms."), watch.ElapsedMilliseconds);
            }
        }
        catch (Exception)
        {
            onlineInfo.Text = TextInLanguage("Não consegui conectar ao relay (endereço, porta ou firewall).",
                "Could not reach the relay (address, port or firewall).", "릴레이에 연결할 수 없습니다 (주소, 포트, 방화벽).");
        }
    }

    private int ReadIndex(string key, int count, int fallback = 0)
    {
        int value;
        return Int32.TryParse(Native.ReadIni(LauncherIni, "video", key, fallback.ToString(CultureInfo.InvariantCulture)), out value)
            && value >= 0 && value < count ? value : fallback;
    }

    private static readonly string[] LanguageCodes = { "pt", "en", "ko", "zh" };

    // O chinês é opcional: onde não há tradução, mostra o inglês.
    private string TextInLanguage(string portuguese, string english, string korean, string chinese = null)
    {
        if (language.SelectedIndex == 3) return chinese ?? english;
        if (language.SelectedIndex == 2) return korean;
        if (language.SelectedIndex == 1) return english;
        return portuguese;
    }

    private void ApplyLanguage()
    {
        for (int i = 0; i < flags.Length; ++i)
        {
            flags[i].Selected = i == language.SelectedIndex;
            flagTips.SetToolTip(flags[i], LanguageNames[i]);
        }
        fieldLabels[1].Text = TextInLanguage("Modo", "Display mode", "화면 모드", "顯示模式");
        fieldLabels[2].Text = TextInLanguage("Resolução da janela", "Window resolution", "창 해상도", "視窗解析度");
        fieldLabels[3].Text = TextInLanguage("Filtro gráfico", "Graphics filter", "그래픽 필터", "圖形濾鏡");
        fieldLabels[4].Text = TextInLanguage("Limite de FPS", "FPS limit", "FPS 제한", "FPS 上限");
        videoTab.Text = TextInLanguage("Vídeo", "Video", "화면", "畫面");
        controlsTab.Text = TextInLanguage("Controles", "Controls", "조작", "操作");
        originalTab.Text = TextInLanguage("Referência", "Reference", "참고", "參考");
        onlineTab.Text = TextInLanguage("Online", "Online", "온라인", "線上");
        online.Text = TextInLanguage("Jogar online por um relay (Multi Play pela internet)",
            "Play online through a relay (Multi Play over the internet)", "릴레이로 온라인 플레이 (인터넷 멀티플레이)");
        onlineLabels[0].Text = TextInLanguage("Relay (nome ou IP:porta)", "Relay (name or IP:port)", "릴레이 (이름 또는 IP:포트)");
        onlineLabels[1].Text = TextInLanguage("Rede (nome)", "Network (name)", "네트워크 이름");
        onlineLabels[2].Text = TextInLanguage("Senha da rede", "Network password", "네트워크 비밀번호");
        onlineLabels[3].Text = TextInLanguage("Apelido", "Nickname", "닉네임");
        testRelay.Text = TextInLanguage("Testar relay", "Test relay", "릴레이 테스트");
        if (onlineInfo.Text.Length == 0)
            onlineInfo.Text = TextInLanguage(
                "Todos que usarem o mesmo relay, o mesmo nome de rede e a mesma senha entram na mesma LAN virtual. Depois é só usar Multi Play, TCP/IP, deixar o IP vazio e procurar. Quem cria o nome da rede define a senha. A senha ainda trafega sem criptografia.",
                "Everyone using the same relay, network name and password joins the same virtual LAN. Then use Multi Play, TCP/IP, leave the IP empty and search. Whoever creates the network sets its password. The password is not encrypted yet.",
                "같은 릴레이, 네트워크 이름, 비밀번호를 쓰면 같은 가상 LAN에 접속합니다. 멀티플레이, TCP/IP에서 IP를 비우고 검색하세요. 비밀번호는 아직 암호화되지 않습니다.");
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
        nativeHeader.Text = TextInLanguage("Atalhos nativos do Akuma", "Akuma's own shortcuts", "아쿠마 기본 단축키", "Akuma 原生快捷鍵");
        for (int i = 0; i < NativeKeys.Length; ++i)
        {
            string[] names = NativeNames[i];
            bindingLabels[CustomBindings + i].Text = TextInLanguage(names[0], names[1], names[2], names[3]);
        }
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
        play.Text = TextInLanguage("Jogar", "Play", "게임 시작", "開始遊戲");

        int modeSelection = mode.SelectedIndex;
        int resolutionSelection = resolution.SelectedIndex;
        int filterSelection = filter.SelectedIndex;
        int limitSelection = limit.SelectedIndex;
        mode.Items[0] = TextInLanguage("Janela", "Windowed", "창 모드", "視窗");
        mode.Items[1] = TextInLanguage("Tela cheia sem bordas", "Borderless fullscreen", "테두리 없는 전체 화면", "無邊框全螢幕");
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
        Native.WriteIni(LauncherIni, "online", "enabled", online.Checked ? "1" : "0");
        Native.WriteIni(LauncherIni, "online", "relay", relay.Text.Trim());
        Native.WriteIni(LauncherIni, "online", "network", netName.Text.Trim());
        Native.WriteIni(LauncherIni, "online", "password", netPass.Text);
        Native.WriteIni(LauncherIni, "online", "nick", nick.Text.Trim());
    }

    private string JoinBindings(int start, int count)
    {
        var parts = new string[count];
        for (int i = 0; i < count; ++i) parts[i] = bindings[start + i].ToString(CultureInfo.InvariantCulture);
        return String.Join(",", parts);
    }

    private void StartGame(object sender, EventArgs args)
    {
        try
        {
            string relayAddress = RelayAddress(), relayHost;
            int relayPort;
            string network = netName.Text.Trim().Length > 0 ? netName.Text.Trim() : "akuma";
            if (online.Checked)
            {
                bool nameOk = System.Text.RegularExpressions.Regex.IsMatch(network, "^[A-Za-z0-9_. -]{1,32}$");
                if (!ParseAddress(relayAddress, out relayHost, out relayPort) || !nameOk)
                {
                    tabs.SelectedTab = onlineTab;
                    onlineInfo.Text = TextInLanguage(
                        "Confira o relay (nome ou IP:porta) e o nome da rede (letras, números, espaço, ponto, hífen e sublinhado).",
                        "Check the relay (name or IP:port) and the network name (letters, digits, space, dot, hyphen, underscore).",
                        "릴레이와 네트워크 이름을 확인하세요.");
                    return;
                }
            }
            SaveSettings();
            var start = new ProcessStartInfo(Path.Combine(folder, "AkumaHD.exe"))
            {
                WorkingDirectory = folder,
                UseShellExecute = false,
                Arguments = (intro.Checked ? "-show-intro " : "-skip-intro ") + LanguageCodes[language.SelectedIndex],
            };
            // Keep each extracted copy bound to its own wrapper configuration.
            start.EnvironmentVariables["CNC_DDRAW_CONFIG_FILE"] = WrapperIni;
            start.EnvironmentVariables["AKUMA_WIDE_HUD"] = wideHud.Checked ? "1" : "0";
            start.EnvironmentVariables["AKUMA_WASD_CAMERA"] = wasdCamera.Checked ? "1" : "0";
            start.EnvironmentVariables["AKUMA_BINDINGS"] = JoinBindings(0, CustomBindings);
            // Key the player chose for each of the game's own actions (same order as NativeKeys).
            start.EnvironmentVariables["AKUMA_NATIVE_KEYS"] = JoinBindings(CustomBindings, NativeKeys.Length);
            start.EnvironmentVariables["AKUMA_LANGUAGE"] = LanguageCodes[language.SelectedIndex];
            start.EnvironmentVariables["AKUMA_EXCLUSIVE_PARTY"] = exclusiveParty.Checked ? "1" : "0";
            start.EnvironmentVariables["AKUMA_CLICK_MARKER"] = clickMarker.Checked ? "1" : "0";
            start.EnvironmentVariables["AKUMA_SMOOTH_CAMERA"] = smoothCamera.Checked ? "1" : "0";
            start.EnvironmentVariables["AKUMA_PIXEL_ZOOM"] = filter.SelectedIndex == 1 ? "1" : "0";
            if (online.Checked)
            {
                start.EnvironmentVariables["AKUMA_RELAY"] = relayAddress;
                start.EnvironmentVariables["AKUMA_NET"] = network;
                start.EnvironmentVariables["AKUMA_NETPASS"] = netPass.Text;
                start.EnvironmentVariables["AKUMA_NICK"] = nick.Text.Trim().Length > 0 ? nick.Text.Trim() : "jogador";
            }
            else
            {
                start.EnvironmentVariables["AKUMA_RELAY_OFF"] = "1";   // ignora rede.txt
            }
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

// Bandeira clicável desenhada em GDI+ (emoji de bandeira não renderiza no Windows).
// Kind: 0 = Brasil, 1 = Reino Unido, 2 = Coreia do Sul, 3 = Taiwan. Um clique aciona Click.
internal sealed class FlagButton : Control
{
    public readonly int Kind;
    private bool selected, hover;

    public FlagButton(int kind)
    {
        Kind = kind;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
            ControlStyles.ResizeRedraw | ControlStyles.Selectable | ControlStyles.StandardClick, true);
        TabStop = true;
        Cursor = Cursors.Hand;
        Size = new Size(46, 34);
        Margin = new Padding(3, 0, 0, 0);
    }

    public bool Selected
    {
        get { return selected; }
        set { if (selected != value) { selected = value; Invalidate(); } }
    }

    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
    protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Space || e.KeyCode == Keys.Enter) { OnClick(EventArgs.Empty); e.Handled = true; }
        base.OnKeyDown(e);
    }

    protected override bool IsInputKey(Keys key) { return key == Keys.Space || base.IsInputKey(key); }

    protected override void OnPaint(PaintEventArgs e)
    {
        Graphics g = e.Graphics;
        g.Clear(Parent != null ? Parent.BackColor : SystemColors.Control);
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        var r = new RectangleF(5, 5, Width - 10, Height - 10);
        var state = g.Save();
        g.SetClip(r);
        DrawFlag(g, r);
        g.Restore(state);
        // Bandeiras não selecionadas ficam esmaecidas até o mouse passar por cima.
        if (!selected && !hover)
            using (var veil = new SolidBrush(Color.FromArgb(120, SystemColors.Control))) g.FillRectangle(veil, r);
        using (var edge = new Pen(Color.FromArgb(90, 0, 0, 0))) g.DrawRectangle(edge, r.X, r.Y, r.Width, r.Height);
        if (selected)
            using (var ring = new Pen(SystemColors.Highlight, 3)) g.DrawRectangle(ring, 2, 2, Width - 5, Height - 5);
        else if (Focused)
            using (var ring = new Pen(SystemColors.ControlDark, 1) { DashStyle = System.Drawing.Drawing2D.DashStyle.Dot })
                g.DrawRectangle(ring, 2, 2, Width - 5, Height - 5);
    }

    private void DrawFlag(Graphics g, RectangleF r)
    {
        float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
        if (Kind == 0)
        {
            using (var green = new SolidBrush(Color.FromArgb(0, 156, 59))) g.FillRectangle(green, r);
            using (var yellow = new SolidBrush(Color.FromArgb(255, 223, 0)))
                g.FillPolygon(yellow, new[] {
                    new PointF(cx, r.Y + r.Height * 0.10f), new PointF(r.Right - r.Width * 0.07f, cy),
                    new PointF(cx, r.Bottom - r.Height * 0.10f), new PointF(r.X + r.Width * 0.07f, cy) });
            float d = r.Height * 0.52f;
            using (var blue = new SolidBrush(Color.FromArgb(0, 39, 118))) g.FillEllipse(blue, cx - d / 2, cy - d / 2, d, d);
            using (var band = new Pen(Color.White, Math.Max(1f, r.Height * 0.07f)))
                g.DrawArc(band, cx - d * 0.42f, cy - d * 0.42f, d * 0.84f, d * 0.84f, 200, 110);
        }
        else if (Kind == 1)
        {
            using (var blue = new SolidBrush(Color.FromArgb(1, 33, 105))) g.FillRectangle(blue, r);
            using (var white = new Pen(Color.White, r.Height * 0.22f))
            {
                g.DrawLine(white, r.X, r.Y, r.Right, r.Bottom);
                g.DrawLine(white, r.X, r.Bottom, r.Right, r.Y);
            }
            using (var red = new Pen(Color.FromArgb(200, 16, 46), r.Height * 0.08f))
            {
                g.DrawLine(red, r.X, r.Y, r.Right, r.Bottom);
                g.DrawLine(red, r.X, r.Bottom, r.Right, r.Y);
            }
            using (var white = new SolidBrush(Color.White))
            {
                g.FillRectangle(white, r.X, cy - r.Height * 0.19f, r.Width, r.Height * 0.38f);
                g.FillRectangle(white, cx - r.Width * 0.11f, r.Y, r.Width * 0.22f, r.Height);
            }
            using (var red = new SolidBrush(Color.FromArgb(200, 16, 46)))
            {
                g.FillRectangle(red, r.X, cy - r.Height * 0.11f, r.Width, r.Height * 0.22f);
                g.FillRectangle(red, cx - r.Width * 0.06f, r.Y, r.Width * 0.12f, r.Height);
            }
        }
        else if (Kind == 3)
        {
            using (var red = new SolidBrush(Color.FromArgb(254, 0, 0))) g.FillRectangle(red, r);
            var canton = new RectangleF(r.X, r.Y, r.Width / 2, r.Height / 2);
            using (var blue = new SolidBrush(Color.FromArgb(0, 0, 149))) g.FillRectangle(blue, canton);
            float sx = canton.X + canton.Width / 2, sy = canton.Y + canton.Height / 2, ray = canton.Height * 0.46f;
            using (var white = new SolidBrush(Color.White))
            using (var rays = new Pen(Color.White, Math.Max(1f, canton.Height * 0.12f)))
            {
                for (int i = 0; i < 12; ++i)
                {
                    double a = i * Math.PI / 6;
                    g.DrawLine(rays, sx, sy, sx + (float)Math.Cos(a) * ray, sy + (float)Math.Sin(a) * ray);
                }
                using (var blueRing = new SolidBrush(Color.FromArgb(0, 0, 149)))
                {
                    float b = canton.Height * 0.36f;
                    g.FillEllipse(blueRing, sx - b / 2, sy - b / 2, b, b);
                }
                float c = canton.Height * 0.27f;
                g.FillEllipse(white, sx - c / 2, sy - c / 2, c, c);
            }
        }
        else
        {
            using (var white = new SolidBrush(Color.White)) g.FillRectangle(white, r);
            float d = r.Height * 0.52f, rad = d / 2;
            var state = g.Save();
            g.TranslateTransform(cx, cy);
            g.RotateTransform(-33.7f);
            using (var red = new SolidBrush(Color.FromArgb(205, 46, 58)))
            using (var blue = new SolidBrush(Color.FromArgb(0, 71, 160)))
            {
                g.FillPie(red, -rad, -rad, d, d, 180, 180);
                g.FillPie(blue, -rad, -rad, d, d, 0, 180);
                g.FillEllipse(red, -rad, -rad / 2, rad, rad);
                g.FillEllipse(blue, 0, -rad / 2, rad, rad);
            }
            g.Restore(state);
            // Quatro trigramas (Geon, Gam, Gon, Ri): barras inteiras ou partidas nos cantos.
            string[] pattern = { "SSS", "PSP", "SPS", "PPP" };
            for (int corner = 0; corner < 4; ++corner)
            {
                float sx = (corner & 1) == 0 ? -1 : 1, sy = corner < 2 ? -1 : 1;
                var s2 = g.Save();
                g.TranslateTransform(cx + sx * r.Width * 0.33f, cy + sy * r.Height * 0.30f);
                g.RotateTransform((float)(Math.Atan2(sy * r.Height, sx * r.Width) * 180 / Math.PI) + 90);
                float len = r.Height * 0.46f, thick = Math.Max(1f, r.Height * 0.075f), gap = thick * 1.0f;
                using (var black = new SolidBrush(Color.Black))
                    for (int bar = 0; bar < 3; ++bar)
                    {
                        float y = (bar - 1) * (thick + gap) - thick / 2;
                        if (pattern[corner][bar] == 'S')
                            g.FillRectangle(black, -len / 2, y, len, thick);
                        else
                        {
                            g.FillRectangle(black, -len / 2, y, len * 0.42f, thick);
                            g.FillRectangle(black, len * 0.08f, y, len * 0.42f, thick);
                        }
                    }
                g.Restore(s2);
            }
        }
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
        var value = new StringBuilder(260);
        GetPrivateProfileString(section, key, fallback, value, value.Capacity, file);
        return value.ToString();
    }

    internal static void WriteIni(string file, string section, string key, object value)
    {
        if (!WritePrivateProfileString(section, key, Convert.ToString(value, CultureInfo.InvariantCulture), file))
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }
}



