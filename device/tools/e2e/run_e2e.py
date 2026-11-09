#!/usr/bin/env python3
"""End-to-end tests on the Kindle (developer tool). Drives the running app with scripted raw pen/touch input (the
devinput hook: events go through the same queue and handlers as the real pen and touch screen), reads back the
tappable regions (devdump hook) and the app log, and answers AI turns with canned replies (fake_response.json)
unless --real-ai is given. Uses a separate 'e2e' notebook and restores the owner's config and state afterwards.
Needs a dev build on the Kindle (device/app/build.sh --dev): release builds have none of these hooks.

Usage: run_e2e.py [--real-ai] [--keep-going] [name-filter ...]
Screenshots of key screens go to dist/e2e/."""
import json, pathlib, re, subprocess, sys, time, traceback

ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = ROOT / 'dist' / 'e2e'
SECURE = '/var/local/scribe-claude'
APP = '/var/local/scribe/app'
LOG = f'{APP}/scribe.log'
USB = '/mnt/us/inkling'
OLD_USB = '/mnt/us/scribe-claude'  # its name up to 0.2.x (Scribe): the app moves it to USB when it starts
CTL = f'/tmp/scribe-e2e-ssh-{pathlib.Path.home().name}'
SSH = ['ssh', '-o', 'ControlMaster=auto', '-o', f'ControlPath={CTL}', '-o', 'ControlPersist=120', 'scribe-kindle']


class Fail(Exception):
    pass


def ssh(cmd, check=True, timeout=120, inp=None):
    r = subprocess.run(SSH + [cmd], capture_output=True, text=True, timeout=timeout, input=inp)
    if check and r.returncode != 0:
        raise Fail(f'ssh failed ({r.returncode}): {cmd}\n{r.stderr.strip()}')
    return r.stdout


class Dev:
    def __init__(self):
        self.mark = 0

    # --- log ---
    def log_size(self):
        return int(ssh(f'wc -c < {LOG}').strip() or 0)

    def mark_now(self):
        self.mark = self.log_size()

    def since(self):
        return ssh(f'tail -c +{self.mark + 1} {LOG}', check=False)

    def expect(self, pattern, timeout=10, what=None):
        t = time.time()
        while time.time() - t < timeout:
            m = re.search(pattern, self.since())
            if m:
                return m
            time.sleep(0.25)
        raise Fail(f'expected log /{pattern}/ within {timeout}s ({what or ""})\n--- log since mark:\n{self.since()[-1500:]}')

    def expect_not(self, pattern, wait=1.5):
        time.sleep(wait)
        m = re.search(pattern, self.since())
        if m:
            raise Fail(f'unexpected log /{pattern}/: {m.group(0)}')

    # --- input ---
    def inp(self, script):
        ssh(f'cat > {SECURE}/devinput.tmp && mv {SECURE}/devinput.tmp {SECURE}/devinput && kill -USR2 $(pidof scribe)',
            inp=script.strip() + '\n')

    def hits(self):
        ssh(f'rm -f {SECURE}/hits.txt; touch {SECURE}/devdump; kill -USR2 $(pidof scribe)')
        out = ''
        for _ in range(40):
            time.sleep(0.2)
            out = ssh(f'cat {SECURE}/hits.txt 2>/dev/null', check=False)
            if out.startswith('state ') and out.endswith('\n'):
                break
        state, bar, panel = {}, {}, {}
        for line in out.splitlines():
            if line.startswith('state '):
                state = dict(kv.split('=', 1) for kv in line[6:].split())
            elif line.count('\t') == 2:
                kind, label, box = line.split('\t')
                x, y, w, h = map(int, box.split())
                (bar if kind == 'bar' else panel)[label] = (x + w // 2, y + h // 2)
        if not state:
            raise Fail(f'no state from devdump: {out[:200]}')
        return state, bar, panel

    def tap(self, label, how='pen', panel_only=False):
        st, bar, panel = self.hits()
        if label in panel:
            x, y = panel[label]
        elif not panel_only and label in bar:
            x, y = bar[label]
        else:
            raise Fail(f'no tappable "{label}" on screen; state={st} bar={list(bar)} panel={list(panel)[:40]}')
        if y > 2470:
            raise Fail(f'"{label}" is off the bottom of the screen (y={y})')
        self.inp(f'{"pentap" if how == "pen" else "fingertap"} {x} {y}')
        return x, y

    def wait_idle(self, timeout=60):
        """Until no reply is being drawn and nothing is being sent."""
        t = time.time()
        while time.time() - t < timeout:
            st, _, _ = self.hits()
            if st.get('nops') == '0' and 'thinking' not in self.last_status():
                return st
            time.sleep(0.5)
        raise Fail('app never went idle')

    def last_status(self):
        lines = [l for l in ssh(f'tail -n 30 {LOG}', check=False).splitlines() if ' status: ' in l]
        return lines[-1] if lines else ''

    def shot(self, name):
        OUT.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(ROOT / 'device' / 'tools' / 'fbshot.sh'), str(OUT / f'{name}.png')], capture_output=True, check=False)

    def home(self):
        """Close whatever is open, back to the page."""
        for _ in range(4):
            st, bar, _ = self.hits()
            if st.get('modes', '0') != '0':  # a drop-down: a tap beside the card closes it
                self.inp('pentap 120 2400')
                time.sleep(1.5)
                continue
            if st['menu'] == st['settings'] == st['games'] == st['pagemenu'] == '0':
                return st
            self.mark_now()
            x, y = bar['Close']
            self.inp(f'pentap {x} {y}')
            time.sleep(1.5)
        raise Fail('could not get back to the page')

    # --- canned AI replies ---
    def fake(self, reply, mode='talk', style='script', read='test', memo='', illustrate=''):
        inner = {'read': read, 'mode': mode, 'style': style, 'reply': reply, 'memo': memo, 'illustrate': illustrate}
        body = {'content': [{'type': 'text', 'text': json.dumps(inner)}], 'stop_reason': 'end_turn'}
        ssh(f'cat > {SECURE}/fake_response.json', inp=json.dumps(body))

    def no_fake(self):
        ssh(f'rm -f {SECURE}/fake_response.json {SECURE}/fake_draw.json')

    def fake_draw(self, strokes):
        body = {'content': [{'type': 'text', 'text': json.dumps({'strokes': strokes})}], 'stop_reason': 'end_turn'}
        ssh(f'cat > {SECURE}/fake_draw.json', inp=json.dumps(body))

    def ink(self, x, y, w, h):
        """Dark pixels in a screen rectangle (reads the framebuffer)."""
        pan = ssh('cut -d, -f2 /sys/class/graphics/fb0/pan').strip() or '0'
        raw = subprocess.run(SSH + [f'dd if=/dev/fb0 bs=1872 skip={int(pan) + y} count={h} 2>/dev/null'],
                             capture_output=True, timeout=60).stdout
        return sum(1 for r in range(h) for b in raw[r * 1872 + x:r * 1872 + x + w] if b < 128)

    def config(self, key):
        m = re.search(rf'^{re.escape(key)}\s*=\s*(.*)$', ssh(f'cat {USB}/config.txt'), re.M)
        return m.group(1).strip() if m else None


BAR_Y = 66
MODE_STATUS = {'talk': 'Chat:', 'notes': 'Type up:', 'math': 'Math:', 'translate': 'Translate:'}


def menu(d, item):
    """Menu drop-down: newpage, settings or exit."""
    d.wait_idle()  # an answer still coming makes Menu say "wait for the answer to finish"
    d.tap('Menu'); time.sleep(1.5)
    if f'menu:{item}' not in d.hits()[2]:
        raise Fail(f'no menu:{item} in the Menu drop-down: {list(d.hits()[2])}')
    d.tap(f'menu:{item}', panel_only=True)


def send_now(d):
    d.wait_idle()
    d.tap('Send'); time.sleep(1.5)
    d.tap('send:now', panel_only=True)


def games(d):
    """Games, through the mode picker (also from inside a game: back to the list)."""
    d.tap('Mode'); time.sleep(1.5)
    st, _, panel = d.hits()
    if 'mode:games' not in panel:
        raise Fail(f'no Games in the mode picker: {list(panel)}')
    d.tap('mode:games', panel_only=True)


def set_mode(d, mode_id):
    """Through the mode picker on the bar."""
    d.mark_now(); d.tap('Mode'); time.sleep(1.5)
    st, _, panel = d.hits()
    if st.get('modes') != '1':
        raise Fail(f'the mode picker did not open: {st}')
    d.mark_now(); d.tap(f'mode:{mode_id}', panel_only=True)
    d.expect(re.escape(MODE_STATUS[mode_id]), what=f'mode {mode_id}')
    st = d.hits()[0]
    if st['mode'] != mode_id or st.get('modes') != '0':
        raise Fail(f'picking {mode_id} left {st}')
MENU = (1724, 66)


def scribble(x=300, y=700):
    return f'penstroke {x} {y} {x + 400} {y + 60}\nwait 50\npenstroke {x + 50} {y + 120} {x + 380} {y + 100}'


# ----------------------------------------------------------------------------- input-layer tests

