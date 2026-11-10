/* Inkling (formerly Scribe), the device app: write by hand on the Kindle Scribe; an AI answers on the page.
 * Standalone: calls the AI service's API directly (Claude, OpenAI, Gemini, Bedrock, Grok, Mistral or OpenRouter, with
 * the user's own key, or the user's own server). See docs/plan.md (v2). */
#ifndef SCRIBE_H
#define SCRIBE_H

#include <stddef.h>
#include <stdint.h>

#include "display.h"

/* User-visible folder on the Kindle's USB storage: config.txt, notebooks/ (Markdown exports), README. Until 0.2.x the
 * app was called Scribe and this was OLD_USB_DIR; main() moves it once (upd_migrate_usb_dir). */
#ifndef USB_DIR /* (the host tests point both somewhere else) */
#define USB_DIR "/mnt/us/inkling"
#endif
#define OLD_USB_DIR "/mnt/us/scribe-claude"
#define DOCS_DIR "/mnt/us/documents" /* the Library entry: inkling.sh */
/* Root-only state: keys, history, temp request files (kept under the old name: installs keep their keys). */
#ifndef SECURE_DIR
#define SECURE_DIR "/var/local/scribe-claude"
#endif
#define APP_DIR "/var/local/scribe/app"
#define BIN_DIR "/var/local/scribe/bin"

#define PEN_DEV "/dev/input/event3"
#define TOUCH_DEV "/dev/input/event4"
#define POWER_DEV "/dev/input/event0"
#define PEN_MAX_X 15624
#define PEN_MAX_Y 20832
#define FONT_DIR "/usr/java/lib/fonts/"

#define BAR_H 170 /* top bar: buttons + status line; the canvas is everything below */
#define MAX_MODES 8
#define ERASER_R 24

enum { PROV_NONE = 0, PROV_ANTHROPIC, PROV_OPENAI, PROV_GEMINI, PROV_BEDROCK, PROV_XAI, PROV_MISTRAL, PROV_OPENROUTER,
       PROV_CUSTOM, /* the user's own server (providers.c: provider_custom_apply) */
       PROV_COUNT };

/* providers.c: the AI services (bring your own key), one row each; settings, config.txt and brain.c read it. */
enum { API_ANTHROPIC, API_OPENAI_RESPONSES, API_GEMINI, API_BEDROCK, API_CHAT /* OpenAI-style Chat Completions */ };
#define PROVIDER_MAX_MODELS 6
#define PROVIDER_MAX_REGIONS 12
typedef struct {
	const char *id;    /* sent to the API ("gemini-3.8-flash") or stored in config ("us-east-1") */
	const char *label; /* short, for a chip ("3.8 Flash") */
	int tier;          /* models: 1 small and quick, 2 middle, 3 the most capable and slowest (for Advanced AI's notes) */
} ProviderChoice;
typedef struct {
	const char *id;            /* "anthropic", "openai", "gemini", "bedrock", "xai", "mistral", "openrouter" */
	int prov;                  /* PROV_* (App.provider) */
	const char *label;         /* for the UI: "Gemini (Google)" */
	const char *short_name;    /* in status lines: "Gemini" */
	const char *key_name;      /* in the key store: "gemini_api_key" (also its config.txt line) */
	const char *key_file;      /* key-drop file in USB_DIR: "gemini-key.txt" */
	const char *key_where;     /* where to get a key, one short line: "aistudio.google.com" */
	const char *model_key;     /* config.txt key: "gemini_model" */
	const char *default_model; /* models[] id ("fast" = Claude Haiku, Sonnet for maths) */
	int nmodels;
	ProviderChoice models[PROVIDER_MAX_MODELS];
	int needs_region;          /* 1 for Bedrock: config bedrock_region, one of regions[] (or any region id) */
	const char *default_region;
	int nregions;
	ProviderChoice regions[PROVIDER_MAX_REGIONS];
	int api;                   /* API_*: how brain.c talks to it */
	const char *base_url;
	int custom;                /* 1 for a self-hosted / own server (PROV_CUSTOM): address, API and model from config */
	int slot;                  /* self-hosted servers: 0 .. NSERVERS-1 */
} Provider;
/* A self-hosted server (Settings > AI): slot 0 = config custom_*, slot 1 = custom2_*, slot 2 = custom3_*. */
#define NSERVERS 3
typedef struct {
	char name[32];  /* custom_name: shown in the UI ("Self-hosted", "Home Ollama") */
	char url[200];  /* custom_url: the API's base address, http(s)://host[:port]/v1 ("" = not set up) */
	char api[12];   /* custom_api: openai (Chat Completions), anthropic (Messages), responses (OpenAI Responses) */
	char model[64]; /* custom_model: the model it runs */
	char json[8];   /* custom_json: auto, schema, json or prompt (how replies are held to Inkling's JSON) */
	int timeout;    /* custom_timeout: seconds to wait for an answer (30-600) */
} CustomServer;
const CustomServer *provider_custom(void);         /* the first one (slot 0) */
const CustomServer *provider_custom_slot(int slot);
const Provider *provider_slot(int slot);           /* its Provider (id "custom", "custom2", "custom3"), NULL outside */
/* A custom[N]_<field> setting: returns its slot (bad values are logged and ignored), or -1 if key isn't one;
 * *field (optional) gets the field: url, api, model, name, json, timeout. */
