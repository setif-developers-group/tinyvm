using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace VmDashboard;

/// <summary>Dashboard: registers, flags, memory, execution trace and program output.</summary>
public sealed class MainForm : Form
{
    private readonly VirtualMachine _vm = new();
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 30 };
    private readonly DataGridView _regGrid = new();
    private readonly Label _summary = new();
    private readonly TextBox _memBox = new();
    private readonly TextBox _traceBox = new();
    private readonly TextBox _outBox = new();
    private readonly NumericUpDown _memAddr = new();
    private readonly NumericUpDown _speed = new();
    private readonly Button _openBtn = new(), _stepBtn = new(), _runBtn = new(), _resetBtn = new();
    private readonly List<string> _trace = new();
    private readonly uint[] _prev = new uint[VirtualMachine.RegisterCount];
    private byte[]? _image;

    public MainForm()
    {
        Text = "TinyVM Dashboard";
        Size = new Size(1100, 720);
        MinimumSize = new Size(900, 560);
        Font = new Font("Segoe UI", 9F);

        BuildToolbar();
        BuildBody();

        _timer.Tick += OnTick;
        FormClosed += (_, _) => { _timer.Stop(); _vm.Dispose(); };
        SetControlsEnabled(false);
        UpdateView();
    }

    // ---------------------------------------------------------------- layout
    private void BuildToolbar()
    {
        var bar = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 44, Padding = new Padding(8, 8, 8, 0) };

        _openBtn.Text = "Open .bin…";
        _stepBtn.Text = "Step";
        _runBtn.Text = "Run";
        _resetBtn.Text = "Reset";
        foreach (var b in new[] { _openBtn, _stepBtn, _runBtn, _resetBtn }) b.AutoSize = true;
        _openBtn.Click += (_, _) => OpenProgram();
        _stepBtn.Click += (_, _) => { StepOnce(); UpdateView(); };
        _runBtn.Click += (_, _) => ToggleRun();
        _resetBtn.Click += (_, _) => ResetProgram();

        _speed.Minimum = 1; _speed.Maximum = 5000; _speed.Value = 10; _speed.Width = 70;

        bar.Controls.AddRange(new Control[]
        {
            _openBtn, _stepBtn, _runBtn, _resetBtn,
            new Label { Text = "Steps / tick:", AutoSize = true, Padding = new Padding(12, 6, 0, 0) }, _speed
        });
        Controls.Add(bar);
    }

    private void BuildBody()
    {
        var mono = new Font("Consolas", 9.5F);

        _regGrid.Dock = DockStyle.Fill;
        _regGrid.ReadOnly = true;
        _regGrid.AllowUserToAddRows = false;
        _regGrid.RowHeadersVisible = false;
        _regGrid.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
        _regGrid.DefaultCellStyle.Font = mono;
        _regGrid.Columns.Add("reg", "Register");
        _regGrid.Columns.Add("hex", "Hex");
        _regGrid.Columns.Add("dec", "Signed");
        for (int i = 0; i < VirtualMachine.RegisterCount; i++)
            _regGrid.Rows.Add(RegisterName(i), "0x00000000", "0");

        _summary.Dock = DockStyle.Bottom;
        _summary.Height = 46;
        _summary.Font = mono;

        var regPanel = new Panel { Dock = DockStyle.Fill };
        regPanel.Controls.Add(_regGrid);
        regPanel.Controls.Add(_summary);

        ConfigureMono(_memBox, mono);
        ConfigureMono(_traceBox, mono);
        ConfigureMono(_outBox, mono);

        _memAddr.Hexadecimal = true;
        _memAddr.Minimum = 0;
        _memAddr.Maximum = VirtualMachine.MemorySize - 256;
        _memAddr.Increment = 256;
        _memAddr.ValueChanged += (_, _) => UpdateMemory();
        var memPanel = new Panel { Dock = DockStyle.Fill };
        memPanel.Controls.Add(_memBox);
        memPanel.Controls.Add(new FlowLayoutPanel
        {
            Dock = DockStyle.Top, Height = 30,
            Controls = { new Label { Text = "Start address (hex):", AutoSize = true, Padding = new Padding(0, 6, 0, 0) }, _memAddr }
        });

        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 2, Padding = new Padding(6) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 45));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 55));
        grid.RowStyles.Add(new RowStyle(SizeType.Percent, 55));
        grid.RowStyles.Add(new RowStyle(SizeType.Percent, 45));
        grid.Controls.Add(Group("Registers & flags", regPanel), 0, 0);
        grid.Controls.Add(Group("Memory", memPanel), 1, 0);
        grid.Controls.Add(Group("Execution trace", _traceBox), 0, 1);
        grid.Controls.Add(Group("Program output", _outBox), 1, 1);
        Controls.Add(grid);
        grid.BringToFront();
    }

    private static void ConfigureMono(TextBox box, Font font)
    {
        box.Multiline = true;
        box.ReadOnly = true;
        box.ScrollBars = ScrollBars.Vertical;
        box.Dock = DockStyle.Fill;
        box.Font = font;
        box.WordWrap = false;
    }

    private static GroupBox Group(string title, Control content)
    {
        var box = new GroupBox { Text = title, Dock = DockStyle.Fill };
        content.Dock = DockStyle.Fill;
        box.Controls.Add(content);
        return box;
    }

    // --------------------------------------------------------------- actions
    private void OpenProgram()
    {
        using var dialog = new OpenFileDialog { Filter = "TinyVM binary (*.bin)|*.bin|All files (*.*)|*.*" };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;

        var data = File.ReadAllBytes(dialog.FileName);
        if (data.Length > VirtualMachine.MemorySize)
        {
            MessageBox.Show(this, "Program is larger than the 64 KiB address space.", "Load failed");
            return;
        }
        _image = data;
        Text = $"TinyVM Dashboard - {Path.GetFileName(dialog.FileName)}";
        ResetProgram();
    }

    private void ResetProgram()
    {
        StopRunning();
        if (_image == null) return;
        _vm.Reset();
        _vm.Load(_image);
        _trace.Clear();
        _outBox.Clear();
        Array.Clear(_prev);
        SetControlsEnabled(true);
        UpdateView();
    }

    private void ToggleRun()
    {
        if (_timer.Enabled) { StopRunning(); return; }
        if (_vm.Status != VmStatus.Running) return;
        _timer.Start();
        _runBtn.Text = "Pause";
    }

    private void StopRunning()
    {
        _timer.Stop();
        _runBtn.Text = "Run";
    }

    private void OnTick(object? sender, EventArgs e)
    {
        int n = (int)_speed.Value;
        for (int i = 0; i < n && _vm.Status == VmStatus.Running; i++) StepOnce();
        if (_vm.Status != VmStatus.Running) StopRunning();
        UpdateView();
    }

    private void StepOnce()
    {
        if (_vm.Status != VmStatus.Running) return;
        uint pc = _vm.Pc;
        string text = _vm.DisassembleAt(pc);
        _vm.Step();
        _trace.Add($"{_vm.Cycles,7}  {pc:X4}: {text}");
        if (_trace.Count > 500) _trace.RemoveRange(0, _trace.Count - 500);
    }

    private void SetControlsEnabled(bool on)
    {
        _stepBtn.Enabled = on;
        _runBtn.Enabled = on;
        _resetBtn.Enabled = on;
    }

    // ------------------------------------------------------------------ view
    private static string RegisterName(int i) => i == 15 ? "sp" : $"r{i}";

    private void UpdateView()
    {
        for (int i = 0; i < VirtualMachine.RegisterCount; i++)
        {
            uint v = _vm.Register(i);
            var row = _regGrid.Rows[i];
            row.Cells[1].Value = $"0x{v:X8}";
            row.Cells[2].Value = unchecked((int)v).ToString();
            row.DefaultCellStyle.BackColor = v != _prev[i] ? Color.Khaki : SystemColors.Window;
            _prev[i] = v;
        }

        uint f = _vm.Flags;
        string flags = $"{((f & 1) != 0 ? 'Z' : '-')}{((f & 2) != 0 ? 'N' : '-')}{((f & 4) != 0 ? 'C' : '-')}{((f & 8) != 0 ? 'V' : '-')}";
        _summary.Text = $"PC 0x{_vm.Pc:X4}   Flags {flags}   Cycles {_vm.Cycles}\r\nStatus: {_vm.Status}";

        UpdateMemory();

        _traceBox.Lines = _trace.Skip(Math.Max(0, _trace.Count - 200)).ToArray();
        _traceBox.SelectionStart = _traceBox.TextLength;
        _traceBox.ScrollToCaret();

        string produced = _vm.TakeOutput();
        if (produced.Length > 0) _outBox.AppendText(produced.Replace("\n", "\r\n"));
    }

    private void UpdateMemory()
    {
        uint start = (uint)_memAddr.Value;
        var bytes = _vm.ReadMemory(start, 256);
        var sb = new StringBuilder();
        for (int row = 0; row < bytes.Length / 16; row++)
        {
            sb.Append($"{start + row * 16:X4}  ");
            var ascii = new StringBuilder();
            for (int col = 0; col < 16; col++)
            {
                byte b = bytes[row * 16 + col];
                sb.Append($"{b:X2} ");
                ascii.Append((b >= 32 && b < 127) ? (char)b : '.');
            }
            sb.Append(" |").Append(ascii).Append("|\r\n");
        }
        _memBox.Text = sb.ToString();
    }
}