def t_pen_menu_close(d):
    d.mark_now(); d.inp(f'pentap {MENU[0]} {MENU[1]}')
    d.expect(r'bar: pen Menu'); d.expect(r'status: menu')
    if d.hits()[0].get('modes') != '3':
        raise Fail('the Menu drop-down did not open')
    d.shot('menu')
    time.sleep(0.5)
    d.mark_now(); d.inp(f'pentap {MENU[0]} {MENU[1]}')
    d.expect(r'bar: pen Menu'); time.sleep(1)
    if d.hits()[0].get('modes') != '0':
        raise Fail('a second tap on Menu did not close its drop-down')


def t_pen_bounce(d):
    d.mark_now(); d.inp(f'penbounce {MENU[0]} {MENU[1]}')
    d.expect(r'bar: pen Menu'); d.expect(r'double tap on Menu, ignored')
    time.sleep(1)
    if d.hits()[0].get('modes') != '3':
        raise Fail('a bouncing tap closed the Menu drop-down again')
    d.home()


def t_finger_with_pen_near(d):
    d.mark_now(); d.inp(f'penin 900 1600\nwait 60\nfingertap {MENU[0]} {MENU[1]}\nwait 100\npenout')
    d.expect(r'bar: finger Menu', what='finger tap on the bar while the pen hovers')
    d.home()


def t_palm_plus_finger(d):
    d.mark_now(); d.inp(f'fingerdown 900 1900 0\nwait 120\nfingertap {MENU[0]} {MENU[1]} 1\nwait 150\nfingermove 1300 1900 0\nwait 30\nfingerup 0')
    d.expect(r'bar: finger Menu', what='finger tap with a palm on the screen')
    d.expect_not(r'flick')
    d.home()


def t_forgiving_zones(d):
    mx, my = d.hits()[1]['Menu']  # Menu is 236 px wide: 4 px left of it is in the gap, nearer Menu than Send
    for x, y, what in ((mx, 150, 'status line under Menu'), (mx - 118 - 4, 60, 'gap between Send and Menu'), (1856, 8, 'top-right corner')):
        d.mark_now(); d.inp(f'pentap {x} {y}')
        d.expect(r'bar: pen Menu', what=what)
        d.home()


def t_slide_off(d):
    d.mark_now(); d.inp(f'fingerdown {MENU[0]} {MENU[1]} 0\nwait 60\nfingermove {MENU[0]} 600 0\nwait 60\nfingerup 0')
    d.expect(r'slid off Menu, ignored')
    if d.hits()[0]['menu'] != '0':
        raise Fail('menu opened although the finger slid off')


def t_taps_during_long_redraw(d):
    d.mark_now()
    d.inp(f'stall 3000\nwait 300\npenhover 900 1500 1200\npentap {MENU[0]} {MENU[1]}\nwait 700\npentap {MENU[0]} {MENU[1]}\nwait 700\npentap {MENU[0]} {MENU[1]}')
    d.expect(r'dev: stalling 3000 ms')
    d.expect(r'(?s)bar: pen Menu.*bar: pen Menu.*bar: pen Menu', timeout=12, what='all three taps after the stall')
    d.expect(r'input: events waited \d+ ms')
    d.home()


def t_flicks(d):
    d.home()
    d.mark_now(); d.inp('flick 1500 1500 300')
    d.expect(r'this is a fresh page', what='no new blank page after a blank one')
    d.inp(scribble(300, 2000)); time.sleep(0.8)
    d.mark_now(); d.inp('flick 1500 1500 300')
    d.expect(r'flick left \(next page\)')
    d.expect(r'status: page (\d+) of \1 \(new\)', timeout=20, what='next page after the answer')
    d.mark_now(); d.inp('flick 300 1500 1500')
    d.expect(r'flick right \(previous page\)'); d.expect(r'status: page \d+ of \d+')


def t_palm_rejected_on_panel(d):
    d.mark_now(); d.inp(f'pentap {MENU[0]} {MENU[1]}'); d.expect(r'status: menu')
    _, _, panel = d.hits()
    x, y = panel['menu:exit']
    d.mark_now(); d.inp(f'penin 900 300\nwait 60\nfingertap {x} {y}\nwait 100\npenout')
    d.expect(r'ignored \(pen in range\)')
    if d.hits()[0].get('modes') != '3':
        raise Fail('palm touch acted on the Menu drop-down')
    d.home()


# ----------------------------------------------------------------------------- functions

def t_modes(d):
    st, bar, _ = d.hits()
    if 'Auto' in bar or 'Erase' in bar or 'Chat' in bar or 'Games' in bar:
        raise Fail(f'old buttons on the bar: {list(bar)}')
    d.tap('Mode'); time.sleep(1.5)
    rows = [k for k in d.hits()[2] if k.startswith('mode:')]
    if rows != ['mode:talk', 'mode:notes', 'mode:math', 'mode:translate', 'mode:games']:
        raise Fail(f'mode picker rows: {rows}')
    d.shot('mode-picker')
    d.inp('pentap 1400 1600'); time.sleep(1.5)  # a tap beside the card closes it, nothing changes
    st = d.hits()[0]
    if st.get('modes') != '0' or st['mode'] != 'talk':
        raise Fail(f'tap outside the picker: {st}')
    d.tap('Mode'); time.sleep(1.2); d.tap('Mode'); time.sleep(1.2)  # the mode button closes it again
    if d.hits()[0].get('modes') != '0':
        raise Fail('the mode button did not close the picker')
    for mid in ('notes', 'math', 'translate', 'talk'):
        set_mode(d, mid)


def t_send_and_autoreply(d):
    d.fake('Hello there! This is a test reply.')
    d.mark_now(); d.inp(scribble()); time.sleep(1.2); send_now(d)
    d.expect(r'status: thinking'); d.expect(r'turn ok .* mode talk', timeout=20)
    d.wait_idle()
    d.mark_now(); d.inp(scribble(300, 1100))
    m = d.expect(r'turn ok .* on the page ([\d.]+) s after the pen lifted', timeout=20, what='auto-reply after the pause')
    if float(m.group(1)) < 2.4:
        raise Fail(f'the answer came before the 2.5 s pause was over ({m.group(1)} s)')
    d.wait_idle()
    d.shot('chat-reply')


def t_drawing(d):
    """A reply that asks for a picture: the text first, then the drawing pass draws it below as smooth strokes, and the
    next turn isn't held up meanwhile."""
    import math
    new_page(d)
    circle = [[500 + 300 * math.cos(t / 20 * 2 * math.pi), 500 + 300 * math.sin(t / 20 * 2 * math.pi)] for t in range(21)]
    d.fake_draw([circle, [[380, 420], [420, 440]], [[580, 440], [620, 420]], [[400, 620], [500, 680], [600, 620]]])
    d.fake("Here's a smiley for you!", illustrate='a smiling face')
    d.mark_now(); d.inp(scribble()); time.sleep(1.2); send_now(d)
    d.expect(r'turn ok .* mode talk', timeout=20)
    d.expect(r'drawing with \S+: a smiling face', timeout=10)
    d.expect(r'drawing: 4 strokes, \d+ points', timeout=20)
    d.expect(r'drawing on the page', timeout=20)
    d.wait_idle()
    time.sleep(1.5)
    # the circle (radius 300 of 1000 on a square up to 1000 px wide, centred) left ink in the middle band below the reply
    if d.ink(330, 900, 1200, 1500) < 1500:
        raise Fail('no drawing on the page')
    d.shot('drawing-pass')
    d.no_fake()


def t_menu_while_writing_reply(d):
    d.fake('This reply is long on purpose so that it is still being written when the menu button is tapped. ' * 3)
    d.mark_now(); d.inp(scribble(300, 1500)); time.sleep(1); send_now(d)
    d.expect(r'turn ok', timeout=20)
    time.sleep(0.5)
    d.mark_now(); d.inp(f'pentap {MENU[0]} {MENU[1]}')
    d.expect(r'bar: pen Menu'); d.expect(r'status: menu')
    st = d.hits()[0]
    if st.get('modes') != '3' or st['nops'] != '0':
        raise Fail(f'menu during a reply: {st}')
    d.home()


def t_menu_while_thinking(d):
    d.fake('ok')
    d.mark_now(); d.inp(scribble(300, 1900)); time.sleep(1); send_now(d)
    d.expect(r'status: thinking')
    d.mark_now(); d.inp(f'pentap {MENU[0]} {MENU[1]}')
    d.expect(r'wait for the answer to finish')
    d.wait_idle()


def t_page_menu(d):
    """New page (Menu), delete this page and start over (Clear), through the drop-downs."""
    d.home()
    st0 = d.hits()[0]
    d.mark_now(); menu(d, 'newpage'); m = d.expect(r'page (\d+) of \1 \(new\)')
    n = int(m.group(1))
    d.inp(scribble(400, 800)); time.sleep(1.5)
    d.mark_now(); d.tap('Clear'); time.sleep(1.5)
    d.tap('clear:delete', panel_only=True); d.expect(r'Delete this page - tap it again')
    d.shot('confirm-delete')
    d.tap('clear:delete', panel_only=True); d.expect(rf'page {n} deleted')
    st = d.hits()[0]
    if st['last_page'] != str(n - 1):
        raise Fail(f'after deleting page {n}: {st}')
    d.mark_now(); d.tap('Clear'); time.sleep(1.5)
    d.tap('clear:startover', panel_only=True); time.sleep(0.8)
    d.tap('clear:startover', panel_only=True); d.expect(r'a fresh start')
    st = d.home()
    if st['page'] != '1' or st['last_page'] != '1':
        raise Fail(f'after Start over: {st}')