int provider_custom_apply(const char *key, const char *value, const char **field);
int provider_custom_ready(void);                   /* slot 0 has an address and a model */
int provider_custom_ready_slot(int slot);
int providers_count(void);
const Provider *provider_at(int i);            /* 0 .. providers_count()-1, NULL outside */
const Provider *provider_find(const char *id); /* NULL if unknown */
const Provider *provider_by_prov(int prov);    /* NULL for PROV_NONE */
const char *provider_model_label(const Provider *p, const char *model); /* NULL if not in the table */

/* The AI features. Each can use its own service, model and thinking level (Settings > Advanced AI; config.txt
 * ai_<id>, ai_<id>_model, ai_<id>_effort); "" in App.feat_* = Inkling's default for that feature. */
enum { FEAT_CHAT, FEAT_TYPEUP, FEAT_MATH, FEAT_TRANSLATE, FEAT_GAMES, FEAT_DRAW, FEAT_EMAIL, FEAT_COUNT };
typedef struct {
	const char *id;    /* config: "chat", "typeup", "math", "translate", "games", "drawing", "email" */
	const char *label; /* "Chat" */
	const char *what;  /* one line: what the AI does for it */
	int image;         /* 1: the request carries a picture of the page (the model must read images) */
} Feature;
const Feature *feature_at(int f); /* NULL outside 0 .. FEAT_COUNT-1 */
/* The writer's own instructions, added to Inkling's prompt (ai_instructions for every feature, then
 * ai_<feature>_instructions): one line each in config.txt, at most this many bytes. */
#define AI_INSTR_MAX 360

typedef struct {
	int16_t x, y;
	uint16_t p;
} Pt;

typedef struct {
	int tool; /* 0 pen, 1 eraser */
	int n, cap;
	Pt *pts;
} Stroke;

typedef struct {
	int x0, y0, x1, y1; /* inclusive; empty when x1 < x0 */
} Dirty;

typedef struct {
	char text[64];
	int x, y, w, h;
	int kind; /* 0 mode, 1 send, 2 new page, 3 exit */
	int arg;
} Button;

typedef enum { OP_STATUS, OP_MODE, OP_NEWPAGE, OP_CLEAR, OP_STROKE, OP_TEXT, OP_RULE, OP_FILL, OP_DISC, OP_PAUSE, OP_REVEAL } OpType;

typedef struct {
	OpType t;
	int a, b, c, d;   /* CLEAR/TEXT/RULE rect x,y,w,h; NEWPAGE page; STROKE width */
	int size, style;  /* TEXT: px size, font style (TXT_REGULAR/BOLD/...), formatted flag in bit 8 */
	char *s;          /* STATUS / MODE / TEXT */
	int n;            /* stroke points */
	int16_t *xy;      /* 2n */
	uint16_t *dt;     /* n */
} Op;

