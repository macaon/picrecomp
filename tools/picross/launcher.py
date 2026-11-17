"""Desktop setup for a locally built PicRecomp."""

import os
from pathlib import Path
from queue import Empty, Queue
import shutil
import subprocess
import sys
from threading import Thread
import tkinter as tk
from tkinter import filedialog, ttk

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "picross"
GAME = ROOT / ".picrecomp" / "prepared-game"
BUILD = ROOT / "build"


class Setup:
    def __init__(self, window):
        self.window = window
        self.events = Queue()
        self.busy = False
        self.entries = []
        self.files = [tk.StringVar() for _ in range(3)]
        self.status = tk.StringVar()
        self.page = "home"
        self.theme(window)
        window.title("PicRecomp")
        window.geometry("1040x650")
        window.minsize(720, 545)
        window.columnconfigure(0, weight=1)
        window.rowconfigure(1, weight=1)
        window.protocol("WM_DELETE_WINDOW", self.close)
        window.bind("<Escape>", lambda _: self.show("home"))

        header = ttk.Frame(window, padding=(24, 14))
        header.grid(sticky="ew")
        ttk.Label(header, text="▦  PicRecomp", style="Brand.TLabel").pack(side="left")
        ttk.Label(header, text="PICROSS S", style="Muted.TLabel").pack(side="right")
        self.body = ttk.Frame(window)
        self.body.grid(row=1, sticky="nsew")
        self.body.rowconfigure(0, weight=1)
        self.body.columnconfigure(0, weight=1)
        self.pages = {}
        for name in ("home", "files", "settings"):
            page = ttk.Frame(self.body)
            page.grid(row=0, column=0, sticky="nsew")
            self.pages[name] = page
        self.home_page()
        self.files_page()
        self.settings_page()

        footer = ttk.Frame(window, padding=(24, 12))
        footer.grid(row=2, sticky="ew")
        ttk.Label(footer, textvariable=self.status, style="Muted.TLabel", wraplength=660).pack(side="left")
        self.progress = ttk.Progressbar(footer, mode="indeterminate", length=125)
        self.progress.pack(side="right")
        self.progress.pack_forget()
        self.show("home")
        self.refresh(initial=True)
        window.after(80, self.poll)

    @staticmethod
    def theme(window):
        window.configure(background="#141923")
        style = ttk.Style(window)
        style.theme_use("clam")
        style.configure(".", background="#141923", foreground="#edf3fb", font=("Inter", 11))
        style.configure("Muted.TLabel", foreground="#aebbd0")
        style.configure("Brand.TLabel", font=("Inter", 14, "bold"))
        style.configure("Title.TLabel", font=("Inter", 34, "bold"))
        style.configure("Page.TLabel", font=("Inter", 23, "bold"))
        style.configure("TButton", background="#252e3e", bordercolor="#38465a", padding=(14, 9))
        style.configure("Accent.TButton", background="#87ebc9", foreground="#102823", font=("Inter", 13, "bold"))
        style.map("TButton", background=[("active", "#334259")], foreground=[("disabled", "#8f9bad")])
        style.map("Accent.TButton", background=[("disabled", "#263c3b"), ("active", "#a7f5dc")],
                  foreground=[("disabled", "#8faaa5"), ("!disabled", "#102823")])
        style.configure("TEntry", fieldbackground="#212a39", insertcolor="#edf3fb", bordercolor="#39475b")
        style.map("TEntry", fieldbackground=[("disabled", "#1b2230")])
        style.configure("TCombobox", fieldbackground="#212a39", arrowcolor="#edf3fb")
        style.map("TCombobox", fieldbackground=[("readonly", "#212a39")],
                  selectbackground=[("readonly", "#212a39")], selectforeground=[("readonly", "#edf3fb")])
        style.configure("Horizontal.TProgressbar", background="#87ebc9", troughcolor="#263347")

    def show(self, name):
        self.page = name
        self.pages[name].tkraise()

    def home_page(self):
        page = self.pages["home"]
        page.rowconfigure(0, weight=1)
        page.columnconfigure(1, weight=1)
        menu = ttk.Frame(page, padding=(48, 40, 32, 38))
        menu.grid(row=0, column=0, sticky="nsw")
        ttk.Label(menu, text="PICROSS S", style="Title.TLabel").pack(anchor="w", pady=(30, 4))
        ttk.Label(menu, text="Nintendo Switch recompilation", style="Muted.TLabel").pack(anchor="w", pady=(0, 25))
        self.play_button = ttk.Button(menu, text="▶  Play", style="Accent.TButton", command=self.play)
        self.play_button.pack(fill="x", pady=(0, 14))
        for label, command in (
            ("Game Files", lambda: self.show("files")),
            ("Settings", lambda: self.show("settings")),
            ("Quit", self.close),
        ):
            ttk.Button(menu, text=label, command=command).pack(fill="x", pady=4)

        art = tk.Canvas(page, background="#141923", highlightthickness=0, width=1)
        art.grid(row=0, column=1, sticky="nsew")

        def draw(event):
            art.delete("all")
            size = max(29, min(64, (event.width - 54) // 7, event.height // 8))
            left = (event.width - size * 7) / 2
            top = (event.height - size * 7) / 2
            pattern = ("..###..", ".#...#.", ".#...#.", "..###..", "...#...", "...###.", "...#...")
            for row, line in enumerate(pattern):
                for column, cell in enumerate(line):
                    x, y = left + column * size, top + row * size
                    fill = "#67caaa" if cell == "#" else "#202c3c"
                    art.create_rectangle(x, y, x + size - 4, y + size - 4, fill=fill, outline="")
        art.bind("<Configure>", draw)

    def section(self, name, heading, subtitle):
        panel = ttk.Frame(self.pages[name], padding=(48, 28))
        panel.pack(fill="both", expand=True)
        panel.columnconfigure(0, weight=1)
        ttk.Label(panel, text=heading, style="Page.TLabel").grid(sticky="w")
        ttk.Label(panel, text=subtitle, style="Muted.TLabel").grid(sticky="w", pady=(5, 22))
        return panel

    def files_page(self):
        panel = self.section("files", "Game Files", "Link your PICROSS S base game and update v524288.")
        fields = ttk.Frame(panel)
        fields.grid(sticky="ew")
        fields.columnconfigure(1, weight=1)
        for row, (label, value, filetypes) in enumerate((
            ("Base NSP", self.files[0], [("Switch package", "*.nsp")]),
            ("Update NSP", self.files[1], [("Switch package", "*.nsp")]),
            ("Production keys", self.files[2], [("Key file", "*.keys")]),
        )):
            ttk.Label(fields, text=label).grid(row=row, column=0, sticky="w", padx=(0, 20), pady=8)
            entry = ttk.Entry(fields, textvariable=value)
            entry.grid(row=row, column=1, sticky="ew", ipady=5)
            button = ttk.Button(fields, text="Browse…", command=lambda v=value, name=label, types=filetypes:
                                self.browse(v, name, types))
            button.grid(row=row, column=2, padx=(12, 0))
            self.entries.extend((entry, button))
            value.trace_add("write", lambda *_: self.refresh())
        actions = ttk.Frame(panel)
        actions.grid(sticky="e", pady=(24, 0))
        ttk.Button(actions, text="Back", command=lambda: self.show("home")).pack(side="left", padx=(0, 8))
        self.prepare_button = ttk.Button(actions, text="Prepare game", style="Accent.TButton", command=self.prepare)
        self.prepare_button.pack(side="left")

    @staticmethod
    def settings_path():
        data = Path(os.environ.get("EOWRECOMP_DATA_ROOT") or ROOT / ".picrecomp" / "runtime")
        return data / (os.environ.get("EOWRECOMP_SETTINGS") or "eowrecomp.ini")

    def settings_page(self):
        panel = self.section("settings", "Settings", "These settings apply the next time you launch the game.")
        tabs = ttk.Notebook(panel)
        tabs.grid(sticky="ew")
        self.options = {}
        for name, settings in (
            ("Screen", (("fullscreen", "Window mode", ("Windowed", "Fullscreen"), ("0", "1")),
                        ("show_fps", "Performance overlay", ("Off", "On"), ("0", "1")))),
            ("Graphics", (("resolution", "Internal resolution", ("Match window", "Dynamic", "75%", "50%", "25%", "1×", "2×", "3×", "4×"),
                           ("match", "dynamic", "75%", "50%", "25%", "1x", "2x", "3x", "4x")),
                          ("upscaler", "Upscaling filter", ("FSR", "Bilinear"), ("fsr", "bilinear")),
                          ("antialias", "Anti-aliasing", ("Off", "SMAA"), ("off", "smaa")))),
            ("Sound", (("volume", "Master volume", tuple(f"{n}%" for n in range(0, 201, 5)),
                        tuple(f"{n / 100:.2f}" for n in range(0, 201, 5))),)),
        ):
            tab = ttk.Frame(tabs, padding=18)
            tabs.add(tab, text=name)
            tab.columnconfigure(0, weight=1)
            for row, (key, label, choices, values) in enumerate(settings):
                ttk.Label(tab, text=label).grid(row=row, column=0, sticky="w", pady=10)
                var = tk.StringVar()
                field = ttk.Combobox(tab, textvariable=var, values=choices, state="readonly", width=21)
                field.grid(row=row, column=1, sticky="e")
                self.options[key] = (var, dict(zip(choices, values)))
        actions = ttk.Frame(panel)
        actions.grid(sticky="e", pady=(22, 0))
        ttk.Button(actions, text="Back", command=lambda: self.show("home")).pack(side="left", padx=(0, 8))
        self.save_button = ttk.Button(actions, text="Save settings", style="Accent.TButton", command=self.save_settings)
        self.save_button.pack(side="left")
        self.load_settings()

    def load_settings(self):
        path = self.settings_path()
        defaults = {"fullscreen": "0", "show_fps": "0", "resolution": "match", "upscaler": "fsr",
                    "antialias": "off", "volume": "1.00"}
        if path.is_file():
            for line in path.read_text().splitlines():
                key, sep, value = line.partition("=")
                if sep and key.strip() in defaults:
                    defaults[key.strip()] = value.strip()
        for key, (variable, choices) in self.options.items():
            value = defaults[key]
            if key == "volume":
                try:
                    value = f"{max(0, min(40, round(float(value) * 20))) / 20:.2f}"
                except (ValueError, OverflowError):
                    value = "1.00"
            variable.set(next((label for label, wire in choices.items() if value == wire), next(iter(choices))))

    def save_settings(self):
        path = self.settings_path()
        updates = {key: choices[variable.get()] for key, (variable, choices) in self.options.items()}
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            lines = path.read_text().splitlines() if path.is_file() else []
            lines = [line for line in lines if line.partition("=")[0].strip() not in updates]
            lines.extend(f"{key} = {value}" for key, value in updates.items())
            temporary = path.with_suffix(".ini.tmp")
            temporary.write_text("\n".join(lines) + "\n")
            temporary.replace(path)
        except OSError as error:
            self.status.set(str(error))
        else:
            self.status.set("Settings saved.")

    def browse(self, variable, label, filetypes):
        title = f"Select {label}"
        filename = self.pick_file(title, filetypes)
        if filename:
            variable.set(filename)

    def pick_file(self, title, filetypes):
        # Let the desktop toolkit apply its own colors and file chooser settings.
        zenity = shutil.which("zenity") if sys.platform.startswith("linux") else None
        if zenity:
            command = [zenity, "--file-selection", "--modal", f"--title={title}"]
            command.extend(f"--file-filter={name} | {pattern}" for name, pattern in filetypes)
            try:
                result = subprocess.run(command, capture_output=True, text=True, check=False)
            except OSError:
                pass
            else:
                if result.returncode == 0:
                    return result.stdout.rstrip("\n")
                if result.returncode == 1:
                    return ""

        # Tk's file grid uses its stock canvas background. Use a stock theme
        # during the modal dialog so its text and canvas colors agree.
        style = ttk.Style(self.window)
        launcher_theme = style.theme_use()
        try:
            style.theme_use("default")
            return filedialog.askopenfilename(parent=self.window, title=title, filetypes=filetypes)
        finally:
            style.theme_use(launcher_theme)

    @staticmethod
    def ready():
        return all((GAME / name).is_file() for name in (
            "exefs/main", "exefs/main.npdm", "exefs/subsdk0", "exefs/sdk", "romfs.bin",
        ))

    @staticmethod
    def built():
        return os.access(BUILD / "eowrecomp", os.X_OK) and all(
            (BUILD / name).is_file() for name in ("eowgame.so", "eowgame.so.id")
        )

    def refresh(self, initial=False):
        ready = self.ready()
        fields_enabled = not self.busy and not GAME.exists()
        for widget in self.entries:
            widget.configure(state="normal" if fields_enabled else "disabled")
        can_prepare = fields_enabled and all(item.get().strip() for item in self.files)
        self.save_button.configure(state="disabled" if self.busy else "normal")
        self.prepare_button.configure(state="normal" if can_prepare else "disabled")
        self.play_button.configure(state="normal" if ready and self.built() and not self.busy else "disabled")
        if initial:
            if ready:
                self.status.set("Ready to play." if self.built() else "Game data is ready. Build PicRecomp using README.md.")
            elif GAME.exists():
                self.status.set("The prepared game directory is incomplete. Move it aside and reopen setup.")
            else:
                self.status.set("Select your two NSPs and prod.keys to prepare the game.")

    def prepare(self):
        base, update, keys = (value.get().strip() for value in self.files)
        command = [
            sys.executable, "-u", str(TOOLS / "prepare.py"), base, update, str(GAME),
            "--prod-keys", keys,
        ]
        self.start(command, "Preparing game data…", preparation=True)

    def play(self):
        self.start([str(TOOLS / "play.sh"), str(GAME)], "PICROSS S is running.")

    def start(self, command, status, preparation=False):
        self.busy = True
        self.status.set(status)
        self.refresh()
        self.progress.pack(side="right")
        self.progress.start(12)
        Thread(target=self.run, args=(command, preparation), daemon=True).start()

    def run(self, command, preparation):
        try:
            GAME.parent.mkdir(parents=True, exist_ok=True)
            if preparation:
                with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as process:
                    for line in process.stdout:
                        self.events.put(("progress", line.strip()))
                    code = process.wait()
                self.events.put(("prepared" if code == 0 else "failed", code))
            else:
                log_path = GAME.parent / "play.log"
                with log_path.open("w") as log:
                    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
                self.events.put(("played", result.returncode))
        except OSError as error:
            self.events.put(("error", str(error)))

    def poll(self):
        try:
            while True:
                event, detail = self.events.get_nowait()
                if event == "progress":
                    self.status.set(detail)
                    continue
                self.busy = False
                self.progress.stop()
                self.progress.pack_forget()
                self.refresh(initial=event == "prepared")
                if event == "played":
                    self.load_settings()
                    self.status.set("Ready to play." if detail == 0 else
                                    f"Game exited with status {detail}. See .picrecomp/play.log or the runtime log.")
                elif event == "error":
                    self.status.set(detail)
        except Empty:
            pass
        self.window.after(80, self.poll)

    def close(self):
        if not self.busy:
            self.window.destroy()


def main():
    window = tk.Tk()
    Setup(window)
    window.mainloop()


if __name__ == "__main__":
    main()