def t_translate(d):
    set_mode(d, 'translate')
    d.fake('Hola, ¿cómo estás?', mode='translate')
    d.mark_now(); d.inp(scribble(300, 700)); time.sleep(1); send_now(d)
    d.expect(r'turn ok .* mode translate', timeout=20); d.wait_idle()
    d.shot('translate-reply')
    set_mode(d, 'talk')


def t_settings(d):
    d.home()
    d.mark_now(); menu(d, 'settings'); time.sleep(1.5)
    st, _, panel = d.hits()
    if st['settings'] != '1':
        raise Fail('Settings tile did not open settings')
    d.shot('settings-home')
    cats = [k for k in panel if '=' not in k and k != '< Back']
    if len(cats) < 7:
        raise Fail(f'settings categories: {cats}')
    # one Voices section: everyday and fantasy voices together, all on screen
    d.tap('Voices', panel_only=True); time.sleep(1.5)
    st, _, panel = d.hits()
    voices = [k for k in panel if k.startswith('reply_voice=')]
    if len(voices) != 30 or 'reply_voice=friendly' not in panel or 'reply_voice=evil_wizard' not in panel:
        raise Fail(f'Voices section has {len(voices)} voices')
    low = max(y for (x, y) in panel.values())
    if low + 56 > 2470:
        raise Fail(f'Voices section runs off the screen (last chip centre y={low})')
    d.shot('settings-voices')
    d.tap('< Back', panel_only=True); time.sleep(1.2)
    # one Handwriting & fonts section: all 30 handwriting styles, size, speed and the typed fonts, all on screen
    d.tap('Handwriting & fonts', panel_only=True); time.sleep(2)
    st, _, panel = d.hits()
    hw = [k for k in panel if k.startswith('handwriting_font=')]
    tf = [k for k in panel if k.startswith('typed_font=')]
    if len(hw) != 30 or len(tf) < 10 or not any(k.startswith('writing_speed=') for k in panel):
        raise Fail(f'Handwriting & fonts: {len(hw)} styles, {len(tf)} typed fonts')
    low = max(y for (x, y) in panel.values())
    if low + 50 > 2470:
        raise Fail(f'Handwriting & fonts runs off the screen (last chip centre y={low})')
    d.shot('settings-fonts')
    d.tap('< Back', panel_only=True); time.sleep(1.2)
    changed = []
    for cat in cats:
        d.tap(cat, panel_only=True); time.sleep(1.5)
        st, _, panel = d.hits()
        if st['settings_page'] == '-1':
            raise Fail(f'category {cat} did not open')
        chips = [k for k in panel if '=' in k]
        if not chips and any(k.startswith(('ai:', 'field:')) for k in panel):
            d.shot('settings-' + re.sub(r'\W+', '-', cat.lower()).strip('-'))
            d.tap('< Back', panel_only=True); time.sleep(1.2)
            continue  # AI & keys has its own controls (t_ai_keys)
        if not chips:
            raise Fail(f'category {cat} shows no options')
        d.shot('settings-' + re.sub(r'\W+', '-', cat.lower()).strip('-'))
        # pick an option that differs from the current one and isn't the AI provider/model (they need keys)
        for chip in chips:
            key, val = chip.split('=', 1)
            if panel[chip][1] > 2400:
                continue
            if key in ('provider', 'anthropic_model', 'openai_model'):
                continue
            cur = d.config(key)
            if cur is not None and cur != val:
                d.tap(chip, panel_only=True); time.sleep(1.2)
                now = d.config(key)
                if now != val:
                    raise Fail(f'{chip}: config.txt has {key} = {now}')
                changed.append(chip)
                break
        d.tap('< Back', panel_only=True); time.sleep(1.2)
    # flick between category pages
    d.tap(cats[0], panel_only=True); time.sleep(1.2)
    for flick, want in (('flick 1500 1500 300', '1'), ('flick 1500 1500 300', '2'), ('flick 300 1500 1500', '-1')):
        d.inp(flick); time.sleep(1.8)
        st = d.hits()[0]
        if st['settings_page'] != want:
            raise Fail(f'{flick} in settings went to section {st["settings_page"]}, expected {want}')
    d.home()
    print('    changed:', ', '.join(changed))


def t_games_local(d):
    d.home()
    d.mark_now(); games(d); d.expect(r'status: games')
    st, _, panel = d.hits()
    d.shot('games-menu')
    glist = [k for k in panel if k.startswith('game:')]
    if len(glist) < 21:
        raise Fail(f'only {len(glist)} local games listed: {glist}')
    for g in glist:
        gid = g.split(':', 1)[1]
        d.mark_now(); d.tap(g, panel_only=True)
        if gid == 'wordsearch':  # a puzzle saved earlier: the choice screen first
            time.sleep(1.5)
            if 'choice:new' in d.hits()[2]:
                d.tap('choice:new', panel_only=True)
        d.expect(rf'game: {re.escape(gid)} \(level \d\)', what=g)
        time.sleep(2.0)
        d.shot('game-' + gid)
        d.mark_now(); games(d)
        d.expect(r'status: games - tap one', what=f'back to the games menu from {gid}')
        time.sleep(0.8)
    d.home()


def t_games_picker(d):
    d.home()
    d.mark_now(); games(d); d.expect(r'status: games')
    d.mark_now(); d.tap('game:tictactoe', panel_only=True); d.expect(r'game: tictactoe'); time.sleep(2)
    st, bar, _ = d.hits()
    d.shot('games-mode-button')
    d.mark_now(); d.tap('Mode'); time.sleep(1.5)
    if 'mode:notes' not in d.hits()[2]:
        raise Fail('mode picker did not open in a game')
    d.tap('mode:notes', panel_only=True)
    d.expect(r'back to your notebook'); d.expect(r'Type up:')
    st = d.hits()[0]
    if st['games'] != '0' or st['mode'] != 'notes':
        raise Fail(f'picking Type up from a game: {st}')
    set_mode(d, 'talk')


def t_games_ai(d):
    d.fake('Let us play!')
    d.home()
    d.mark_now(); games(d); d.expect(r'status: games')
    d.tap('tab:ai', panel_only=True); time.sleep(1.5)
    st, _, panel = d.hits()
    ais = [k for k in panel if k.startswith('ai:')]
    if len(ais) < 16:
        raise Fail(f'only {len(ais)} AI games listed')
    for g in ais:
        d.mark_now(); d.tap(g, panel_only=True); time.sleep(1.5)
        st = d.hits()[0]
        if st['games'] != '0':
            raise Fail(f'{g}: still in the games menu')
        d.mark_now(); games(d); d.expect(r'status: games', what=f'games menu again after {g}')
        time.sleep(1)
    d.tap('tab:local', panel_only=True); time.sleep(1)
    d.home()


def t_levels(d):
    d.home()
    for level, n in (('Easy', 0), ('Medium', 1), ('Hard', 2)):
        d.mark_now(); games(d); d.expect(r'status: games')
        d.tap(level, panel_only=True); time.sleep(1)
        d.mark_now(); d.tap('game:wordsearch', panel_only=True); time.sleep(1.5)
        if 'choice:new' in d.hits()[2]:  # a saved puzzle: New puzzle uses the level picked in the menu
            d.tap('choice:new', panel_only=True)
        d.expect(rf'game: wordsearch \(level {n}\)')
        time.sleep(6)
        d.shot(f'wordsearch-{level.lower()}')
        d.home()
    games(d); time.sleep(1.5); d.tap('Medium', panel_only=True); time.sleep(1); d.home()