typedef struct {
	char transcript[4096];
	char mode[24];
	char style[16]; /* "script" | "typeset" */
	char *reply;    /* malloc'd */
	char memo[1024]; /* the model's private notes for its next turn (games), never drawn */
	char illustrate[480]; /* a picture the writer asked for, described for the drawing pass ("" = none) */
	int nstrokes;    /* a drawing (from the drawing pass): strokes of points on a 1000x1000 grid */
	int *slen;
	float *dpts;
} Reply;

typedef struct {
	/* config (USB_DIR/config.txt, keys in SECURE_DIR/keys.txt) */
	int provider;          /* PROV_*: the one answering now */
	char provider_cfg[16]; /* config "provider": auto or a provider id */
	char model[PROV_COUNT][64]; /* config "<provider>_model" per PROV_* ("" = the table's default) */
	char bedrock_region[24];
	char feat_service[FEAT_COUNT][16]; /* ai_<feature>: "" = the main service, else a provider id */
	char feat_model[FEAT_COUNT][64];   /* ai_<feature>_model: "" = the recommended model for the feature */
	char feat_effort[FEAT_COUNT][8];   /* ai_<feature>_effort: "" = the tuned level, else low/medium/high */
	char feat_instr[FEAT_COUNT][AI_INSTR_MAX + 1]; /* ai_<feature>_instructions: added to its prompt ("" none) */
	char feat_temp[FEAT_COUNT][6];     /* ai_<feature>_temperature: "" = the model's own, else "0" .. "2" */
	int feat_maxtok[FEAT_COUNT];       /* ai_<feature>_max_tokens: 0 = Inkling's (16000), else 256 .. 64000 */
	char all_instr[AI_INSTR_MAX + 1];  /* ai_instructions: added to every feature's prompt, before its own */
	int ai_advanced_ok;                /* the Advanced AI warning was read (ai_advanced = understood) */
	char notebook[48];
	double anim_speed;
	int pause_ms; /* auto-send after this long without pen contact (0 = Send button only) */
	char voice[16], length[8], math_help[8], language[12], hsize[8], ink[8]; /* settings page */
	char resend_key[160], email_to[160], email_from[160]; /* Send > Email this page */
	char email_provider[16], email_address[160], smtp_server[96]; /* gmail|outlook|icloud|yahoo|fastmail|custom|resend */
	int smtp_port;
	int typeup_below;  /* Type up keeps the handwriting and puts the text under it (default: replaces it) */
	int scratch_erase; /* scribbling back and forth over ink erases it */
	int erase_mode;    /* the Erase button: the pen erases */
	int clear_armed;   /* Clear was tapped once; a second tap within 4 s wipes the page */
	double clear_armed_t;
	int script_cap; /* handwriting cap height, px */
	int in_settings, settings_page;
	int in_games;            /* 0 notebook, 1 games menu, 2 playing a local game */
	int in_pagemenu;
	int in_menu;
	int in_modes; /* the mode picker is dropped down */
	int pressed; /* bar button held down (-1 none) */
	const char *game_prompt; /* AI game in progress (a conversation with this prompt), or NULL */
	int hfont;         /* index into HFONTS (handwriting font) */
	char tfont[16];    /* typed font family id */
	char translate_to[24];
	int translate_typed; /* 0 = translations are written out (default), 1 = typed */
	DispFonts font_reply, font_cjk[3]; /* typed replies: the chosen family; Japanese, Chinese, Korean (on demand) */
	int reply_fonts_loaded, cjk_loaded[3];

	/* display */
	Display disp;
	int W, H, stride, pan_y;
	DispRefresh fast, gc16, gl16, flash;
	DispFonts font_ui; /* the UI's font (Amazon Ember) */
	unsigned char *fb, *saved;
	size_t fbsize;
	int cx, cy, cw, ch; /* canvas */
	int ink_bottom;     /* lowest drawn pixel on the canvas (user ink + replies) */

	/* input */
	int pen_fd, touch_fd, power_fd;
	int px, py, pp, touching, rubber, pen_near, have_last, lx, ly, pen_in_bar;
	double pen_t;            /* time of the last pen event (pen_near goes stale if the pen stops reporting) */
	int pen_x0, pen_y0;      /* where the pen went down (taps act on the press point) */
	double ev_t;             /* time stamp of the input event being handled */
	double stroke_lag;       /* how far (ms) drawing fell behind the pen during the current stroke */
	double last_act_t;       /* last top-bar button activation, against a bouncing pen tip */
	int last_act_btn;
	Stroke cur;
	int cur_active;
	Stroke *pending;
	int npending, cappending;
	Dirty dirty;

	/* modes + bar */
	char mode_id[MAX_MODES][24], mode_label[MAX_MODES][40];
	int autosend_ms[MAX_MODES];
	int nmodes, mode;
	Button buttons[16];
	int nbuttons;
	char status[160];
	double autosend_at;

	/* session */
	int page, last_page;

	/* reply playback */
	Op *ops;
	int nops, opi, seg;
	double seg_t;

	int quit, quit_power, quit_sleep; /* quit_sleep: the Kindle is going to sleep (exit code 3: reopen after unlock) */
	int page_dirty;                  /* the page has ink that isn't in its saved PNG yet */
} App;