def t_email(d):
    """Send > Email this page: the AI writes subject + body, the email goes out with the page attached (not really
    sent: the fake_email hook keeps the SMTP message in run/last_email.eml, the Resend request in last_email.json).
    First through a Gmail account (SMTP, the default), then through Resend."""
    ssh(f'printf "\\nemail_provider = gmail\\nemail_address = test@example.com\\nemail_to = test@example.com\\n" >> {USB}/config.txt; '
        f'sed -i "s/^pause_seconds = .*/pause_seconds = 0/" {USB}/config.txt; '  # no auto-reply to the scribble
        f'printf "email_password=abcd efgh ijkl mnop\\n" >> {SECURE}/keys.txt; touch {SECURE}/fake_email')
    restart(d)
    run = '/var/local/scribe-claude/run'
    inner = json.dumps({'subject': 'Shopping list', 'body': '- eggs\n- milk (½ litre)'})
    canned = json.dumps({'content': [{'type': 'text', 'text': inner}], 'stop_reason': 'end_turn'})

    def send_page():
        new_page(d)
        d.inp(scribble(300, 800)); time.sleep(0.5)
        ssh(f'cat > {SECURE}/fake_response.json', inp=canned)
        d.mark_now(); d.tap('Send'); time.sleep(1.5); d.tap('send:email', panel_only=True)
        d.expect(r'writing the email for page')
        d.expect(r'emailed "Shopping list" to test@example.com', timeout=20)

    try:
        new_page(d)
        d.tap('Send'); time.sleep(1.5)
        panel = d.hits()[2]
        if 'send:email' not in panel or 'send:now' not in panel:
            raise Fail(f'Send drop-down: {list(panel)}')
        d.shot('send-dropdown')
        d.home()
        send_page()
        import email, email.policy
        raw = subprocess.run(SSH + [f'cat {run}/last_email.eml'], capture_output=True, timeout=60).stdout
        msg = email.message_from_bytes(raw, policy=email.policy.default)
        parts = list(msg.iter_parts()) if msg.is_multipart() else []
        att = parts[1] if len(parts) == 2 else None
        if str(msg['to']) != 'test@example.com' or str(msg['subject']) != 'Shopping list' \
                or 'test@example.com' not in str(msg['from']) or not parts or '½ litre' not in parts[0].get_content() \
                or att is None or att.get_content_type() != 'image/png' or not (att.get_filename() or '').startswith('page-') \
                or len(att.get_content()) < 1000 or b'\r\n' not in raw:
            raise Fail(f'the SMTP email: {raw[:600]!r}')
        if ssh(f'ls {run}/email.eml {run}/smtp.cfg 2>/dev/null', check=False).strip():
            raise Fail('temporary email files left in run/')
        req = ssh(f'cat {run}/last_req.json', check=False)
        if 'Turn page' not in req or 'claude-haiku-5-5' not in req:
            raise Fail('the email request did not use the email prompt on Haiku')
        # the same through Resend
        ssh(f'printf "\\nemail_provider = resend\\nresend_api_key = re_test_0123456789abcdefghij\\n" >> {USB}/config.txt')
        restart(d)
        send_page()
        mail = json.loads(ssh(f'cat {run}/last_email.json'))
        att = mail.get('attachments', [{}])[0]
        if mail.get('to') != ['test@example.com'] or mail.get('subject') != 'Shopping list' or 'eggs' not in mail.get('text', '') \
                or not att.get('filename', '').startswith('page-') or len(att.get('content', '')) < 1000:
            raise Fail(f'the Resend email: { {k: (v if k != "attachments" else "...") for k, v in mail.items()} }')
    finally:  # (auto-reply back on for the tests after this one, which expect it)
        ssh(f'rm -f {SECURE}/fake_email {run}/last_email.json {run}/last_email.eml; '
            f'sed -i "s/^pause_seconds = .*/pause_seconds = 2.5/" {USB}/config.txt')
        d.fake('A canned test reply.')
        restart(d)


def kb_type(d, text):
    """Types on the on-screen keyboard (letters, digits, spaces and - _ . @ + / = : ~)."""
    panel = d.hits()[2]
    for ch in text:
        if ch.isupper():
            x, y = panel['kb:Shift']; d.inp(f'pentap {x} {y}'); time.sleep(1.2)
            panel = d.hits()[2]
        k = 'kb:space' if ch == ' ' else f'kb:{ch.lower()}'  # keys are labelled in lower case; Shift makes the next one a capital
        if k not in panel:
            raise Fail(f'no key {ch!r} on the keyboard')
        x, y = panel[k]
        d.inp(f'pentap {x} {y}'); time.sleep(0.45)
        if ch.isupper():
            panel = d.hits()[2]


def t_keyboard(d):
    d.home()
    menu(d, 'settings'); time.sleep(1.5)
    d.tap('Email', panel_only=True); time.sleep(1.5)
    st, _, panel = d.hits()
    if 'field:email_address' not in panel or 'email_provider=gmail' not in panel:
        raise Fail(f'Email section: {list(panel)}')
    if 'field:smtp_server' in panel:
        raise Fail('the server field shows for Gmail')
    d.shot('settings-email')
    d.tap('field:email_address', panel_only=True); time.sleep(2)
    panel = d.hits()[2]
    if 'kb:q' not in panel or 'kb:Save' not in panel:
        raise Fail(f'the keyboard did not open: {list(panel)[:12]}')
    d.shot('keyboard')
    x, y = panel['kb:Clear']; d.inp(f'pentap {x} {y}'); time.sleep(0.8)  # the field opens with the current value
    kb_type(d, 'Test@exampleX.com')
    x, y = panel['kb:Delete']
    for _ in range(5):  # remove 'X.com', then type '.com' again: Delete works
        d.inp(f'pentap {x} {y}'); time.sleep(0.45)
    kb_type(d, '.com')
    d.mark_now(); d.tap('kb:Save', panel_only=True); d.expect(r'setting email_address = Test@example.com')
    if d.config('email_address') != 'Test@example.com':
        raise Fail(f'config.txt email_address = {d.config("email_address")}')
    time.sleep(1.5)
    # a key field: typing then Cancel changes nothing
    before = ssh('md5sum /var/local/scribe-claude/keys.txt 2>/dev/null', check=False)
    d.tap('< Back', panel_only=True); time.sleep(1.2)
    d.tap('AI', panel_only=True); time.sleep(1.5)
    d.tap('ai:anthropic', panel_only=True); time.sleep(1.5)   # show Claude's key box
    d.tap('ai:key', panel_only=True); time.sleep(2)
    kb_type(d, 'abc')
    d.mark_now(); d.tap('kb:Cancel', panel_only=True); d.expect(r'not changed')
    if ssh('md5sum /var/local/scribe-claude/keys.txt 2>/dev/null', check=False) != before:
        raise Fail('Cancel changed the stored keys')
    d.home()


def t_ai_keys(d):
    d.home()
    menu(d, 'settings'); time.sleep(1.5)
    d.tap('AI', panel_only=True); time.sleep(2)
    panel = d.hits()[2]
    if 'ai:tab:services' not in panel or 'ai:tab:features' not in panel:
        raise Fail(f'no AI services / Each feature tabs: {list(panel)}')
    ids = [k for k in panel if k.startswith('ai:') and k not in ('ai:key', 'ai:test', 'ai:server-setup', 'ai:tab:services',
                                                                   'ai:tab:features', 'ai:add-server')]
    if ids != ['ai:anthropic', 'ai:openai', 'ai:gemini', 'ai:bedrock', 'ai:xai', 'ai:mistral', 'ai:openrouter', 'ai:custom'] or \
            'ai:add-server' not in panel:
        raise Fail(f'AI services: {ids}')
    if any('github' in k for k in panel):
        raise Fail('the GitHub token row is still there')
    d.shot('settings-ai')
    d.mark_now(); d.tap('ai:gemini', panel_only=True); d.expect(r'add a Gemini key below')
    time.sleep(1.5)
    panel = d.hits()[2]
    if not any(k.startswith('model:gemini') for k in panel) or d.config('provider') == 'gemini':
        raise Fail(f'Gemini details: {[k for k in panel if k.startswith("model:")]}, provider {d.config("provider")}')
    d.tap('ai:bedrock', panel_only=True); time.sleep(1.8)
    panel = d.hits()[2]
    if len([k for k in panel if k.startswith('region:')]) < 6:
        raise Fail('no regions for Amazon Bedrock')
    d.shot('settings-ai-bedrock')
    d.mark_now(); d.tap('ai:anthropic', panel_only=True); d.expect(r'Claude answers now')
    time.sleep(1.5)
    d.mark_now(); d.tap('ai:test', panel_only=True)  # a free model-list call with the real key
    d.expect(r'checking the Claude key'); d.expect(r'key check: works', timeout=30)
    d.home()


def t_update_check(d):
    d.home()
    d.mark_now(); menu(d, 'update')
    d.expect(r'status: .*(latest|available|private|token|GitHub|update)', timeout=40, what='an answer from the update check')
    d.shot('update-check')
    d.home()


def t_update_install(d):
    """--update-install: the in-app update from the real releases repository. Install a --dev build with a VERSION
    below the latest release first; it updates itself to that release (download, signature, install, restart)."""
    d.home()
    d.mark_now(); menu(d, 'update')
    m = d.expect(r'status: Version ([\d.]+) is available - tap again to install', timeout=40, what='an update offer')
    want = m.group(1)
    if not re.search(r'status: Checking the update.s signature', d.since()):
        raise Fail('the offer came without checking the signed manifest')
    d.mark_now(); menu(d, 'update')
    d.expect(rf'update: installed {re.escape(want)} \(was ', timeout=180)
    # (the new version is a release build: no developer hooks, so nothing taps it from here on)
    d.expect(r'launch: Inkling was updated, starting the new version', timeout=30)
    d.expect(r'\d start: screen', timeout=30, what='the updated app starting')
    time.sleep(8)
    if 'failed to start' in d.since():
        raise Fail('the updated app failed to start (rolled back)')
    got = ssh('cat /var/local/scribe/app/VERSION 2>/dev/null || true').strip()
    print(f'  updated to {want} (installed VERSION file: {got or "-"})', flush=True)


def t_wordsearch_resume(d):
    d.home()
    d.mark_now(); games(d); d.expect(r'status: games')
    d.tap('Medium', panel_only=True); time.sleep(1)
    st, _, panel = d.hits()
    if 'choice:continue' in panel or 'SAVED' in str(panel):
        pass
    d.mark_now(); d.tap('game:wordsearch', panel_only=True); time.sleep(1.5)
    st, _, panel = d.hits()
    if 'choice:new' in panel:  # a puzzle from an earlier run: start fresh
        d.mark_now(); d.tap('choice:new', panel_only=True)
    d.expect(r'game: wordsearch \(level 1\)'); time.sleep(6)
    d.shot('wordsearch-before-leaving')
    d.mark_now(); games(d); d.expect(r'game: saved wordsearch \(level 1, 0 of 20 found\)')
    d.expect(r'status: games - tap one')
    d.mark_now(); d.tap('game:wordsearch', panel_only=True); time.sleep(1.5)
    st, _, panel = d.hits()
    for k in ('choice:continue', 'choice:new', 'choice:back'):
        if k not in panel:
            raise Fail(f'no {k} on the choice screen: {list(panel)}')
    d.shot('wordsearch-choice')
    d.mark_now(); d.tap('choice:continue', panel_only=True)
    d.expect(r'game: wordsearch resumed \(level 1, 0 of 20 found\)'); time.sleep(5)
    d.shot('wordsearch-resumed')
    # leaving the app saves too, and a restart keeps it
    d.mark_now(); d.tap('Close'); d.expect(r'game: saved wordsearch')
    restart(d)
    d.mark_now(); games(d); d.expect(r'status: games')
    d.tap('game:wordsearch', panel_only=True); time.sleep(1.5)
    if 'choice:continue' not in d.hits()[2]:
        raise Fail('the saved puzzle did not survive a restart')
    d.mark_now(); d.tap('choice:new', panel_only=True); d.expect(r'game: wordsearch \(level 1\)')
    time.sleep(3); d.home()


def t_exit_and_power(d):
    d.home()
    d.mark_now(); menu(d, 'exit')
    d.expect(r'exit \(Exit button\)'); d.expect(r'launch: app exited')
    relaunch(d)
    d.mark_now(); d.inp('power')
    d.expect(r'exit \(power key\)')
    d.expect(r'launch: app exited \(3\)')
    d.expect(r'Inkling reopens after it wakes and is unlocked')
    if ssh(f'cat {APP}/reopen-on-wake 2>/dev/null', check=False).strip() == '':
        raise Fail('no launcher waiting to reopen Inkling after the wake-up')
    relaunch(d)  # opening by hand cancels the waiting launcher
    if ssh(f'cat {APP}/reopen-on-wake 2>/dev/null', check=False).strip():
        raise Fail('opening Inkling by hand did not cancel the reopen-after-wake')


def t_eraser_and_page_memory(d):
    d.fake('ok')
    st = d.home()
    d.mark_now(); menu(d, 'newpage'); d.expect(r'\(new\)')
    ssh(f'sed -i "s/^pause_seconds = .*/pause_seconds = 0/" {USB}/config.txt')  # no auto-reply while drawing
    d.inp('penstroke 300 1200 1500 1200\nwait 40\npenstroke 300 1400 1500 1400'); time.sleep(2)
    a, b = d.ink(300, 1180, 1200, 40), d.ink(300, 1380, 1200, 40)
    if a < 500 or b < 500:
        raise Fail(f'pen lines not drawn (dark px {a}, {b})')
    d.inp('erasestroke 250 1200 1550 1200'); time.sleep(2)
    a2, b2 = d.ink(300, 1180, 1200, 40), d.ink(300, 1380, 1200, 40)
    if a2 > a * 0.05 or b2 < b * 0.9:
        raise Fail(f'eraser: erased line has {a2}/{a} dark px left, other line {b2}/{b}')
    page = d.hits()[0]['page']
    d.mark_now(); d.inp('flick 300 1500 1500'); d.expect(r'flick right'); time.sleep(2)
    d.mark_now(); d.inp('flick 1500 1500 300'); d.expect(rf'status: page {page} of'); time.sleep(2)
    b3 = d.ink(300, 1380, 1200, 40)
    if b3 < b * 0.9 or d.ink(300, 1180, 1200, 40) > a * 0.05:
        raise Fail(f'page not restored after turning away and back ({b3}/{b} dark px)')
    restart(d)
    st = d.hits()[0]
    if st['page'] != page or d.ink(300, 1380, 1200, 40) < b * 0.9:
        raise Fail(f'after a restart: page {st["page"]} (want {page}), ink {d.ink(300, 1380, 1200, 40)}/{b}')
    ssh(f'sed -i "s/^pause_seconds = .*/pause_seconds = 2.5/" {USB}/config.txt')
    restart(d)


def set_setting(d, category, chip):
    """Through the settings page, like a person would."""
    d.home()
    d.mark_now(); menu(d, 'settings'); time.sleep(1.5)
    d.tap(category, panel_only=True); time.sleep(1.5)
    d.tap(chip, panel_only=True); time.sleep(1.2)
    key, val = chip.split('=', 1)
    if d.config(key) != val:
        raise Fail(f'setting {chip} not saved (config has {d.config(key)})')
    d.home()


def new_page(d):
    d.wait_idle()  # an answer still being written would make Menu say "wait for the answer to finish"
    d.home()
    d.mark_now(); menu(d, 'newpage'); d.expect(r'\(new\)')


def t_early_send(d):
    """The turn goes out 0.8 s after the pen lifts; writing again cancels it; one answer for the whole page."""
    new_page(d)
    d.mark_now()
    d.inp('penstroke 300 900 900 900'); time.sleep(1.6)       # past 0.8 s: an early turn is in flight (fake: 1 s)
    d.inp('penstroke 300 1000 900 1000')                      # writing again cancels it
    d.expect(r'early (turn cancelled|answer dropped): you kept writing', timeout=5)
    m = d.expect(r'turn ok .* on the page ([\d.]+) s after the pen lifted', timeout=20)
    time.sleep(3)
    n = len(re.findall(r'turn ok', d.since()))
    if n != 1:
        raise Fail(f'{n} answers for one page')
    if 'Erase' in d.hits()[1]:
        raise Fail('Erase is still on the bar')


def t_clear_button(d):
    set_setting(d, 'Pen & timing', 'pause_seconds=0')
    new_page(d)
    d.inp('penstroke 300 1500 1500 1500\nwait 40\npenstroke 300 1700 1500 1700'); time.sleep(2)
    a = d.ink(300, 1480, 1200, 240)
    d.mark_now(); d.tap('Clear'); time.sleep(1.5)
    st, bar, panel = d.hits()
    rows = [k for k in panel if k.startswith('clear:')]
    if st.get('modes') != '2' or rows != ['clear:page', 'clear:forget', 'clear:both', 'clear:delete', 'clear:startover']:
        raise Fail(f'Clear drop-down: state {st}, rows {rows}')
    cx, cy = bar['Clear']
    if d.ink(cx - 60, cy - 30, 120, 60) < 120 * 60 * 0.6:
        raise Fail('the Clear button is not dark while its drop-down is open')
    d.shot('clear-dropdown')
    d.mark_now(); d.tap('clear:page', panel_only=True); d.expect(r'Clear this page - tap it again to confirm')
    d.shot('clear-armed')
    d.expect(r'clear - tap an option twice', timeout=8, what='the armed row goes back after 4 s')
    d.mark_now(); d.tap('clear:page', panel_only=True); d.expect(r'tap it again')
    time.sleep(4.5)
    d.tap('clear:page', panel_only=True); time.sleep(1)  # after the 4 s: arms again, doesn't clear
    if d.ink(300, 1480, 1200, 240) < a * 0.9:
        raise Fail('a tap after the 4 s cleared the page')
    d.inp('pentap 300 2300'); time.sleep(1.5)  # beside the card: closes it
    if d.hits()[0].get('modes') != '0':
        raise Fail('a tap beside the Clear drop-down did not close it')
    d.mark_now(); d.tap('Clear'); time.sleep(1.5)
    d.tap('clear:page', panel_only=True); time.sleep(0.8)
    d.tap('clear:page', panel_only=True); d.expect(r'page \d+ cleared - the conversation is kept')
    time.sleep(1)
    if d.ink(300, 1480, 1200, 240) > 50:
        raise Fail(f'Clear left {d.ink(300, 1480, 1200, 240)} dark px')
    d.mark_now(); d.tap('Clear'); time.sleep(1.5)
    d.tap('clear:forget', panel_only=True); time.sleep(0.8)
    d.tap('clear:forget', panel_only=True); d.expect(r'conversation forgotten')
    set_setting(d, 'Pen & timing', 'pause_seconds=2.5')