/* ink.c */
void dirty_add(Dirty *d, int l, int t, int r, int b);
void stamp(App *a, int x, int y, int r, uint8_t gray, Dirty *d);
void stamp_line(App *a, int x0, int y0, int x1, int y1, int r, uint8_t gray, Dirty *d);
void flush_dirty(App *a, Dirty *d, const DispRefresh *how);
void stroke_add(Stroke *s, int x, int y, int p);
void pending_push(App *a, Stroke *s);
void pending_clear(App *a);
void clear_canvas(App *a);

/* ui.c */
void ui_layout(App *a);
void ui_draw_bar(App *a);
void ui_draw_status(App *a);
void ui_set_status(App *a, const char *fmt, ...);
int ui_hit(App *a, int x, int y);
extern int ui_redraws;
void ui_rrect(App *a, int x, int y, int w, int h, int r, uint8_t g);
void ui_icon(App *a, int kind, int x, int y, int s, uint8_t g);
int ui_mode_icon(const char *mode_id);
void modes_open(App *a);
void modes_close(App *a);
int modes_tap(App *a, int x, int y); /* 1 = a mode row was tapped (and chosen) */
void app_set_mode(App *a, int k);
void app_open_games(App *a);
void hits_reset(void);
void hits_note(const char *label, int x, int y, int w, int h);
void hits_dump(App *a, const char *path); /* developer hook: dev builds only (INKLING_DEV) */
void ui_draw_button(App *a, int i);
void ui_press(App *a, int i);
int text_measure(App *a, const char *s, int w, int px, int style, int *lines);
void text_draw(App *a, const char *s, int x, int y, int w, int h, int px, int style);

/* net.c: the network worker thread (one job at a time) */
typedef int (*JobFn)(void *arg, char **body, size_t *len);
void net_start(void);
int net_submit_lane(int lane, JobFn fn, void *arg, int kind); /* lane 1: drawings, beside lane 0 (net_submit_fn) */
int net_lane_busy(int lane);
int net_pending(void);
int net_busy(void);
int net_submit_fn(JobFn fn, void *arg, int kind);
int net_result_fd(void);
int net_take_result(int *kind, int *http_status, char **body, size_t *len);

/* reply.c */
Op *op_new(App *a, OpType t);
int reply_step(App *a, double now);
void reply_finish(App *a);
void reply_free(App *a);

/* brain.c: direct API turns */
typedef struct AiJob AiJob;
void brain_load_config(App *a);
const char *brain_provider_name(App *a);
/* Provider settings (main thread). brain_config_apply takes "provider", "<provider>_model" and "bedrock_region"
 * (returns 1 if it was one of them; the caller saves it with config_set). brain_keys_changed re-reads the key store
 * and picks the provider again (call it after key_set). */
int brain_config_apply(App *a, const char *key, const char *value);
void brain_keys_changed(App *a);
const char *brain_model(App *a, const Provider *p); /* the model in use for p (its default if unset) */
int brain_has_key(const Provider *p);
int brain_usable(const Provider *p); /* can answer: a key (built-in services) or an address + model (own server) */
/* What a feature will use: the service, model and thinking level after defaults and fallbacks. custom = 1 when any of
 * them comes from Advanced AI rather than Inkling's defaults; fallback = 1 when the feature's chosen service can't be
 * used (no key / not set up) and the main one answers instead. */