def t_pen_keeps_up(d):
    """Writing must never fall behind the pen (scratch-out snapshot, saves...)."""
    new_page(d)
    d.mark_now()
    for i in range(6):
        d.inp(f'penstroke 200 {700 + i * 120} 1600 {720 + i * 120}\nwait 300\npenstroke 220 {760 + i * 120} 1500 {740 + i * 120}')
        time.sleep(1.8 if i % 2 else 0.7)  # long enough pauses for the idle autosave to run in between
    time.sleep(2)
    m = re.findall(r'pen: drawing fell up to (\d+) ms behind', d.since())
    if m:
        raise Fail(f'drawing fell behind the pen: {m} ms')
    if not re.search(r'thinking', d.since()) and False:
        pass


def t_scratch_out(d):
    sys.path.insert(0, str(ROOT / 'device' / 'tools' / 'demo'))
    import run_demo
    new_page(d)
    d.inp('penstroke 300 1000 900 1000\nwait 30\npenstroke 320 1030 880 1020\nwait 30\npenstroke 300 1300 900 1300'); time.sleep(2)
    a, b = d.ink(280, 980, 640, 70), d.ink(280, 1280, 640, 40)
    d.mark_now(); d.inp('penzigzag 280 1015 920 6'); d.expect(r'scratch-out: erased')
    time.sleep(1)
    a2, b2 = d.ink(280, 980, 640, 70), d.ink(280, 1280, 640, 40)
    if a2 > a * 0.05 or b2 < b * 0.9:
        raise Fail(f'scratch-out: crossed-out words {a}->{a2} dark px, line below {b}->{b2}')
    # ordinary handwriting must never count as a scratch-out, on blank paper or next to ink
    d.mark_now(); run_demo.write(['minimum mammal momentum', 'swimming in winter'], x0=200, y0=1600)
    d.expect_not(r'scratch-out', wait=1)
    if d.ink(150, 1500, 1500, 300) < 2000:
        raise Fail('handwriting missing')


def t_typeup_in_place(d):
    new_page(d)
    set_mode(d, 'notes')
    d.fake('Shopping list\n- eggs\n- milk', mode='notes', style='typeset', read='Shopping list - eggs - milk')
    # handwriting on the right, where the short typed lines won't reach
    d.inp('penstroke 1100 900 1700 900\nwait 30\npenstroke 1100 960 1700 950'); time.sleep(1.5)
    d.mark_now(); send_now(d)
    m = d.expect(r'type up: handwriting wiped, text starts at y (\d+)', timeout=20)
    y = int(m.group(1)); d.wait_idle()
    left = d.ink(1100, 880, 600, 100)
    d.shot('typeup-replace')
    if left > 200 or not 840 <= y <= 960:
        raise Fail(f'Type up: {left} dark px of handwriting left, text at y {y} (handwriting was at 900)')
    if d.ink(60, y - 20, 900, 200) < 500:
        raise Fail('no typed text where the handwriting was')
    set_setting(d, 'Replies', 'typeup=below')
    new_page(d)
    set_mode(d, 'notes')
    d.inp('penstroke 1100 900 1700 900'); time.sleep(1.5)
    d.mark_now(); send_now(d); d.expect(r'turn ok .* mode notes', timeout=20); d.wait_idle()
    d.expect_not(r'type up: handwriting wiped', wait=0.5)
    if d.ink(1100, 880, 600, 40) < 300:
        raise Fail('typeup=below wiped the handwriting')
    d.shot('typeup-below')
    set_setting(d, 'Replies', 'typeup=replace')
    set_setting(d, 'Pen & timing', 'pause_seconds=2.5')
    set_mode(d, 'talk'); d.fake('A canned test reply.')


# ----------------------------------------------------------------------------- Advanced AI and the own server

def open_ai(d, tab='services'):
    """Settings > AI, on one of its two tabs (services or features)."""
    d.home()
    menu(d, 'settings'); time.sleep(1.5)
    d.tap('AI', panel_only=True); time.sleep(2)
    d.tap(f'ai:tab:{tab}', panel_only=True); time.sleep(1.8)


def open_feature(d, fid):
    """Settings > AI > Each feature > that feature (past the one-time warning)."""
    open_ai(d, 'features')
    d.tap(f'adv:row:{fid}', panel_only=True); time.sleep(2)
    if 'adv:ok' in d.hits()[2]:
        d.tap('adv:ok', panel_only=True); time.sleep(1.5)


def last_request():
    return json.loads(ssh(f'cat {SECURE}/run/last_req.json'))


def t_advanced_ai(d):
    d.home()
    ssh(f'sed -i "/^ai_advanced *=/d" {USB}/config.txt')  # the warning shows until it has been read once
    restart(d)
    open_ai(d, 'features')
    rows = [k for k in d.hits()[2] if k.startswith('adv:row:')]
    if rows != ['adv:row:chat', 'adv:row:typeup', 'adv:row:math', 'adv:row:translate', 'adv:row:games', 'adv:row:drawing',
                'adv:row:email', 'adv:row:all']:
        raise Fail(f'Each feature rows: {rows}')
    d.shot('ai-each-feature')
    d.tap('adv:row:math', panel_only=True); time.sleep(2)
    panel = d.hits()[2]
    if 'adv:ok' not in panel or any(k.startswith('adv:svc:') for k in panel):
        raise Fail(f'the warning did not come first: {list(panel)}')
    d.shot('adv-warning')
    d.mark_now(); d.tap('adv:ok', panel_only=True); d.expect(r'each feature can have its own AI')
    time.sleep(1.5)
    if d.config('ai_advanced') != 'understood':
        raise Fail(f'ai_advanced = {d.config("ai_advanced")}')
    panel = d.hits()[2]
    missing = [k for k in ('adv:tab:service', 'adv:tab:tuning', 'adv:svc:default', 'adv:svc:anthropic', 'adv:svc:custom',
                           'adv:model:default', 'adv:model:claude-opus-5-5', 'adv:model:other') if k not in panel]
    if missing or 'adv:model:fast' in panel:
        raise Fail(f'Math page: missing {missing}, or the combined "fast" choice listed')
    d.mark_now(); d.tap('adv:model:claude-opus-5-5', panel_only=True)
    d.expect(r'setting ai_math = anthropic'); d.expect(r'setting ai_math_model = claude-opus-5-5')
    d.expect(r'status: Math: Claude Opus 5.5 - .*bigger model', what='the slower-model warning')
    time.sleep(1.5)
    d.tap('adv:tab:tuning', panel_only=True); time.sleep(2)
    panel = d.hits()[2]
    if not all(k in panel for k in ('adv:effort:high', 'adv:temp:0.5', 'adv:maxtok:8000', 'adv:instr')):
        raise Fail(f'Tuning tab: {list(panel)}')
    d.mark_now(); d.tap('adv:effort:high', panel_only=True); d.expect(r'setting ai_math_effort = high')
    time.sleep(1.5)
    d.mark_now(); d.tap('adv:temp:0.5', panel_only=True); d.expect(r'setting ai_math_temperature = 0.5')
    time.sleep(1.5)
    d.mark_now(); d.tap('adv:maxtok:8000', panel_only=True); d.expect(r'setting ai_math_max_tokens = 8000')
    time.sleep(1.5)
    d.tap('adv:instr', panel_only=True); time.sleep(2)
    kb_type(d, 'explain each step')
    d.mark_now(); d.tap('kb:Save', panel_only=True); d.expect(r'setting ai_math_instructions: 17 characters')
    time.sleep(1.5)
    if d.config('ai_math_instructions') != 'explain each step':
        raise Fail(f'ai_math_instructions = {d.config("ai_math_instructions")}')
    if 'adv:reset' not in d.hits()[2]:
        raise Fail('no reset button on a customised feature')
    d.shot('adv-math-tuning')
    d.tap('adv:tab:service', panel_only=True); time.sleep(2)
    d.shot('adv-math-service')
    # a maths turn goes to Opus with high thinking; chat stays on the defaults
    new_page(d)
    set_mode(d, 'math')
    d.fake('Check: 2 + 2 = 4.', mode='math', style='typeset')
    d.inp(scribble()); time.sleep(1.5)
    d.mark_now(); send_now(d); d.expect(r'turn ok .* mode math', timeout=20); d.wait_idle()
    req = last_request()
    if req.get('model') != 'claude-opus-5-5' or req.get('output_config', {}).get('effort') != 'high':
        raise Fail(f'math request: model {req.get("model")}, output_config {req.get("output_config")}')
    if 'explain each step' not in req.get('system', '') or 'json_schema' not in json.dumps(req.get('output_config')):
        raise Fail('math request: the instructions (or the reply schema) are missing')
    if req.get('temperature') != 0.5 or req.get('max_tokens') != 8000:
        raise Fail(f'math request: temperature {req.get("temperature")}, max_tokens {req.get("max_tokens")}')
    set_mode(d, 'talk'); d.fake('A canned test reply.')
    d.inp(scribble(300, 1400)); time.sleep(1.5)
    d.mark_now(); send_now(d); d.expect(r'turn ok .* mode talk', timeout=20); d.wait_idle()
    req = last_request()
    if req.get('model') == 'claude-opus-5-5' or req.get('output_config', {}).get('effort') != 'low' or \
            'explain each step' in req.get('system', ''):
        raise Fail(f'chat request on Math\'s settings: {req.get("model")} {req.get("output_config")}')
    # back to the defaults
    open_feature(d, 'math')
    d.mark_now(); d.tap('adv:reset', panel_only=True); d.expect(r'tap again to put Math back')  # two taps: typed text is at stake
    time.sleep(1.5)
    d.mark_now(); d.tap('adv:reset', panel_only=True); d.expect(r'setting ai_math_effort = default')
    if (d.config('ai_math'), d.config('ai_math_model'), d.config('ai_math_effort'), d.config('ai_math_instructions'),
            d.config('ai_math_temperature'), d.config('ai_math_max_tokens')) != ('default', 'default', 'default', '', 'default', 'default'):
        raise Fail('Math was not put back to its default')
    d.home()