typedef struct {
	const Provider *p; /* NULL: no service at all */
	char model[64];
	char effort[8];    /* "low", "medium", "high"; "" = the service takes none */
	int custom, fallback, effort_set;
	int instr;         /* the writer's instructions apply (for every feature and/or this one) */
	int temp_set;      /* a temperature is sent (temperature) */
	double temperature;
	int max_out;       /* output token limit sent */
} Route;
void brain_route(App *a, int feat, Route *r);
int brain_feature_custom(App *a, int feat); /* any ai_<feature> setting (instructions too) differs from the default */
const char *brain_default_effort(int feat); /* the thinking level Inkling tunes the feature to ("low", "medium") */
/* Plain-language notes on what a feature's current settings mean for speed, accuracy, cost and whether it works (the
 * Advanced AI page). Lines separated by '\n'; "" when everything is at Inkling's defaults. */
void brain_route_notes(App *a, int feat, char *out, size_t n);
/* Models a self-hosted server reported at its last test (GET <url>/models), for picking one without typing. */
int brain_server_models(int slot, const char ***ids);
/* "Test key": the cheapest authenticated call to the provider (free listing calls; nothing is generated).
 * Blocking version: runs curl on the calling thread (up to ~20 s); returns 1 key works, 0 rejected or failed,
 * -1 busy (an AI request is running). msg gets one line for the status bar. key = NULL or "" tests the saved key.
 * Worker version: brain_key_check_prepare (main) -> net_submit_fn(brain_key_check_run, job, kind) ->
 * brain_key_check_result (main, with the status/body from net_take_result) -> brain_key_check_free. */
typedef struct KeyCheck KeyCheck;
int brain_key_check(App *a, const char *provider, const char *key, char *msg, size_t n);
KeyCheck *brain_key_check_prepare(App *a, const char *provider, const char *key); /* NULL: unknown provider/no key */
int brain_key_check_run(void *job, char **body, size_t *len); /* JobFn */
int brain_key_check_result(KeyCheck *job, int status, const char *body, size_t len, char *msg, size_t n);
void brain_key_check_free(KeyCheck *job);
/* keys.c: keys and passwords in SECURE_DIR/keys.txt (name=value) */
/* keyboard.c: on-screen keyboard inside Settings */
int kb_active(void);
void kb_open(App *a, const char *title, const char *hint, const char *key, const char *initial, int secret,
	     void (*done)(App *a, const char *key, const char *text)); /* text NULL = cancelled */
void kb_draw(App *a);
void kb_tap(App *a, int x, int y);
void kb_cancel(App *a);
void keys_load(void);
void keys_save(void);
const char *key_get(const char *name);         /* "" when not set */
void key_set(const char *name, const char *value); /* "" removes; saves the file */
void key_masked(const char *name, char *out, size_t n); /* "sk-ant-...4ZtA" */
AiJob *brain_prepare(App *a);
AiJob *brain_prepare_email(App *a);           /* NULL when email isn't set up */
const char *brain_email_missing(App *a);       /* what email still needs ("set up email first: ..."), or NULL */
const char *brain_email_to(App *a);           /* the recipient: email_to, else email_address */
int brain_email_run(void *job, char **body, size_t *len);
/* Drawings: a turn's reply that asks for a picture (Reply.illustrate) starts a drawing pass in net lane 1 with the
 * provider's strongest model (own = 1: the user's chosen model, the retry after a failure). */
AiJob *brain_prepare_draw(App *a, const char *desc, int own);
int brain_draw_run(void *job, char **body, size_t *len); /* JobFn */
int brain_parse_draw(AiJob *j, int status, const char *body, size_t len, Reply *out);
int brain_draw_is_own(AiJob *j);
void brain_cancel_draw(void); /* stops the drawing in flight (its answer comes back as an error) */
int brain_draw_fail_msg(AiJob *j, int status, char *msg, size_t n);
/* After a reply: the feature's label if it ran on Advanced AI settings (for a hint when it was slow), else NULL. */
const char *brain_job_custom_feature(AiJob *j);
void brain_cancel(void); /* stop the request in flight */
int brain_run(void *job, char **body, size_t *len); /* JobFn, runs in the worker */
int brain_parse(App *a, AiJob *job, int status, const char *body, size_t len, Reply *out);
void brain_commit(App *a, AiJob *job, Reply *r);
void brain_job_free(AiJob *job);
void brain_new_page(App *a);
void brain_forget(App *a, int archive);