class MockAI:
    """An OpenAI-compatible server on this computer for t_own_server: GET /v1/models, POST /v1/chat/completions. The
    first chat request that carries response_format is refused (as some local servers do), to exercise the retry."""

    def __init__(self):
        import http.server, threading
        mock = self
        self.requests = []

        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def answer(self, code, obj):
                b = json.dumps(obj).encode()
                self.send_response(code)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(b)))
                self.end_headers()
                self.wfile.write(b)

            def do_GET(self):
                mock.requests.append(('GET', self.path, dict(self.headers), None))
                if self.path == '/v1/models':
                    self.answer(200, {'object': 'list', 'data': [{'id': 'mock-vl', 'object': 'model'}, {'id': 'other', 'object': 'model'}]})
                else:
                    self.answer(404, {'error': {'message': 'not found'}})

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))) or b'{}')
                mock.requests.append(('POST', self.path, dict(self.headers), body))
                if self.path != '/v1/chat/completions':
                    return self.answer(404, {'error': {'message': 'not found'}})
                if 'response_format' in body:
                    return self.answer(400, {'error': {'message': 'response_format is not supported by this server'}})
                m = re.search(r'Mode: (\w+)', json.dumps(body.get('messages', [])))
                inner = {'read': 'shopping list', 'mode': m.group(1) if m else 'notes', 'style': 'typeset',
                         'reply': 'Answered by the mock server', 'memo': '', 'illustrate': ''}
                self.answer(200, {'id': 'mock-1', 'object': 'chat.completion', 'model': body.get('model'),
                                  'choices': [{'index': 0, 'message': {'role': 'assistant', 'content': json.dumps(inner)},
                                               'finish_reason': 'stop'}]})

        self.srv = http.server.ThreadingHTTPServer(('0.0.0.0', 0), H)
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def stop(self):
        self.srv.shutdown()
        self.srv.server_close()


def t_own_server(d):
    host = ssh('echo $SSH_CLIENT').split()[0]  # this computer, as the Kindle sees it
    mock = MockAI()
    try:
        new_page(d)  # (ink an earlier test left unanswered is answered now, by the canned reply, not by a real service)
        set_setting(d, 'Pen & timing', 'pause_seconds=2.5')  # the turns here send themselves after the pause
        d.no_fake()
        # the first self-hosted server, set up in Settings > AI > AI services
        open_ai(d, 'services')
        d.mark_now(); d.tap('ai:custom', panel_only=True); d.expect(r'set up your own server first')
        time.sleep(1.5)
        d.tap('ai:server-setup', panel_only=True); time.sleep(2)
        panel = d.hits()[2]
        if not all(k in panel for k in ('srv:url', 'srv:key', 'srv:test', 'srv:model', 'srv:api:openai', 'srv:wait:300',
                                        'srv:json:auto', 'srv:name')):
            raise Fail(f'server page: {list(panel)}')
        d.shot('ai-server-empty')
        d.tap('srv:url', panel_only=True); time.sleep(2)
        kb_type(d, f'{host}:{mock.port}/v1')  # (the field starts with http://)
        d.mark_now(); d.tap('kb:Save', panel_only=True)
        d.expect(rf'setting custom_url = http://{re.escape(host)}:{mock.port}/v1')
        d.expect(r"http:// isn't encrypted", what='the http warning')
        d.expect(r'Self-hosted answers: 2 models - pick one below', timeout=30, what='the model list')
        time.sleep(2)
        gets = [r for r in mock.requests if r[0] == 'GET']
        if not gets or gets[-1][1] != '/v1/models' or 'Authorization' in gets[-1][2]:
            raise Fail(f'model list request: {gets}')
        d.mark_now(); d.tap('srv:pick:mock-vl', panel_only=True); d.expect(r'setting custom_model = mock-vl')
        time.sleep(1.5)
        d.mark_now(); d.tap('srv:wait:60', panel_only=True); d.expect(r'setting custom_timeout = 60')
        time.sleep(1.5)
        d.shot('ai-server-set')
        # a second server (here the same mock, with a key), named
        d.tap('< Back', panel_only=True); time.sleep(2)
        if 'ai:add-server' not in d.hits()[2]:
            raise Fail('Back did not return to AI services, or no + Server')
        d.tap('ai:add-server', panel_only=True); time.sleep(2)
        d.tap('srv:name', panel_only=True); time.sleep(2)
        x, y = d.hits()[2]['kb:Clear']; d.inp(f'pentap {x} {y}'); time.sleep(0.8)
        kb_type(d, 'Work')
        d.mark_now(); d.tap('kb:Save', panel_only=True); d.expect(r'setting custom2_name = Work')
        time.sleep(1.5)
        d.tap('srv:url', panel_only=True); time.sleep(2)
        kb_type(d, f'{host}:{mock.port}/v1')
        d.mark_now(); d.tap('kb:Save', panel_only=True)
        d.expect(rf'setting custom2_url = http://{re.escape(host)}:{mock.port}/v1')
        d.expect(r'Work answers', timeout=30); time.sleep(2)
        d.mark_now(); d.tap('srv:pick:mock-vl', panel_only=True); d.expect(r'setting custom2_model = mock-vl')
        time.sleep(1.5)
        d.tap('srv:key', panel_only=True); time.sleep(2)
        kb_type(d, 'workkey123')
        d.mark_now(); d.tap('kb:Save', panel_only=True); d.expect(r'setting custom2_api_key: saved')
        d.expect(r'Work answers', timeout=30); time.sleep(2)
        if mock.requests[-1][2].get('Authorization') != 'Bearer workkey123':
            raise Fail('the second server\'s key was not sent with its model list request')
        d.shot('ai-server-2')
        d.tap('< Back', panel_only=True); time.sleep(2)
        if 'ai:custom2' not in d.hits()[2]:
            raise Fail('the second server is not among the AI services')
        d.shot('ai-services-two-servers')
        # Type up on the first server (keyless), Translate on the second (its key)
        open_feature(d, 'typeup')
        d.mark_now(); d.tap('adv:svc:custom', panel_only=True); d.expect(r'setting ai_typeup = custom')
        d.expect(r'Self-hosted \(your server\)', what='the self-hosted warning')
        time.sleep(1.5)
        d.shot('adv-typeup-server')
        open_feature(d, 'translate')
        d.mark_now(); d.tap('adv:svc:custom2', panel_only=True); d.expect(r'setting ai_translate = custom2')
        time.sleep(1.5)
        if 'adv:server-settings' not in d.hits()[2]:
            raise Fail('no server settings button for the chosen server')
        # a Type up turn over plain http to the mock: refused schema, asked again without it, answered. The turns here
        # send by themselves after the pause (early sending starts 0.8 s after the pen lifts; tapping Send on top of it
        # would drop that answer and ask again)
        new_page(d)
        set_mode(d, 'notes')
        mock.requests.clear()
        d.mark_now()
        d.inp('penstroke 1100 900 1700 900\nwait 30\npenstroke 1100 960 1700 950')
        try:
            d.expect(r"the server didn't take the reply schema", timeout=30)
        except Fail:  # what was the app doing? (a request in flight, the state, what the mock saw)
            print('DIAG curl:', ssh('pidof curl; ls -la /var/local/scribe-claude/run/', check=False).strip())
            print('DIAG state:', d.hits()[0])
            print('DIAG mock:', [(r[0], r[1]) for r in mock.requests])
            raise
        d.expect(r'turn ok .* mode notes', timeout=60); d.wait_idle()
        posts = [r for r in mock.requests if r[0] == 'POST']
        if len(posts) != 2 or 'response_format' not in posts[0][3] or 'response_format' in posts[1][3] or \
                any('Authorization' in p[2] for p in posts):
            raise Fail(f'type up requests: {[("response_format" in p[3], p[2].get("Authorization")) for p in posts]}')
        msgs = posts[1][3]['messages']
        if posts[1][3].get('model') != 'mock-vl' or 'JSON Schema' not in msgs[0]['content'] or \
                not str(msgs[-1]['content']).count('data:image/png;base64,'):
            raise Fail('the retried request: model, schema in the instructions or page image missing')
        d.shot('adv-typeup-server-reply')
        # the next one starts without the schema (one request)
        mock.requests.clear()
        d.mark_now()
        d.inp('penstroke 1100 1500 1700 1500')
        d.expect(r'turn ok .* mode notes', timeout=60); d.wait_idle()
        if len([r for r in mock.requests if r[0] == 'POST']) != 1:
            raise Fail('the server\'s answer about the schema was not remembered')
        # Translate goes to the second server with its key (it learns the reply format on its own)
        new_page(d)
        set_mode(d, 'translate')
        mock.requests.clear()
        d.mark_now()
        d.inp('penstroke 300 900 900 900')
        d.expect(r'turn ok .* mode translate', timeout=60); d.wait_idle()
        posts = [r for r in mock.requests if r[0] == 'POST']
        if len(posts) != 2 or any(p[2].get('Authorization') != 'Bearer workkey123' or p[3].get('model') != 'mock-vl' for p in posts):
            raise Fail(f'translate requests: {[(p[2].get("Authorization"), p[3].get("model")) for p in posts]}')
        # a stopped server is named
        mock.stop()
        mock = None
        set_mode(d, 'notes')
        d.mark_now()
        d.inp('penstroke 1100 1800 1700 1800')
        d.expect(r"couldn't reach Self-hosted .* is it running", timeout=40)
        set_mode(d, 'talk')
    finally:
        if mock:
            mock.stop()
        d.fake('A canned test reply.')