/* update.c: Menu > Check for updates (signed GitHub releases; details in update.h) */
#define UPDATE_NET_KIND 40  /* the net_submit_fn kind of update jobs: handle_result hands it to update_result */
#define UPDATE_EXIT_CODE 4  /* main's exit code once an update is installed: launch.sh starts the new version */
void update_tap(App *a);                            /* first tap checks, second installs */
void update_result(App *a, int status, char *body); /* from handle_result (body stays the caller's) */
void update_tick(App *a);                           /* main loop: download progress in the status line */
const char *update_row_desc(App *a);                /* the Menu row's one-line description */
int update_restart_pending(void);                   /* 1 once installed: exit with UPDATE_EXIT_CODE */

/* pagemenu.c */
void pagemenu_open(App *a);
void pagemenu_close(App *a);
void pagemenu_tap(App *a, int x, int y);
void app_request_turn(App *a, int dir);
void app_action(App *a, int kind);
void app_explain_mode(App *a);

/* menu.c */
void menu_open(App *a);
void menu_close(App *a);
int menu_tap(App *a, int x, int y);

/* pages.c */
void page_save(App *a);
void page_load(App *a, int n);
int page_is_blank(App *a);
int page_ink_bottom(App *a);
void page_autosave(App *a);
void page_save_wait(void); /* before deleting or reloading page files */
void page_clear(App *a);
void page_delete(App *a);
void conversation_forget(App *a);
void notebook_start_over(App *a);
void clearpick_open(App *a, int right);
void menupick_open(App *a, int right);
void sendpick_open(App *a, int right);
void clearpick_tick(App *a, double now);
void app_dropdown_action(App *a, int kind, const char *id); /* main.c: a drop-down row was chosen */
void app_key_check(App *a, const char *provider);           /* main.c: test a key in the background */

/* settings.c */
int settings_apply(App *a, const char *k, const char *v);
void settings_open(App *a);
void settings_close(App *a);
void settings_tap(App *a, int x, int y);
void settings_flip(App *a, int dir);
void settings_refresh(App *a); /* redraw the open settings page (a server's model list arrived) */

/* fonts.c */
void reply_fonts_load(App *a);
const DispFonts *font_for(App *a, const char *s, int style);
void fonts_free(App *a);
void config_set(const char *key, const char *value);

/* games.c */
void games_open_menu(App *a);
void games_close(App *a);
void games_menu_tap(App *a, int x, int y);
void games_stroke(App *a, const Stroke *s, int finger);
int games_wants_ink(App *a);
void games_tick(App *a, double now);
void games_exit(App *a); /* app exit: keeps an unfinished game (games.h resume) */

/* layout.c */
void layout_reply(App *a, Reply *r);
void layout_picture(App *a, Reply *r); /* a drawing (r->nstrokes) below the ink */

double mono_ms(void);

/* input.c: a thread reads the pen, touch screen and power key into a queue so no event is lost while drawing */
enum { SRC_PEN, SRC_TOUCH, SRC_POWER };
#define MT_SLOTS 2
typedef struct {
	unsigned char src;
	unsigned short type, code;
	int value;
	double t; /* mono_ms() when it was read */
} InEv;
int input_start(int pen_fd, int touch_fd, int power_fd);
void input_stop(void);
int input_fd(void);          /* readable when events are queued */
void input_drain_note(void);
int input_next(InEv *e);     /* 1 = popped an event */
void input_take_drops(int *kernel, int *queue);
void input_script_start(const char *path, int w, int h); /* developer hook: scripted input (INKLING_DEV only) */
int input_take_stall(void);                              /* (INKLING_DEV only) */
void logf_(const char *fmt, ...);
void state_save(App *a);

#endif