# ----------------------------------------------------------------------------- real AI (costs a few cents)

def t_real_ai(d):
    sys.path.insert(0, str(ROOT / 'device' / 'tools' / 'demo'))
    import run_demo
    d.no_fake()
    d.home()
    for mode, lines, expect in (('talk', ['Hello! What is your name?'], 'talk'),
                                ('math', ['12 x 14 = 158'], 'math'),
                                ('notes', ['shopping list', '- eggs', '- milk'], 'notes'),
                                ('translate', ['Good morning, how are you?'], 'translate'),
                                ('talk', ['Please draw me a cat'], 'talk')):
        d.mark_now(); menu(d, 'newpage'); d.expect(r'\(new\)')
        time.sleep(1); set_mode(d, mode)
        d.mark_now(); run_demo.write(lines)
        d.expect(rf'turn ok in [\d.]+ s, mode {expect}', timeout=90, what=f'real {mode}')
        d.wait_idle(120)
        d.shot(f'real-{expect}-{lines[0].split()[0].lower()}')


# ----------------------------------------------------------------------------- setup / teardown

def relaunch(d):
    ssh('for i in $(seq 30); do pidof scribe >/dev/null || break; sleep 1; done')
    d.mark_now()
    ssh(f'{APP}/launch.sh')
    d.expect(r'start: screen', timeout=30)
    time.sleep(2)


def restart(d):
    ssh('kill $(pidof scribe) 2>/dev/null; true')
    relaunch(d)


def migrate_usb(d):
    """A Kindle that still has the USB folder under its old name (a new build copied over SSH, not yet started): the
    app moves it when it starts, so start it once and check that it did."""
    if ssh(f'[ -d {USB} ] || [ ! -d {OLD_USB} ] || echo old', check=False).strip() != 'old':
        return
    print(f'{OLD_USB} is still there: starting the app once so it moves it to {USB}')
    ssh('kill $(pidof scribe) 2>/dev/null; while pidof scribe >/dev/null; do sleep 1; done')
    relaunch(d)
    if ssh(f'[ -d {USB} ] && [ ! -d {OLD_USB} ] && echo ok', check=False).strip() != 'ok':
        raise SystemExit(f'the app did not move {OLD_USB} to {USB}: see {LOG}')
    d.expect(rf'rename: moved {re.escape(OLD_USB)} to {re.escape(USB)}', what='the USB folder move')


def setup(d):
    tail = ssh(f'tail -n 1 {LOG}')
    print('last log line:', tail.strip())
    if ssh('grep -q devinput /var/local/scribe/bin/scribe && echo dev', check=False).strip() != 'dev':
        raise SystemExit('the installed app is a release build (no developer hooks): install a device/app/build.sh --dev build first')
    migrate_usb(d)
    # never back up test settings as if they were the owner's: refuse if an earlier run didn't clean up
    left = ssh(f'ls -d {APP}/state.e2e-keep {USB}/config.e2e-keep {SECURE}/games.e2e-keep {SECURE}/keys.e2e-keep 2>/dev/null; grep -q "^notebook = e2e" {USB}/config.txt && echo "config.txt is on the e2e notebook"', check=False).strip()
    if left:
        raise SystemExit(f'an earlier e2e run did not clean up ({left}); restore the owner config/state first')
    # keep the Kindle awake for the whole run (sleep turns Wi-Fi off); teardown sets it back before relaunching
    ssh('lipc-set-prop com.lab126.powerd preventScreenSaver 1', check=False)
    ssh(f'kill $(pidof scribe) 2>/dev/null; while pidof scribe >/dev/null; do sleep 1; done; '
        f'cp {APP}/state {APP}/state.e2e-keep; cp {USB}/config.txt {USB}/config.e2e-keep; '
        f'rm -rf {SECURE}/games.e2e-keep; [ -d {SECURE}/games ] && cp -a {SECURE}/games {SECURE}/games.e2e-keep; '
        f'cp -p {SECURE}/keys.txt {SECURE}/keys.e2e-keep; '
        f'printf "page=1\\nlast_page=1\\nmode=talk\\n" > {APP}/state; '
        f'sed -i -e "s/^notebook = .*/notebook = e2e/" -e "s/^writing_speed = .*/writing_speed = 2/" '
        f'-e "s/^pause_seconds = .*/pause_seconds = 2.5/" {USB}/config.txt; '
        f'rm -rf {SECURE}/history/e2e.json {USB}/notebooks/e2e {USB}/notebooks/e2e.md; '
        f'rm -f {SECURE}/devinput {SECURE}/devtap {SECURE}/devink {SECURE}/devdump {SECURE}/fake_response.json')
    d.fake('A canned test reply.')
    relaunch(d)


def teardown(d):
    ssh(f'kill $(pidof scribe) 2>/dev/null; while pidof scribe >/dev/null; do sleep 1; done; '
        f'[ -f {APP}/state.e2e-keep ] && mv {APP}/state.e2e-keep {APP}/state; '
        f'[ -f {USB}/config.e2e-keep ] && mv {USB}/config.e2e-keep {USB}/config.txt; '
        f'[ -d {SECURE}/games.e2e-keep ] && rm -rf {SECURE}/games && mv {SECURE}/games.e2e-keep {SECURE}/games; '
        f'[ -f {SECURE}/keys.e2e-keep ] && mv {SECURE}/keys.e2e-keep {SECURE}/keys.txt; '
        f'rm -f {SECURE}/devinput {SECURE}/devtap {SECURE}/devink {SECURE}/devdump {SECURE}/fake_response.json {SECURE}/hits.txt; '
        f'rm -rf {SECURE}/history/e2e.json {SECURE}/history/e2e-*.json {USB}/notebooks/e2e {USB}/notebooks/e2e.md {USB}/notebooks/e2e-*.md; '
        f'lipc-set-prop com.lab126.powerd preventScreenSaver 0; {APP}/launch.sh', check=False)


TESTS = [t_pen_menu_close, t_pen_bounce, t_finger_with_pen_near, t_palm_plus_finger, t_forgiving_zones, t_slide_off,
         t_taps_during_long_redraw, t_flicks, t_palm_rejected_on_panel, t_modes, t_send_and_autoreply, t_drawing,
         t_menu_while_writing_reply, t_menu_while_thinking, t_page_menu, t_translate, t_settings, t_games_local,
         t_games_picker, t_games_ai, t_levels, t_eraser_and_page_memory, t_early_send, t_clear_button, t_pen_keeps_up, t_scratch_out,
         t_typeup_in_place,
         t_wordsearch_resume, t_email, t_keyboard, t_ai_keys, t_advanced_ai, t_own_server, t_update_check, t_exit_and_power]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    tests = TESTS + ([t_real_ai] if '--real-ai' in sys.argv else []) + ([t_update_install] if '--update-install' in sys.argv else [])
    if args:
        tests = [t for t in tests if any(a in t.__name__ for a in args)]
    d = Dev()
    results = []
    try:
        setup(d)  # inside the try: a setup that fails half way still gets its teardown
        for t in tests:
            t0 = time.time()
            try:
                t(d)
                results.append((t.__name__, 'PASS', ''))
                print(f'PASS {t.__name__} ({time.time() - t0:.0f}s)', flush=True)
            except Exception as e:
                msg = str(e) if isinstance(e, (Fail, AssertionError)) else traceback.format_exc()
                results.append((t.__name__, 'FAIL', msg))
                print(f'FAIL {t.__name__}: {msg}', flush=True)
                try:
                    d.shot('FAIL-' + t.__name__)
                    d.home()
                except Exception:
                    pass
                if '--keep-going' not in sys.argv:
                    break
    finally:
        teardown(d)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'results.json').write_text(json.dumps(results, indent=1))
    npass = sum(r[1] == 'PASS' for r in results)
    print(f'\n{npass}/{len(results)} passed')
    sys.exit(0 if npass == len(results) == len(tests) else 1)


if __name__ == '__main__':
    main()
