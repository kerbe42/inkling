/* Direct API turns: the Kindle itself calls the AI service the user chose, with their own key (providers.c: Claude,
 * OpenAI, Gemini, Amazon Bedrock, Grok, Mistral, OpenRouter).
 * Main thread: brain_prepare (snapshot the page) -> worker: brain_run (PNG, request JSON, curl) -> main thread:
 * brain_parse + brain_commit (history, Markdown export). Keys live in the key store (keys.c: SECURE_DIR/keys.txt,
 * root 600) and reach curl through a root-only header file, never the command line. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "email.h"
#include "scribe.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

void prompt_system(const char *mode, const char *voice, const char *length, const char *math, const char *lang,
		   const char *target, const char *game, char *buf, size_t n);
const char *prompt_effort(const char *mode);

#define RUN_DIR SECURE_DIR "/run"
#ifndef CURL /* (the host tests use a stand-in) */
#define CURL "/usr/bin/curl"
#endif
#define CA_BUNDLE "/etc/ssl/certs/ca-certificates.crt"
#define HIST_MAX 24
#define ANTHROPIC_VERSION "2026-10-21"
#define MAX_OUT 16000 /* output token cap per reply (Advanced AI can change it per feature) */

struct AiJob {
	int provider; /* PROV_* */
	int slot;     /* PROV_CUSTOM: which self-hosted server (0 .. NSERVERS-1) */
	int api;      /* API_*, and the base address: copied from the provider when the job is made (the own server's
		       * settings can change while a request is in flight) */
	char base_url[200];
	int feat;                 /* FEAT_* */
	int customized;           /* the feature runs on Advanced AI settings, not Inkling's defaults */
	char effort[8];           /* thinking level: low/medium/high, or off (self-hosted servers only) */
	int effort_set;           /* chosen in Advanced AI: also sent to services that otherwise get none */
	int json_mode;            /* 2 = JSON schema, 1 = JSON object, 0 = the schema in the instructions only */
	int json_auto;            /* own server, custom_json auto: drop to 0 if the server rejects the schema */
	int timeout;              /* seconds */
	int temp_set;             /* send temperature (Advanced AI) */
	double temperature;
	int max_out;              /* output token limit */
	char instr[2 * AI_INSTR_MAX + 4]; /* the writer's instructions (every feature, then this one), "" none */
	char model[64], key[320], mode[24], region[24];
	char voice[16], length[8], math[8], lang[12], target[24];
	char game[1600];
	unsigned char *px;
	int w, h;
	int bx0, by0, bx1, by1; /* newest ink, image pixels; bx1 < bx0 if none */
	int page;
	char *history; /* JSON array of {"role","text"} */
	double started;
	/* email: provider, custom server/port, the account and its app password (or the Resend key), recipient, From */
	char eprov[16], ehost[96], euser[160], epass[320], rkey[160], to[160], from[160];
	int eport;
	char draw[480]; /* drawing pass: the picture to draw */
	int draw_own;   /* drawing pass: already the user's own model (no retry) */
};

static cJSON *hist; /* main thread only */

static const char SCHEMA[] =
	"{\"type\":\"object\",\"properties\":{\"read\":{\"type\":\"string\"},\"mode\":{\"type\":\"string\",\"enum\":"
	"[\"talk\",\"notes\",\"math\",\"translate\"]},\"style\":{\"type\":\"string\",\"enum\":[\"script\",\"typeset\"]},\"reply\":"
	"{\"type\":\"string\"},\"memo\":{\"type\":\"string\"},\"illustrate\":{\"type\":\"string\"}},\"required\":[\"read\",\"mode\",\"style\",\"reply\",\"memo\",\"illustrate\"],\"additionalProperties\":false}";

/* The drawing pass: pen strokes on a 1000 x 1000 canvas */
static const char DRAW_SCHEMA[] =
	"{\"type\":\"object\",\"properties\":{\"strokes\":{\"type\":\"array\",\"items\":{\"type\":\"array\",\"items\":{\"type\":"
	"\"array\",\"items\":{\"type\":\"number\"}}}}},\"required\":[\"strokes\"],\"additionalProperties\":false}";
#define DRAW_MAX_STROKES 160
#define DRAW_MAX_POINTS 120

static const char EMAIL_SCHEMA[] =
	"{\"type\":\"object\",\"properties\":{\"subject\":{\"type\":\"string\"},\"body\":{\"type\":\"string\"}},"
	"\"required\":[\"subject\",\"body\"],\"additionalProperties\":false}";

/* config.txt for a new install: CONFIG_HEAD, one block per provider (from providers.c), then CONFIG_TEMPLATE */
static const char CONFIG_HEAD[] =
	"# Inkling settings. Edit this file over USB, then reopen the app.\n"
	"#\n"
	"# Your API key: the easiest way is to make a text file that contains only the key and copy it into this\n"
	"# inkling folder, named for the AI service: anthropic-key.txt (Claude), openai-key.txt (ChatGPT),\n"
	"# gemini-key.txt (Google Gemini), bedrock-key.txt (an Amazon Bedrock API key), xai-key.txt (Grok),\n"
	"# mistral-key.txt or openrouter-key.txt. The app moves the key into protected storage on the Kindle and deletes\n"
	"# the file. You can also paste the key on a key line below (edit this file on your computer and copy it back); it\n"
	"# is then replaced here with \"saved-on-device\". To remove a key, write \"delete\" on its line.\n"
	"#\n"
	"# Which AI to use: auto (the first service below that has a key), or one of anthropic, openai, gemini, bedrock,\n"
	"# xai, mistral, openrouter, custom (your own server, further down). Each <service>_model line picks its model\n"
	"# (the README lists them).\n"
	"provider = auto\n\n";

/* config.txt for a new install: the own server and Advanced AI, after the services */
static const char CONFIG_ADVANCED[] =
	"# ADVANCED. Inkling's defaults are chosen and tested for speed and accuracy on the Kindle. The settings below can\n"
	"# make replies slower, less accurate or more expensive, or stop a feature working. Settings > AI on the\n"
	"# Kindle has the same settings, with notes on what each choice means.\n"
	"#\n"
	"# A self-hosted AI server: any server with an OpenAI-compatible API (Ollama, LM Studio, llama.cpp, vLLM, LiteLLM, a\n"
	"# company gateway), or one that speaks Anthropic's Messages API or OpenAI's Responses API. Its model must read\n"
	"# images to read your handwriting. http:// addresses are not encrypted: use them only on your own network.\n"
	"#   custom_url: the API's base address, e.g. http://10.0.0.20:11434/v1 (Ollama) or https://example.com/v1\n"
	"#   custom_api: openai (Chat Completions, most servers), anthropic (Messages) or responses (OpenAI Responses)\n"
	"#   custom_api_key: only if the server needs one (or a file custom-key.txt in this folder)\n"
	"#   custom_json: auto, schema, json or prompt - how replies are held to Inkling's format (auto finds out)\n"
	"#   custom_timeout: seconds to wait for an answer, 30 to 600\n"
	"#   custom_name: what it's called on the Kindle\n"
	"# Two more servers: the same settings as custom2_* and custom3_*. A server can be a cloud service's own API too,\n"
	"# e.g. https://api.openai.com/v1 with a key used for nothing else.\n"
	"custom_name = Self-hosted\n"
	"custom_url =\n"
	"custom_api = openai\n"
	"custom_api_key =\n"
	"custom_model =\n"
	"custom_json = auto\n"
	"custom_timeout = 170\n\n"
	"# A different AI for one feature (also Settings > AI > Each feature). Features: chat, typeup, math, translate, games,\n"
	"# drawing, email.\n"
	"#   ai_<feature> = default, or a service: anthropic, openai, gemini, bedrock, xai, mistral, openrouter, or a\n"
	"#     self-hosted server: custom, custom2, custom3\n"
	"#   ai_<feature>_model = default, or a model of that service\n"
	"#   ai_<feature>_effort = default, low, medium or high (how much the model thinks first); off (no thinking: the\n"
	"#     quickest replies) for a self-hosted server\n"
	"#   ai_<feature>_temperature = default, or 0 (most predictable) to 2 (most varied)\n"
	"#   ai_<feature>_max_tokens = default (16000), or the longest reply in tokens (256 to 64000)\n"
	"#   ai_<feature>_instructions = your own instructions, added to Inkling's for that feature (one line, up to\n"
	"#     360 characters); ai_instructions = the same for every feature. They can't change the page layout.\n"
	"# For example: ai_math = openai, ai_math_model = gpt-6-astra, ai_math_effort = high,\n"
	"#   ai_translate_instructions = Use British spelling and explain any idiom.\n\n";

static const char CONFIG_TEMPLATE[] =
	"# Email a page to yourself (Send > Email this page) through your own email account. Easiest on the Kindle:\n"
	"# Settings > Email. The password is an app password made for Inkling (see the README), typed there; it is kept in\n"
	"# protected storage, never in this file.\n"
	"# email_provider: gmail, outlook (also Hotmail/Live), icloud, yahoo, fastmail, custom (any other mail server:\n"
	"#   set smtp_server and smtp_port; 465 = SSL/TLS, anything else = STARTTLS, e.g. 587), or resend (resend.com).\n"
	"# email_address: your account; pages go there unless email_to names another address.\n"
	"email_provider = gmail\n"
	"email_address =\n"
	"email_to =\n"
	"smtp_server =\n"
	"smtp_port =\n"
	"# resend only: the key from resend.com (or a file resend-key.txt in this folder); email_from needs a domain you\n"
	"# verified at Resend (leave it empty otherwise).\n"
	"resend_api_key =\n"
	"email_from =\n\n"
	"# Transcripts and replies are saved to notebooks/<notebook>.md in this folder.\n"
	"notebook = default\n\n"
	"# Speed of the handwritten replies: 1 = normal, 2 = twice as fast.\n"
	"writing_speed = 1\n\n"
	"# Seconds of pause after you lift the pen before the AI answers (0 = only the Send button).\n"
	"pause_seconds = 1.5\n\n"
	"# Reply style (also on the Kindle: the Settings button).\n"
	"# reply_voice: friendly, mentor, playful, poetic, mysterious, concise\n"
	"reply_voice = friendly\n"
	"# reply_length: short, medium, long    math_help: hints, full    reply_language: match, english\n"
	"reply_length = medium\n"
	"math_help = hints\n"
	"# typeup: replace (Type up turns your handwriting into the text) or below (keeps it, text underneath)\n"
	"typeup = replace\n"
	"# scratch_erase: on or off (scribble back and forth over writing to erase it)\n"
	"scratch_erase = on\n"
	"reply_language = match\n"
	"# handwriting_size: small, medium, large    ink (your pen): thin, normal, thick\n"
	"handwriting_size = medium\n"
	"ink = normal\n"
	"# handwriting_font: script, elegant, cursive, copperplate, flowing, signature, retro, swirly, brush, modern,\n"
	"#   print, boldprint, serif, clean, rounded, marker, architect, playful, typewriter, italic, modernitalic,\n"
	"#   calligraphic, chancery, finescript, gothic, rotunda, storybook, fairytale, deco, scifi\n"
	"handwriting_font = script\n"
	"# typed_font: bookerly, sans, palatino, baskerville, futura, dyslexic, slab, classicsans, modernserif, display\n"
	"typed_font = bookerly\n"
	"# translate_to (Translate mode): any language name, e.g. English, Spanish, French, Japanese\n"
	"translate_to = English\n";

static void trim(char *s) {
	char *p = s;
	while (*p == ' ' || *p == '\t') p++;
	memmove(s, p, strlen(p) + 1);
	size_t n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}

static int write_file(const char *path, const char *data, size_t len, mode_t mode) {
	char tmp[256];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0) return -1;
	ssize_t w = write(fd, data, len);
	fsync(fd);
	close(fd);
	if (w != (ssize_t)len) { unlink(tmp); return -1; }
	return rename(tmp, path);
}

#define MAX_FILE (16L << 20) /* the most read_file takes (curl stops AI responses at MAX_RESP) */
#define MAX_RESP "8000000"

static char *read_file(const char *path, size_t *len) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0 || n > MAX_FILE) { fclose(f); return NULL; }
	char *b = malloc((size_t)n + 1);
	if (!b) { fclose(f); return NULL; }
	size_t r = n > 0 ? fread(b, 1, (size_t)n, f) : 0;
	fclose(f);
	b[r] = 0;
	if (len) *len = r;
	return b;
}

static void history_path(App *a, char *p, size_t n) { snprintf(p, n, SECURE_DIR "/history/%s.json", a->notebook); }

/* Model and region names end up in URLs and request headers: letters, digits and . _ : / - only. */
static int safe_name(const char *v, int model) {
	size_t n = strlen(v);
	if (!n || n > 62) return 0;
	for (const char *c = v; *c; c++)
		if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' ||
		      (model && ((*c >= 'A' && *c <= 'Z') || *c == '.' || *c == '_' || *c == ':' || *c == '/'))))
			return 0;
	return 1;
}

int brain_has_key(const Provider *p) { return p && key_get(p->key_name)[0]; }

/* Built-in services need a key; the own server needs an address and a model (a key only if it asks for one). */
int brain_usable(const Provider *p) { return p && (p->custom ? provider_custom_ready_slot(p->slot) : brain_has_key(p)); }

const char *brain_model(App *a, const Provider *p) {
	if (!p) return "";
	return a->model[p->prov][0] ? a->model[p->prov] : p->default_model;
}

/* The provider that answers: the configured one if it can, else the first in the table that can. */
static void pick_provider(App *a) {
	const Provider *p = provider_find(a->provider_cfg);
	if (brain_usable(p)) { a->provider = p->prov; return; }
	a->provider = PROV_NONE;
	for (int i = 0; i < providers_count(); i++)
		if (brain_usable(provider_at(i))) { a->provider = provider_at(i)->prov; return; }
}

/* ---- Advanced AI: a service, model and thinking level per feature ---- */

static volatile int server_json_learned[NSERVERS] = { 2, 2, 2 }; /* custom_json auto: what each server took last (the
									* worker writes) */
static char server_models[NSERVERS][12][64];
static const char *server_model_ids[NSERVERS][12];
static int nserver_models[NSERVERS];

int brain_server_models(int slot, const char ***ids) {
	if (slot < 0 || slot >= NSERVERS) slot = 0;
	*ids = server_model_ids[slot];
	return nserver_models[slot];
}

/* The job's service (a self-hosted server by its slot) */
static const Provider *job_provider(AiJob *j) {
	return j->provider == PROV_CUSTOM ? provider_slot(j->slot) : provider_by_prov(j->provider);
}

static const char *draw_model(int prov);

/* The mode whose tuned settings a feature uses (prompts.c prompt_effort) */
static const char *feature_mode(int feat) {
	static const char *M[FEAT_COUNT] = { "talk", "notes", "math", "translate", "talk", "draw", "email" };
	return feat >= 0 && feat < FEAT_COUNT ? M[feat] : "talk";
}

/* The thinking level Inkling tunes a feature to (what Default means on its Tuning tab) */
const char *brain_default_effort(int feat) { return prompt_effort(feature_mode(feat)); }

static int feature_for(const char *mode, const char *game) {
	if (game && game[0]) return FEAT_GAMES;
	if (!strcmp(mode, "notes")) return FEAT_TYPEUP;
	if (!strcmp(mode, "math")) return FEAT_MATH;
	if (!strcmp(mode, "translate")) return FEAT_TRANSLATE;
	if (!strcmp(mode, "email")) return FEAT_EMAIL;
	if (!strcmp(mode, "draw")) return FEAT_DRAW;
	return FEAT_CHAT; /* talk, auto */
}

/* Inkling's choice of model for a feature on a service, from your model for that service: Claude's "fast" setting is
 * Haiku, with Sonnet for maths; drawings use the service's most capable model; email the quick one. */
static void recommended_model(App *a, const Provider *p, int feat, char *out, size_t n) {
	const char *m = brain_model(a, p);
	int fast = p->prov == PROV_ANTHROPIC && !strcmp(m, "fast");
	if (p->custom) snprintf(out, n, "%s", m);
	else if (feat == FEAT_DRAW) {
		const char *dm = draw_model(p->prov);
		snprintf(out, n, "%s", dm ? dm : fast ? "claude-sonnet-5-5" : m);
	} else if (feat == FEAT_EMAIL && p->prov == PROV_ANTHROPIC && (fast || strstr(m, "sonnet")))
		snprintf(out, n, "claude-haiku-5-5");
	else if (fast) snprintf(out, n, "%s", feat == FEAT_MATH ? "claude-sonnet-5-5" : "claude-haiku-5-5");
	else snprintf(out, n, "%s", m);
}

/* Services that take a thinking level: Claude always gets one (tuned per mode), Gemini a thinking level; OpenAI's
 * Responses and Chat Completions services only when Advanced AI sets one; Bedrock never. */
static int takes_effort(const Provider *p) { return p && p->api != API_BEDROCK; }

void brain_route(App *a, int feat, Route *r) {
	memset(r, 0, sizeof *r);
	if (feat < 0 || feat >= FEAT_COUNT) feat = FEAT_CHAT;
	const Provider *main_p = provider_by_prov(a->provider);
	const char *svc = a->feat_service[feat];
	const Provider *p = svc[0] ? provider_find(svc) : NULL;
	if (svc[0] && !brain_usable(p)) { r->fallback = 1; p = NULL; }
	if (!p) p = main_p;
	r->p = p;
	if (!p) return;
	if (p != main_p) r->custom = 1;
	char rec[64];
	recommended_model(a, p, feat, rec, sizeof rec);
	const char *m = a->feat_model[feat];
	if (m[0] && !r->fallback) { /* a model chosen for another service doesn't go to the main one */
		snprintf(r->model, sizeof r->model, "%s", m);
		if (strcmp(m, rec)) r->custom = 1;
	} else snprintf(r->model, sizeof r->model, "%s", rec);
	const char *tuned = prompt_effort(feature_mode(feat));
	int off_elsewhere = !strcmp(a->feat_effort[feat], "off") && !p->custom; /* Off is for self-hosted servers */
	if (a->feat_effort[feat][0] && takes_effort(p) && !off_elsewhere) {
		snprintf(r->effort, sizeof r->effort, "%s", a->feat_effort[feat]);
		r->effort_set = 1;
		if (strcmp(r->effort, tuned) || p->prov != PROV_ANTHROPIC) r->custom = 1;
	} else if (takes_effort(p)) snprintf(r->effort, sizeof r->effort, "%s", tuned);
	if (a->all_instr[0] || a->feat_instr[feat][0]) r->instr = r->custom = 1;
	if (a->feat_temp[feat][0]) {
		r->temp_set = r->custom = 1;
		r->temperature = atof(a->feat_temp[feat]);
	}
	r->max_out = a->feat_maxtok[feat] > 0 ? a->feat_maxtok[feat] : MAX_OUT;
	if (a->feat_maxtok[feat] > 0) r->custom = 1;
}

int brain_feature_custom(App *a, int feat) {
	return feat >= 0 && feat < FEAT_COUNT &&
	       (a->feat_service[feat][0] || a->feat_model[feat][0] || a->feat_effort[feat][0] || a->feat_instr[feat][0] ||
		a->feat_temp[feat][0] || a->feat_maxtok[feat] > 0);
}

/* The writer's instructions as kept: one line, no control characters, trimmed, at most AI_INSTR_MAX bytes (cut at a
 * character boundary, so UTF-8 typed on a computer stays whole). */
static void clean_instr(const char *v, char *out, size_t n) {
	size_t j = 0, cap = n - 1 < AI_INSTR_MAX ? n - 1 : AI_INSTR_MAX;
	for (const unsigned char *c = (const unsigned char *)v; *c && j < cap; c++) {
		unsigned char ch = *c < 0x20 || *c == 0x7f ? ' ' : *c;
		if (ch == ' ' && (!j || out[j - 1] == ' ')) continue;
		out[j++] = (char)ch;
	}
	if (j == cap && v[j] && ((unsigned char)v[j] & 0xC0) == 0x80) /* don't end inside a character */
		while (j && ((unsigned char)out[j - 1] & 0xC0) == 0x80) j--;
	if (j && ((unsigned char)out[j - 1] & 0xC0) == 0xC0) j--; /* a lead byte without its continuation */
	while (j && out[j - 1] == ' ') j--;
	out[j] = 0;
}

/* How big a model is, from the provider table (1 small and quick, 2 middle, 3 the most capable and slowest; 0 not in
 * the table). */
static int model_tier(const Provider *p, const char *model) {
	for (int i = 0; p && i < p->nmodels; i++)
		if (!strcmp(p->models[i].id, model)) return p->models[i].tier;
	if (p && p->prov == PROV_ANTHROPIC) /* the models "fast" stands for */
		return strstr(model, "haiku") ? 1 : strstr(model, "sonnet") ? 2 : strstr(model, "opus") ? 3 : 0;
	return 0;
}

/* A model's name for a sentence: its label without the chip's remarks ("Opus 5.5 - best, slower" -> "Opus 5.5",
 * "Haiku 5.5 only" -> "Haiku 5.5"), or the model ID. */
static const char *model_name(const Provider *p, const char *model) {
	static char b[80];
	const char *l = provider_model_label(p, model);
	if (!l) return model;
	snprintf(b, sizeof b, "%s", l);
	char *d = strstr(b, " - ");
	if (d) *d = 0;
	size_t n = strlen(b);
	if (n > 5 && !strcmp(b + n - 5, " only")) b[n - 5] = 0;
	return b;
}

static void addf(char *out, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void addf(char *out, size_t n, const char *fmt, ...) {
	size_t l = strlen(out);
	if (l + 2 >= n) return;
	if (l) out[l++] = '\n', out[l] = 0;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(out + l, n - l, fmt, ap);
	va_end(ap);
}

/* An address on your own network or computer (for the note about small local models): a private or loopback IPv4
 * address, localhost, or a .local name. */
static int local_address(const char *url) {
	const char *h = strstr(url, "://");
	h = h ? h + 3 : url;
	int o1, o2, o3, o4;
	if (sscanf(h, "%d.%d.%d.%d", &o1, &o2, &o3, &o4) == 4)
		return o1 == 10 || o1 == 127 || (o1 == 172 && o2 >= 16 && o2 <= 31) || (o1 == 192 && o2 == 168);
	char host[200];
	snprintf(host, sizeof host, "%.*s", (int)strcspn(h, ":/"), h);
	size_t hl = strlen(host);
	return !strcmp(host, "localhost") || (hl > 6 && !strcmp(host + hl - 6, ".local"));
}

void brain_route_notes(App *a, int feat, char *out, size_t n) {
	out[0] = 0;
	if (feat < 0 || feat >= FEAT_COUNT || !n) return;
	Route r;
	brain_route(a, feat, &r);
	const Feature *f = feature_at(feat);
	const Provider *main_p = provider_by_prov(a->provider), *p = r.p;
	if (r.fallback) {
		const Provider *want = provider_find(a->feat_service[feat]);
		addf(out, n, "%s isn't ready (%s), so %s answers instead for now.", want ? want->short_name : a->feat_service[feat],
		     want && want->custom ? "no address or model yet" : "no key yet", p ? p->short_name : "nothing");
	}
	if (!p) { addf(out, n, "No AI service is set up yet: add a key in AI & keys."); return; }
	/* (the own server gets its notes even as the main service; Off kept for a cloud service is named) */
	if (!r.custom && !p->custom && strcmp(a->feat_effort[feat], "off")) return;
	if (p->custom) {
		const CustomServer *cs = provider_custom_slot(p->slot);
		addf(out, n, "%s (your server): its speed, accuracy and privacy are up to it - Inkling can't check them.", p->short_name);
		if (f->image) addf(out, n, "Its model must read images, or it can't read your handwriting.");
		if (!strncmp(cs->url, "http://", 7) || local_address(cs->url)) addf(out, n, "Small local models often misread handwriting, slip in maths and draw roughly.");
		if (!strncmp(cs->url, "http://", 7))
			addf(out, n, "http:// isn't encrypted: use it only on your own network.");
	} else if (p != main_p)
		addf(out, n, "Uses %s, not %s: its own key, prices and speed.", p->short_name, main_p ? main_p->short_name : "the main service");
	char rec[64];
	recommended_model(a, p, feat, rec, sizeof rec);
	if (!p->custom && strcmp(r.model, rec)) {
		int t = model_tier(p, r.model), tr = model_tier(p, rec);
		const char *mn = model_name(p, r.model);
		if (!t) addf(out, n, "%s isn't in Inkling's list: if the name is wrong, replies fail%s.", mn,
			     f->image ? "; it must read images" : "");
		else if (t > tr) addf(out, n, "%s is a bigger model than the default: replies may take two to three times longer and cost more.", mn);
		else if (t < tr) {
			if (feat == FEAT_MATH) addf(out, n, "%s is a smaller model: more maths mistakes are likely.", mn);
			else if (feat == FEAT_DRAW) addf(out, n, "%s is a smaller model: drawings come out simpler and rougher.", mn);
			else addf(out, n, "%s is a smaller model: quicker, but it misreads messy writing more often.", mn);
		}
	}
	if (r.instr) {
		int both = a->all_instr[0] && a->feat_instr[feat][0];
		addf(out, n, "Your instructions%s are added to Inkling's own. Long or conflicting ones can make replies longer, slower "
			     "or less reliable; they can't change how replies are laid out on the page.",
		     both ? " (for every feature, then this one's)" : a->all_instr[0] ? " for every feature" : "");
	}
	if (r.temp_set) {
		if (r.temperature <= 0.01) addf(out, n, "Temperature 0: the most predictable replies; chat and games can get repetitive.");
		else if (r.temperature >= 0.99) addf(out, n, "Temperature %.1f: more varied replies, and more mistakes in maths and typing up.", r.temperature);
		else addf(out, n, "Temperature %.1f instead of the model's own setting.", r.temperature);
		addf(out, n, "Some models don't take a temperature; if one refuses, Inkling asks again without it.");
	}
	if (a->feat_maxtok[feat] > 0) {
		if (feat == FEAT_DRAW && r.max_out < 8000) addf(out, n, "Replies stop at %d tokens: drawings need room and are often cut off below 8000.", r.max_out);
		else if (r.max_out < MAX_OUT) addf(out, n, "Replies stop at %d tokens: long ones (typed-up pages, worked solutions) can be cut off and fail.", r.max_out);
		else addf(out, n, "Replies may run to %d tokens: long ones cost more and take longer.", r.max_out);
	}
	if (r.effort_set) {
		const char *tuned = prompt_effort(feature_mode(feat));
		if (!strcmp(r.effort, "off")) addf(out, n, feat == FEAT_MATH || feat == FEAT_DRAW ?
			"Thinking off: the quickest replies, but more mistakes in maths and rougher drawings." :
			"Thinking off: the quickest replies, but less careful.");
		else if (!strcmp(r.effort, "high")) addf(out, n, "High thinking: more careful, but slower and costlier.");
		else if (!strcmp(r.effort, "medium") && !strcmp(tuned, "low")) addf(out, n, "More thinking than the default: replies take longer.");
		else if (!strcmp(r.effort, "low") && strcmp(tuned, "low"))
			addf(out, n, feat == FEAT_MATH ? "Low thinking: faster, but more maths mistakes." : "Low thinking: faster, but less careful.");
		if (p->prov != PROV_ANTHROPIC && p->prov != PROV_GEMINI)
			addf(out, n, "Not every model takes a thinking level; if one refuses, Inkling asks again without it.");
	} else if (a->feat_effort[feat][0] && !takes_effort(p))
		addf(out, n, "%s doesn't take a thinking level: that setting is ignored here.", p->short_name);
	else if (!strcmp(a->feat_effort[feat], "off"))
		addf(out, n, "Thinking off is for self-hosted servers: %s thinks as usual here.", p->short_name);
	else if (p->custom)
		addf(out, n, "Many local models think before they answer, which can add 10-20 s: Thinking off (Tuning tab) answers sooner.");
}

int brain_config_apply(App *a, const char *k, const char *v) {
	if (!strcmp(k, "provider")) {
		snprintf(a->provider_cfg, sizeof a->provider_cfg, "%s", v[0] ? v : "auto");
		pick_provider(a);
		return 1;
	}
	const char *field = NULL;
	int slot = provider_custom_apply(k, v, &field);
	if (slot >= 0) {
		if (!strcmp(field, "url") || !strcmp(field, "api") || !strcmp(field, "json")) {
			server_json_learned[slot] = 2; /* another server (or API): find out again */
			if (strcmp(field, "json")) nserver_models[slot] = 0;
		}
		pick_provider(a);
		return 1;
	}
	if (!strcmp(k, "ai_advanced")) {
		a->ai_advanced_ok = !strcmp(v, "understood");
		return 1;
	}
	if (!strcmp(k, "ai_instructions")) {
		clean_instr(v, a->all_instr, sizeof a->all_instr);
		return 1;
	}
	if (!strncmp(k, "ai_", 3)) {
		for (int f = 0; f < FEAT_COUNT; f++) {
			const char *id = feature_at(f)->id;
			size_t il = strlen(id);
			if (strncmp(k + 3, id, il)) continue;
			const char *rest = k + 3 + il;
			int dflt = !v[0] || !strcmp(v, "default");
			if (!*rest) {
				if (dflt) a->feat_service[f][0] = 0;
				else if (provider_find(v)) snprintf(a->feat_service[f], sizeof a->feat_service[f], "%s", v);
				else logf_("ignoring %s %.30s: not a service", k, v);
				return 1;
			}
			if (!strcmp(rest, "_model")) {
				if (dflt) a->feat_model[f][0] = 0;
				else if (strlen(v) < 64 && strspn(v, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:/@+-") == strlen(v))
					snprintf(a->feat_model[f], sizeof a->feat_model[f], "%s", v);
				else logf_("ignoring %s %.60s: not a model name", k, v);
				return 1;
			}
			if (!strcmp(rest, "_temperature")) {
				double t = atof(v);
				if (dflt) a->feat_temp[f][0] = 0;
				else if (strspn(v, "0123456789.") == strlen(v) && t >= 0 && t <= 2)
					snprintf(a->feat_temp[f], sizeof a->feat_temp[f], "%.2g", t);
				else logf_("ignoring %s %.30s: use default or 0 to 2", k, v);
				return 1;
			}
			if (!strcmp(rest, "_max_tokens")) {
				int t = atoi(v);
				if (dflt) a->feat_maxtok[f] = 0;
				else if (strspn(v, "0123456789") == strlen(v)) a->feat_maxtok[f] = t < 256 ? 256 : t > 64000 ? 64000 : t;
				else logf_("ignoring %s %.30s: use default or a number of tokens", k, v);
				return 1;
			}
			if (!strcmp(rest, "_instructions")) {
				clean_instr(v, a->feat_instr[f], sizeof a->feat_instr[f]);
				return 1;
			}
			if (!strcmp(rest, "_effort")) {
				if (dflt) a->feat_effort[f][0] = 0;
				else if (!strcmp(v, "low") || !strcmp(v, "medium") || !strcmp(v, "high") || !strcmp(v, "off"))
					snprintf(a->feat_effort[f], sizeof a->feat_effort[f], "%s", v);
				else logf_("ignoring %s %.30s: use default, off, low, medium or high", k, v);
				return 1;
			}
		}
	}
	if (!strcmp(k, "bedrock_region")) {
		if (safe_name(v, 0)) snprintf(a->bedrock_region, sizeof a->bedrock_region, "%s", v);
		else if (v[0]) logf_("ignoring bedrock_region %.40s", v);
		return 1;
	}
	for (int i = 0; i < providers_count(); i++) {
		const Provider *p = provider_at(i);
		if (p->custom || strcmp(k, p->model_key)) continue;
		if (!v[0] || safe_name(v, 1)) snprintf(a->model[p->prov], sizeof a->model[p->prov], "%s", v);
		else logf_("ignoring %s %.60s: not a model name", k, v);
		return 1;
	}
	return 0;
}

void brain_keys_changed(App *a) {
	keys_load();
	snprintf(a->resend_key, sizeof a->resend_key, "%s", key_get("resend_api_key"));
	pick_provider(a);
	logf_("keys changed: provider %s", brain_provider_name(a));
}

/* A key drop file: a text file holding only the key, copied into the app folder over USB (copying a new file works
 * everywhere; editing config.txt in place over MTP often does not). Taken into the key store, then deleted. */
static void take_key_file(const char *file, const char *name, const char *who, size_t min_len) {
	char path[200];
	snprintf(path, sizeof path, USB_DIR "/%s", file);
	char *kb = read_file(path, NULL);
	if (!kb) return;
	char k1[330] = "";
	sscanf(kb, " %329s", k1);
	memset(kb, 0, strlen(kb));
	free(kb);
	if (strlen(k1) >= min_len) {
		key_set(name, k1);
		logf_("took the %s key from %s", who, path);
	}
	memset(k1, 0, sizeof k1);
	unlink(path);
}

/* A root-only directory of ours for keys, history and request files. Anything else in its place (a symlink, a file,
 * a directory someone else made - /var/local is group-writable on some firmware) is moved aside first. */
static void private_dir(const char *p) {
	struct stat st;
	if (lstat(p, &st) == 0 && (!S_ISDIR(st.st_mode) || st.st_uid != geteuid())) {
		char aside[256];
		snprintf(aside, sizeof aside, "%s.untrusted-%ld", p, (long)time(NULL));
		logf_("%s isn't a directory of ours: moved to %s", p, aside);
		if (rename(p, aside) != 0) unlink(p);
	}
	mkdir(p, 0700);
	if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) chmod(p, 0700);
}

static void write_new_config(void) {
	size_t cap = sizeof CONFIG_HEAD + sizeof CONFIG_ADVANCED + sizeof CONFIG_TEMPLATE + 2048, n = 0;
	char *b = malloc(cap);
	if (!b) return;
	n += (size_t)snprintf(b + n, cap - n, "%s", CONFIG_HEAD);
	for (int i = 0; i < providers_count(); i++) {
		const Provider *p = provider_at(i);
		if (p->custom) continue; /* (in CONFIG_ADVANCED) */
		n += (size_t)snprintf(b + n, cap - n, "# %s - key from %s\n%s =\n", p->label, p->key_where, p->key_name);
		if (p->needs_region) n += (size_t)snprintf(b + n, cap - n, "bedrock_region = %s\n", p->default_region);
		n += (size_t)snprintf(b + n, cap - n, "%s = %s\n\n", p->model_key, p->default_model);
	}
	n += (size_t)snprintf(b + n, cap - n, "%s%s", CONFIG_TEMPLATE, CONFIG_ADVANCED);
	write_file(USB_DIR "/config.txt", b, n, 0644);
	free(b);
}

void brain_load_config(App *a) {
	snprintf(a->provider_cfg, sizeof a->provider_cfg, "auto");
	snprintf(a->bedrock_region, sizeof a->bedrock_region, "%s", provider_find("bedrock")->default_region);
	snprintf(a->notebook, sizeof a->notebook, "default");
	a->anim_speed = 1.0;
	a->pause_ms = 1500;
	settings_apply(a, "reply_voice", "friendly");
	settings_apply(a, "reply_length", "medium");
	settings_apply(a, "math_help", "hints");
	settings_apply(a, "typeup", "replace");
	settings_apply(a, "email_provider", "gmail");
	settings_apply(a, "scratch_erase", "on");
	settings_apply(a, "reply_language", "match");
	settings_apply(a, "handwriting_size", "medium");
	settings_apply(a, "ink", "normal");
	settings_apply(a, "translate_to", "English");
	settings_apply(a, "handwriting_font", "script");
	settings_apply(a, "typed_font", "bookerly");
	private_dir(SECURE_DIR);
	private_dir(SECURE_DIR "/history");
	private_dir(RUN_DIR);
	mkdir(USB_DIR, 0755);
	mkdir(USB_DIR "/notebooks", 0755);

	/* keys saved earlier: the key store reads the same name=value keys.txt as before, so old keys carry over */
	keys_load();
	for (int i = 0; i < providers_count(); i++) /* (a self-hosted server's key can be short: "lm-studio") */
		take_key_file(provider_at(i)->key_file, provider_at(i)->key_name, provider_at(i)->short_name, provider_at(i)->custom ? 1 : 21);
	take_key_file("resend-key.txt", "resend_api_key", "Resend", 21);

	/* user config; move any pasted key into protected storage and blank it in the USB copy */
	size_t clen;
	char *cfg = read_file(USB_DIR "/config.txt", &clen);
	if (!cfg) {
		write_new_config();
		cfg = read_file(USB_DIR "/config.txt", &clen);
		if (!cfg) return;
	}
	/* a pasted key's line becomes "<name> = saved-on-device", which can be longer than the line it replaces (at most
	 * sizeof repl), on any number of lines; plus the pause_seconds lines */
	size_t nlines = 1;
	for (const char *c = cfg; *c; c++) nlines += *c == '\n';
	char *out = malloc(clen + nlines * 161 + 256);
	if (!out) { free(cfg); return; }
	size_t on = 0;
	int changed = 0, seen_pause = 0;
	for (char *p = cfg; *p;) {
		char *nl = strchr(p, '\n');
		size_t ll = nl ? (size_t)(nl - p) : strlen(p);
		char ln[512], k[64], v[400];
		snprintf(ln, sizeof ln, "%.*s", (int)(ll < sizeof ln - 1 ? ll : sizeof ln - 1), p);
		trim(ln);
		const char *emit = NULL;
		char repl[160];
		char *eq = strchr(ln, '=');
		if (ln[0] && ln[0] != '#' && eq) {
			*eq = 0;
			snprintf(k, sizeof k, "%s", ln);
			snprintf(v, sizeof v, "%s", eq + 1);
			trim(k);
			trim(v);
			int is_key = !strcmp(k, "resend_api_key");
			for (int i = 0; !is_key && i < providers_count(); i++) is_key = !strcmp(k, provider_at(i)->key_name);
			if (is_key && v[0] && strcmp(v, "saved-on-device")) {
				key_set(k, strcmp(v, "delete") ? v : "");
				snprintf(repl, sizeof repl, "%s = %s", k, key_get(k)[0] ? "saved-on-device" : "");
				emit = repl;
				changed = 1;
			} else if (brain_config_apply(a, k, v)) {
			}
			else if (!strcmp(k, "notebook") && v[0]) {
				/* keep it a safe file name */
				size_t j = 0;
				for (char *c = v; *c && j < sizeof a->notebook - 1; c++)
					a->notebook[j++] = (char)((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' ? *c : '_');
				a->notebook[j] = 0;
			} else if (v[0] && settings_apply(a, k, v)) {
				if (!strcmp(k, "pause_seconds")) seen_pause = 1;
			}
		}
		if (emit) on += (size_t)sprintf(out + on, "%s\n", emit);
		else { memcpy(out + on, p, ll); on += ll; out[on++] = '\n'; }
		p = nl ? nl + 1 : p + ll;
	}
	if (!seen_pause) { /* older config.txt: add the setting so it can be changed */
		on += (size_t)sprintf(out + on, "\n# Seconds of pause after you lift the pen before the AI answers (0 = only the Send button).\npause_seconds = 1.5\n");
		changed = 1;
	}
	if (changed && write_file(USB_DIR "/config.txt", out, on, 0644) != 0) logf_("could not rewrite config.txt");
	free(out);
	free(cfg);

	snprintf(a->resend_key, sizeof a->resend_key, "%s", key_get("resend_api_key"));
	pick_provider(a);

	char hp[256];
	history_path(a, hp, sizeof hp);
	char *hj = read_file(hp, NULL);
	hist = hj ? cJSON_Parse(hj) : NULL;
	free(hj);
	if (!cJSON_IsArray(hist)) { cJSON_Delete(hist); hist = cJSON_CreateArray(); }
	logf_("config: provider %s, notebook %s, %d history entries", brain_provider_name(a), a->notebook,
	      cJSON_GetArraySize(hist));
}

const char *brain_provider_name(App *a) {
	static char b[128];
	const Provider *p = provider_by_prov(a->provider);
	if (p) {
		const char *m = brain_model(a, p);
		if (p->prov == PROV_ANTHROPIC && !strcmp(m, "fast")) snprintf(b, sizeof b, "Claude, fastest (Haiku; Sonnet for maths)");
		else if (p->custom) snprintf(b, sizeof b, "%s (%s)", p->short_name, m);
		else if (p->needs_region) snprintf(b, sizeof b, "%s %s (%s)", p->short_name, m, a->bedrock_region);
		else snprintf(b, sizeof b, "%s %s", p->short_name, m);
	} else snprintf(b, sizeof b, "no API key");
	return b;
}

/* Fills in the service, model, key and request settings for a feature (main thread). */
static void job_route(App *a, AiJob *j, int feat) {
	Route r;
	brain_route(a, feat, &r);
	const Provider *p = r.p;
	j->feat = feat;
	j->customized = r.custom;
	j->provider = p ? p->prov : PROV_NONE;
	j->api = API_ANTHROPIC;
	j->base_url[0] = j->model[0] = j->region[0] = j->effort[0] = 0;
	memset(j->key, 0, sizeof j->key);
	j->effort_set = j->json_auto = j->temp_set = j->slot = 0;
	j->timeout = 170;
	j->json_mode = 2;
	j->max_out = r.max_out > 0 ? r.max_out : MAX_OUT;
	j->temp_set = r.temp_set;
	j->temperature = r.temperature;
	snprintf(j->instr, sizeof j->instr, "%s%s%s", a->all_instr, a->all_instr[0] && a->feat_instr[feat][0] ? "\n" : "",
		 a->feat_instr[feat]);
	if (!p) return;
	j->api = p->api;
	snprintf(j->base_url, sizeof j->base_url, "%s", p->base_url ? p->base_url : "");
	/* "fast" (Claude's default) resolves per feature in recommended_model: Haiku answers in about 2 s; maths goes to
	 * Sonnet, which checks working reliably (measured on the Kindle: Opus averaged 5.2 s, Sonnet 3.2 s, Haiku 2.0 s;
	 * Haiku got a sum check wrong) */
	snprintf(j->model, sizeof j->model, "%s", r.model);
	snprintf(j->key, sizeof j->key, "%s", key_get(p->key_name));
	if (p->needs_region) snprintf(j->region, sizeof j->region, "%s", a->bedrock_region);
	j->effort_set = r.effort_set;
	snprintf(j->effort, sizeof j->effort, "%s", r.effort_set ? r.effort : "");
	/* Chat Completions: 2 = response_format json_schema (all three services document it), 0 = the schema in the
	 * prompt only (OpenRouter's Amazon Nova endpoints take no response_format) */
	if (p->prov == PROV_OPENROUTER && !strncmp(j->model, "amazon/", 7)) j->json_mode = 0;
	if (p->custom) {
		const CustomServer *cs = provider_custom_slot(p->slot);
		j->slot = p->slot;
		j->timeout = cs->timeout;
		j->json_auto = !strcmp(cs->json, "auto");
		j->json_mode = j->json_auto ? server_json_learned[p->slot] : !strcmp(cs->json, "json") ? 1 : !strcmp(cs->json, "prompt") ? 0 : 2;
	}
}

AiJob *brain_prepare(App *a) {
	AiJob *j = calloc(1, sizeof *j);
	snprintf(j->mode, sizeof j->mode, "%s", a->mode_id[a->mode]);
	job_route(a, j, feature_for(j->mode, a->game_prompt));
	snprintf(j->voice, sizeof j->voice, "%s", a->voice);
	snprintf(j->length, sizeof j->length, "%s", a->length);
	snprintf(j->math, sizeof j->math, "%s", a->math_help);
	snprintf(j->lang, sizeof j->lang, "%s", a->language);
	snprintf(j->target, sizeof j->target, "%s", a->translate_to);
	snprintf(j->game, sizeof j->game, "%s", a->game_prompt ? a->game_prompt : "");
	j->page = a->page;
	j->started = mono_ms();
	/* page snapshot: the canvas from its top down to just below the lowest ink, at full resolution */
	int y0 = a->cy, y1 = a->ink_bottom + 40;
	if (y1 < y0 + 240) y1 = y0 + 240;
	if (y1 > a->cy + a->ch) y1 = a->cy + a->ch;
	j->w = a->cw;
	j->h = y1 - y0;
	j->px = malloc((size_t)j->w * (size_t)j->h);
	for (int y = 0; y < j->h; y++)
		memcpy(j->px + (size_t)y * (size_t)j->w, a->fb + (size_t)(a->pan_y + y0 + y) * (size_t)a->stride + (size_t)a->cx,
		       (size_t)j->w);
	j->bx0 = j->w; j->by0 = j->h; j->bx1 = -1; j->by1 = -1;
	for (int i = 0; i < a->npending; i++) {
		if (a->pending[i].tool) continue;
		for (int k = 0; k < a->pending[i].n; k++) {
			int x = a->pending[i].pts[k].x - a->cx, y = a->pending[i].pts[k].y - y0;
			if (x < j->bx0) j->bx0 = x;
			if (x > j->bx1) j->bx1 = x;
			if (y < j->by0) j->by0 = y;
			if (y > j->by1) j->by1 = y;
		}
	}
	if (j->bx1 >= j->bx0) {
		j->bx0 = j->bx0 > 12 ? j->bx0 - 12 : 0;
		j->by0 = j->by0 > 12 ? j->by0 - 12 : 0;
		j->bx1 = j->bx1 + 12 < j->w ? j->bx1 + 12 : j->w - 1;
		j->by1 = j->by1 + 12 < j->h ? j->by1 + 12 : j->h - 1;
	}
	j->history = cJSON_PrintUnformatted(hist);
	return j;
}

void brain_job_free(AiJob *j) {
	if (!j) return;
	free(j->px);
	free(j->history);
	memset(j->key, 0, sizeof j->key);
	memset(j->epass, 0, sizeof j->epass);
	memset(j->rkey, 0, sizeof j->rkey);
	free(j);
}

/* ---- worker side ---- */
typedef struct {
	unsigned char *p;
	size_t n, cap;
} Buf;

static void png_cb(void *ctx, void *data, int size) {
	Buf *b = ctx;
	if (b->n + (size_t)size > b->cap) {
		b->cap = (b->n + (size_t)size) * 2;
		b->p = realloc(b->p, b->cap);
	}
	memcpy(b->p + b->n, data, (size_t)size);
	b->n += (size_t)size;
}

static char *b64(const unsigned char *d, size_t n) {
	static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char *o = malloc(4 * ((n + 2) / 3) + 1), *q = o;
	for (size_t i = 0; i < n; i += 3) {
		unsigned v = (unsigned)d[i] << 16 | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
		*q++ = T[v >> 18 & 63];
		*q++ = T[v >> 12 & 63];
		*q++ = i + 1 < n ? T[v >> 6 & 63] : '=';
		*q++ = i + 2 < n ? T[v & 63] : '=';
	}
	*q = 0;
	return o;
}

static int model_has_fallbacks(const char *m) {
	return !strncmp(m, "claude-opus-5", 13) || !strncmp(m, "claude-sonnet-5-5", 17) || !strncmp(m, "claude-fable", 12);
}

static const char *jstr(cJSON *o, const char *k) {
	const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(o, k));
	return s ? s : "";
}

/* Bedrock: Amazon Nova has no structured-output mode, so it fills a forced tool whose input schema is the reply
 * schema; other Bedrock models (Claude) use Converse's native JSON-schema output (Claude refuses forced tools). */
static int bedrock_uses_tool(const char *model) { return strstr(model, "amazon.nova") != NULL; }

/* The output token limit (Advanced AI per feature, else MAX_OUT) */
static int job_max(AiJob *j) { return j->max_out > 0 ? j->max_out : MAX_OUT; }

/* The thinking level for this request: Advanced AI's choice, else the tuned level for the mode. */
static const char *job_effort(AiJob *j) { return j->effort_set ? j->effort : prompt_effort(j->mode); }
/* Off, in the words of the OpenAI-style APIs */
static const char *api_effort(AiJob *j) { return strcmp(j->effort, "off") ? j->effort : "none"; }

/* The instructions with the reply schema written in, for services that don't hold replies to a schema (malloc'd). */
static char *sys_with_schema(const char *sys, cJSON *schema) {
	char *sch = cJSON_PrintUnformatted(schema);
	size_t n = strlen(sys) + (sch ? strlen(sch) : 0) + 160;
	char *s2 = malloc(n);
	if (s2) snprintf(s2, n, "%s\nAnswer with only one JSON object, no code fence, that matches this JSON Schema: %s", sys,
			 sch ? sch : "{}");
	free(sch);
	return s2;
}

/* One text part / message: {"<k>": text} */
static cJSON *obj1(const char *k, const char *text) {
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject(o, k, text);
	return o;
}

static cJSON *msg_text(const char *role, const char *text) {
	cJSON *m = cJSON_CreateObject();
	cJSON_AddStringToObject(m, "role", role);
	cJSON_AddStringToObject(m, "content", text);
	return m;
}

static void req_anthropic(cJSON *root, AiJob *j, const char *sys, const char *info, cJSON *hist_in, cJSON *schema,
			  const char *img64) {
	int own = j->provider == PROV_CUSTOM; /* an Anthropic-compatible server: only what was asked for */
	cJSON_AddStringToObject(root, "model", j->model);
	cJSON_AddNumberToObject(root, "max_tokens", job_max(j));
	if (j->temp_set) cJSON_AddNumberToObject(root, "temperature", j->temperature);
	if (j->json_mode == 2) cJSON_AddStringToObject(root, "system", sys);
	else {
		char *s2 = sys_with_schema(sys, schema);
		cJSON_AddStringToObject(root, "system", s2 ? s2 : sys);
		free(s2);
	}
	int off = own && j->effort_set && !strcmp(j->effort, "off"); /* Off: no thinking at all, not an effort level */
	if (off) cJSON_AddStringToObject(cJSON_AddObjectToObject(root, "thinking"), "type", "disabled");
	if (!own || (j->effort_set && !off) || j->json_mode == 2) {
		cJSON *oc = cJSON_AddObjectToObject(root, "output_config");
		if (!own || (j->effort_set && !off)) cJSON_AddStringToObject(oc, "effort", job_effort(j));
		if (j->json_mode == 2) {
			cJSON *fmt = cJSON_AddObjectToObject(oc, "format");
			cJSON_AddStringToObject(fmt, "type", "json_schema");
			cJSON_AddItemToObject(fmt, "schema", schema);
			schema = NULL;
		}
	}
	cJSON_Delete(schema);
	if (!own && model_has_fallbacks(j->model)) cJSON_AddStringToObject(root, "fallbacks", "default");
	cJSON *msgs = cJSON_AddArrayToObject(root, "messages"), *h;
	cJSON_ArrayForEach(h, hist_in) cJSON_AddItemToArray(msgs, msg_text(jstr(h, "role"), jstr(h, "text")));
	cJSON *u = cJSON_CreateObject();
	cJSON_AddStringToObject(u, "role", "user");
	cJSON *content = cJSON_AddArrayToObject(u, "content");
	if (img64) {
		cJSON *img = cJSON_CreateObject();
		cJSON_AddStringToObject(img, "type", "image");
		cJSON *src = cJSON_AddObjectToObject(img, "source");
		cJSON_AddStringToObject(src, "type", "base64");
		cJSON_AddStringToObject(src, "media_type", "image/png");
		cJSON_AddStringToObject(src, "data", img64);
		cJSON_AddItemToArray(content, img);
	}
	cJSON *t = obj1("type", "text");
	cJSON_AddStringToObject(t, "text", info);
	cJSON_AddItemToArray(content, t);
	cJSON_AddItemToArray(msgs, u);
}

static void req_openai(cJSON *root, AiJob *j, const char *sys, const char *info, cJSON *hist_in, cJSON *schema,
		       const char *dataurl) {
	cJSON_AddStringToObject(root, "model", j->model);
	if (j->json_mode == 2) cJSON_AddStringToObject(root, "instructions", sys);
	else {
		char *s2 = sys_with_schema(sys, schema);
		cJSON_AddStringToObject(root, "instructions", s2 ? s2 : sys);
		free(s2);
	}
	cJSON_AddNumberToObject(root, "max_output_tokens", job_max(j));
	if (j->temp_set) cJSON_AddNumberToObject(root, "temperature", j->temperature);
	if (j->effort_set) cJSON_AddStringToObject(cJSON_AddObjectToObject(root, "reasoning"), "effort", api_effort(j));
	if (j->json_mode >= 1) {
		cJSON *fmt = cJSON_AddObjectToObject(cJSON_AddObjectToObject(root, "text"), "format");
		if (j->json_mode == 2) {
			cJSON_AddStringToObject(fmt, "type", "json_schema");
			cJSON_AddStringToObject(fmt, "name", "scribe_reply");
			cJSON_AddItemToObject(fmt, "schema", schema);
			cJSON_AddBoolToObject(fmt, "strict", 1);
			schema = NULL;
		} else cJSON_AddStringToObject(fmt, "type", "json_object");
	}
	cJSON_Delete(schema);
	cJSON *in = cJSON_AddArrayToObject(root, "input"), *h;
	cJSON_ArrayForEach(h, hist_in) cJSON_AddItemToArray(in, msg_text(jstr(h, "role"), jstr(h, "text")));
	cJSON *u = cJSON_CreateObject();
	cJSON_AddStringToObject(u, "role", "user");
	cJSON *content = cJSON_AddArrayToObject(u, "content");
	cJSON *t = obj1("type", "input_text");
	cJSON_AddStringToObject(t, "text", info);
	cJSON_AddItemToArray(content, t);
	if (dataurl) {
		cJSON *img = obj1("type", "input_image");
		cJSON_AddStringToObject(img, "image_url", dataurl);
		cJSON_AddStringToObject(img, "detail", "high");
		cJSON_AddItemToArray(content, img);
	}
	cJSON_AddItemToArray(in, u);
}

/* Gemini generateContent: the model is in the URL; JSON out through responseMimeType + responseJsonSchema */
static void req_gemini(cJSON *root, AiJob *j, const char *sys, const char *info, cJSON *hist_in, cJSON *schema,
		       const char *img64) {
	cJSON *si = cJSON_AddObjectToObject(root, "systemInstruction");
	cJSON_AddItemToArray(cJSON_AddArrayToObject(si, "parts"), obj1("text", sys));
	cJSON *contents = cJSON_AddArrayToObject(root, "contents"), *h;
	cJSON_ArrayForEach(h, hist_in) {
		cJSON *m = obj1("role", !strcmp(jstr(h, "role"), "assistant") ? "model" : "user");
		cJSON_AddItemToArray(cJSON_AddArrayToObject(m, "parts"), obj1("text", jstr(h, "text")));
		cJSON_AddItemToArray(contents, m);
	}
	cJSON *u = obj1("role", "user");
	cJSON *parts = cJSON_AddArrayToObject(u, "parts");
	if (img64) {
		cJSON *img = cJSON_CreateObject();
		cJSON *inl = cJSON_AddObjectToObject(img, "inlineData");
		cJSON_AddStringToObject(inl, "mimeType", "image/png");
		cJSON_AddStringToObject(inl, "data", img64);
		cJSON_AddItemToArray(parts, img);
	}
	cJSON_AddItemToArray(parts, obj1("text", info));
	cJSON_AddItemToArray(contents, u);
	cJSON *gc = cJSON_AddObjectToObject(root, "generationConfig");
	cJSON_AddStringToObject(gc, "responseMimeType", "application/json");
	cJSON_AddItemToObject(gc, "responseJsonSchema", schema);
	cJSON_AddNumberToObject(gc, "maxOutputTokens", job_max(j));
	if (j->temp_set) cJSON_AddNumberToObject(gc, "temperature", j->temperature);
	/* thinking level (Gemini 3 and later; 2.x models reject it): low for chat, medium for notes and maths, or Advanced
	 * AI's choice */
	if (strncmp(j->model, "gemini-2", 8) && strncmp(j->model, "gemini-1", 8)) {
		const char *e = job_effort(j);
		cJSON_AddStringToObject(cJSON_AddObjectToObject(gc, "thinkingConfig"), "thinkingLevel",
					!strcmp(e, "low") ? "LOW" : !strcmp(e, "high") && j->effort_set ? "HIGH" : "MEDIUM");
	}
}

/* Bedrock Converse: image as base64 bytes; structured output by a forced tool (Nova) or outputConfig.textFormat */
static void req_bedrock(cJSON *root, AiJob *j, const char *sys, const char *info, cJSON *hist_in, cJSON *schema,
			const char *img64, int email) {
	cJSON_AddItemToArray(cJSON_AddArrayToObject(root, "system"), obj1("text", sys));
	cJSON *msgs = cJSON_AddArrayToObject(root, "messages"), *h;
	cJSON_ArrayForEach(h, hist_in) {
		cJSON *m = obj1("role", jstr(h, "role"));
		cJSON_AddItemToArray(cJSON_AddArrayToObject(m, "content"), obj1("text", jstr(h, "text")));
		cJSON_AddItemToArray(msgs, m);
	}
	cJSON *u = obj1("role", "user");
	cJSON *content = cJSON_AddArrayToObject(u, "content");
	if (img64) {
		cJSON *img = cJSON_CreateObject(), *im = cJSON_AddObjectToObject(img, "image");
		cJSON_AddStringToObject(im, "format", "png");
		cJSON_AddStringToObject(cJSON_AddObjectToObject(im, "source"), "bytes", img64);
		cJSON_AddItemToArray(content, img);
	}
	cJSON_AddItemToArray(content, obj1("text", info));
	cJSON_AddItemToArray(msgs, u);
	int tool = bedrock_uses_tool(j->model);
	cJSON *ic = cJSON_AddObjectToObject(root, "inferenceConfig");
	cJSON_AddNumberToObject(ic, "maxTokens", tool && job_max(j) > 10000 ? 10000 : job_max(j));
	if (j->temp_set) cJSON_AddNumberToObject(ic, "temperature", j->temperature);
	const char *desc = email ? "The email: subject and plain-text body."
			   : !strcmp(j->mode, "draw") ? "The drawing: pen strokes on the canvas." : "Your answer, written back onto the page.";
	if (tool) {
		cJSON *tc = cJSON_AddObjectToObject(root, "toolConfig");
		cJSON *spec = cJSON_CreateObject(), *ts = cJSON_AddObjectToObject(spec, "toolSpec");
		cJSON_AddStringToObject(ts, "name", "scribe_reply");
		cJSON_AddStringToObject(ts, "description", desc);
		cJSON_AddItemToObject(cJSON_AddObjectToObject(ts, "inputSchema"), "json", schema);
		cJSON_AddItemToArray(cJSON_AddArrayToObject(tc, "tools"), spec);
		cJSON_AddStringToObject(cJSON_AddObjectToObject(cJSON_AddObjectToObject(tc, "toolChoice"), "tool"), "name", "scribe_reply");
	} else {
		cJSON *tf = cJSON_AddObjectToObject(cJSON_AddObjectToObject(root, "outputConfig"), "textFormat");
		cJSON_AddStringToObject(tf, "type", "json_schema");
		cJSON *js = cJSON_AddObjectToObject(cJSON_AddObjectToObject(tf, "structure"), "jsonSchema");
		char *sch = cJSON_PrintUnformatted(schema); /* Converse takes the schema as a JSON string */
		cJSON_AddStringToObject(js, "schema", sch ? sch : "{}");
		cJSON_AddStringToObject(js, "name", "scribe_reply");
		cJSON_AddStringToObject(js, "description", desc);
		free(sch);
		cJSON_Delete(schema);
	}
}

/* OpenAI-style Chat Completions (xAI, Mistral, OpenRouter): image as a data URL */
static void req_chat(cJSON *root, AiJob *j, const char *sys, const char *info, cJSON *hist_in, cJSON *schema,
		     const char *dataurl) {
	int mode = j->json_mode;
	cJSON_AddStringToObject(root, "model", j->model);
	cJSON_AddNumberToObject(root, "max_tokens", job_max(j));
	if (j->temp_set) cJSON_AddNumberToObject(root, "temperature", j->temperature);
	if (j->effort_set) {
		if (j->provider == PROV_OPENROUTER) cJSON_AddStringToObject(cJSON_AddObjectToObject(root, "reasoning"), "effort", api_effort(j));
		else cJSON_AddStringToObject(root, "reasoning_effort", api_effort(j));
	}
	cJSON *msgs = cJSON_AddArrayToObject(root, "messages"), *h;
	if (mode < 2) { /* no schema support: the schema goes into the instructions */
		char *s2 = sys_with_schema(sys, schema);
		cJSON_AddItemToArray(msgs, msg_text("system", s2 ? s2 : sys));
		free(s2);
	} else cJSON_AddItemToArray(msgs, msg_text("system", sys));
	cJSON_ArrayForEach(h, hist_in) cJSON_AddItemToArray(msgs, msg_text(jstr(h, "role"), jstr(h, "text")));
	cJSON *u = obj1("role", "user");
	cJSON *content = cJSON_AddArrayToObject(u, "content");
	cJSON *t = obj1("type", "text");
	cJSON_AddStringToObject(t, "text", info);
	cJSON_AddItemToArray(content, t);
	if (dataurl) {
		cJSON *img = obj1("type", "image_url");
		if (j->provider == PROV_MISTRAL) cJSON_AddStringToObject(img, "image_url", dataurl); /* Mistral: a plain string */
		else {
			cJSON *iu = cJSON_AddObjectToObject(img, "image_url");
			cJSON_AddStringToObject(iu, "url", dataurl);
			cJSON_AddStringToObject(iu, "detail", "high");
		}
		cJSON_AddItemToArray(content, img);
	}
	cJSON_AddItemToArray(msgs, u);
	if (mode == 2) {
		cJSON *rf = cJSON_AddObjectToObject(root, "response_format");
		cJSON_AddStringToObject(rf, "type", "json_schema");
		cJSON *js = cJSON_AddObjectToObject(rf, "json_schema");
		cJSON_AddStringToObject(js, "name", "scribe_reply");
		cJSON_AddBoolToObject(js, "strict", 1);
		cJSON_AddItemToObject(js, "schema", schema);
	} else {
		if (mode == 1) cJSON_AddStringToObject(cJSON_AddObjectToObject(root, "response_format"), "type", "json_object");
		cJSON_Delete(schema);
	}
	/* OpenRouter: only route to endpoints that honour response_format */
	if (j->provider == PROV_OPENROUTER && mode >= 1)
		cJSON_AddBoolToObject(cJSON_AddObjectToObject(root, "provider"), "require_parameters", 1);
}

static char *build_request(AiJob *j, const char *img64) {
	char sys[10240], info[512];
	prompt_system(j->mode, j->voice, j->length, j->math, j->lang, j->target, j->game, sys, sizeof sys);
	if (j->instr[0]) /* (the longest built-in prompt is ~4.7 KB: these always fit) */
		snprintf(sys + strlen(sys), sizeof sys - strlen(sys),
			 "\n\nThe writer's own instructions for you. Follow them, except where they conflict with the rules above "
			 "about the JSON answer and how it is laid out on the page:\n%s", j->instr);
	if (j->bx1 >= j->bx0)
		snprintf(info, sizeof info,
			 "Mode: %s. Page %d. The image is %dx%d px; the newest writing is inside the box from (%d,%d) to (%d,%d).",
			 j->mode, j->page, j->w, j->h, j->bx0, j->by0, j->bx1, j->by1);
	else
		snprintf(info, sizeof info, "Mode: %s. Page %d. Read the whole page again and answer it in this mode.", j->mode, j->page);
	cJSON *hist_in = cJSON_Parse(j->history ? j->history : "[]");
	cJSON *root = cJSON_CreateObject();
	int email = !strcmp(j->mode, "email"), draw = !strcmp(j->mode, "draw");
	cJSON *schema = cJSON_Parse(email ? EMAIL_SCHEMA : draw ? DRAW_SCHEMA : SCHEMA); /* owned by the request from here */
	if (email) snprintf(info, sizeof info, "Turn page %d into the email.", j->page);
	if (draw) snprintf(info, sizeof info, "Draw: %s", j->draw);
	char *dataurl = NULL; /* (no image for a drawing pass) */
	if (img64) {
		size_t ul = strlen(img64) + 32;
		dataurl = malloc(ul);
		snprintf(dataurl, ul, "data:image/png;base64,%s", img64);
	}
	switch (j->api) {
	case API_OPENAI_RESPONSES: req_openai(root, j, sys, info, hist_in, schema, dataurl); break;
	case API_GEMINI: req_gemini(root, j, sys, info, hist_in, schema, img64); break;
	case API_BEDROCK: req_bedrock(root, j, sys, info, hist_in, schema, img64, email); break;
	case API_CHAT: req_chat(root, j, sys, info, hist_in, schema, dataurl); break;
	default: req_anthropic(root, j, sys, info, hist_in, schema, img64); break;
	}
	char *s = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	cJSON_Delete(hist_in);
	free(dataurl);
	return s;
}

static int ci_contains(const char *s, const char *needle) {
	size_t n = strlen(needle);
	for (; s && *s; s++) {
		size_t i = 0;
		while (i < n && s[i] && (s[i] | 0x20) == (needle[i] | 0x20)) i++;
		if (i == n) return 1;
	}
	return 0;
}

/* The model's structured answer inside a service's response, as JSON text (malloc'd), or NULL. *refused = 1 when the
 * service says the model declined. Any prose or code fence around the JSON object is dropped. */
static char *answer_text(int api, cJSON *root, int *refused) {
	*refused = 0;
	char *s = NULL;
	const char *stop = NULL, *t;
	cJSON *c, *o;
	switch (api) {
	case API_OPENAI_RESPONSES:
		cJSON_ArrayForEach(o, cJSON_GetObjectItem(root, "output")) {
			if (strcmp(jstr(o, "type"), "message")) continue;
			cJSON_ArrayForEach(c, cJSON_GetObjectItem(o, "content")) {
				t = jstr(c, "type");
				if (!strcmp(t, "refusal")) *refused = 1;
				if (!strcmp(t, "output_text")) { free(s); s = strdup(jstr(c, "text")); }
			}
		}
		if (!strcmp(jstr(cJSON_GetObjectItem(root, "incomplete_details"), "reason"), "max_output_tokens")) stop = "max";
		break;
	case API_GEMINI: {
		if (cJSON_GetObjectItem(cJSON_GetObjectItem(root, "promptFeedback"), "blockReason")) *refused = 1;
		cJSON *cand = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "candidates"), 0);
		size_t n = 0;
		cJSON_ArrayForEach(c, cJSON_GetObjectItem(cJSON_GetObjectItem(cand, "content"), "parts")) {
			if (cJSON_IsTrue(cJSON_GetObjectItem(c, "thought")) || !cJSON_IsString(cJSON_GetObjectItem(c, "text"))) continue;
			t = jstr(c, "text");
			s = realloc(s, n + strlen(t) + 1);
			memcpy(s + n, t, strlen(t) + 1);
			n += strlen(t);
		}
		const char *fr = jstr(cand, "finishReason");
		if (!s && (!strcmp(fr, "SAFETY") || !strcmp(fr, "PROHIBITED_CONTENT") || !strcmp(fr, "BLOCKLIST") ||
			   !strcmp(fr, "SPII") || !strcmp(fr, "IMAGE_SAFETY") || !strcmp(fr, "RECITATION")))
			*refused = 1;
		if (!strcmp(fr, "MAX_TOKENS")) stop = "max";
		break;
	}
	case API_BEDROCK:
		t = jstr(root, "stopReason");
		if (!strcmp(t, "guardrail_intervened") || !strcmp(t, "content_filtered")) *refused = 1;
		if (!strcmp(t, "max_tokens")) stop = "max";
		cJSON_ArrayForEach(c, cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "output"), "message"), "content")) {
			cJSON *tu = cJSON_GetObjectItem(c, "toolUse");
			if (cJSON_IsObject(cJSON_GetObjectItem(tu, "input"))) { free(s); s = cJSON_PrintUnformatted(cJSON_GetObjectItem(tu, "input")); break; }
			if (cJSON_IsString(cJSON_GetObjectItem(c, "text"))) { free(s); s = strdup(jstr(c, "text")); }
		}
		break;
	case API_CHAT: {
		cJSON *ch = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "choices"), 0);
		cJSON *m = cJSON_GetObjectItem(ch, "message"), *ct = cJSON_GetObjectItem(m, "content");
		if (jstr(m, "refusal")[0]) *refused = 1;
		if (!strcmp(jstr(ch, "finish_reason"), "length")) stop = "max";
		if (!strcmp(jstr(ch, "finish_reason"), "content_filter")) *refused = 1;
		if (cJSON_IsString(ct)) s = strdup(ct->valuestring);
		else if (cJSON_IsArray(ct)) { /* content chunks (Mistral reasoning models): the text ones */
			size_t n = 0;
			cJSON_ArrayForEach(c, ct) {
				if (strcmp(jstr(c, "type"), "text")) continue;
				t = jstr(c, "text");
				s = realloc(s, n + strlen(t) + 1);
				memcpy(s + n, t, strlen(t) + 1);
				n += strlen(t);
			}
		}
		break;
	}
	default:
		t = jstr(root, "stop_reason");
		if (!strcmp(t, "refusal")) *refused = 1;
		if (!strcmp(t, "max_tokens")) stop = "max";
		cJSON_ArrayForEach(c, cJSON_GetObjectItem(root, "content"))
			if (!strcmp(jstr(c, "type"), "text")) { s = strdup(jstr(c, "text")); break; }
		break;
	}
	if (stop) logf_("reply hit the output token limit");
	if (s) { /* reasoning models on own servers can put their thinking first, in <think>...</think> */
		char *te = strstr(s, "</think>");
		if (te && strstr(s, "<think>") && strstr(s, "<think>") < te) memmove(s, te + 8, strlen(te + 8) + 1);
	}
	if (s) { /* keep the outermost {...} */
		char *b = strchr(s, '{'), *e = strrchr(s, '}');
		if (b && e && e > b) { memmove(s, b, (size_t)(e - b) + 1); s[e - b + 1] = 0; }
	}
	return s;
}

/* Runs curl; returns the HTTP status, or -(1000 + curl exit code) when curl itself failed. */
static volatile pid_t curl_pid; /* the request in flight, so brain_cancel() can stop it */

/* Stops the request in flight (you started writing again before its answer was shown). Any thread. */
static volatile pid_t draw_curl_pid; /* the drawing in flight (lane 1), for brain_cancel_draw() */
void brain_cancel(void) {
	pid_t p = curl_pid;
	if (p > 0) kill(p, SIGTERM);
}
void brain_cancel_draw(void) {
	pid_t p = draw_curl_pid;
	if (p > 0) kill(p, SIGTERM);
}

/* The drawing lane's thread has its own request/response files (RUN_DIR/draw-*), so it can run beside a turn. */
static __thread int io_draw;

static const char *io_path(char *buf, size_t n, const char *name) {
	snprintf(buf, n, RUN_DIR "/%s%s", io_draw ? "draw-" : "", name);
	return buf;
}

/* allow_http: the own server's address may be http:// (its address was checked when it was set; nothing else is) */
static int run_curl_ex(const char *url, int post, int max_time, int allow_http) {
	char f_err[96], f_resp[96], f_hdr[96], f_req[96], at_hdr[100], at_req[100];
	io_path(f_err, sizeof f_err, "curl.err");
	io_path(f_resp, sizeof f_resp, "resp.json");
	snprintf(at_hdr, sizeof at_hdr, "@%s", io_path(f_hdr, sizeof f_hdr, "hdr.txt"));
	snprintf(at_req, sizeof at_req, "@%s", io_path(f_req, sizeof f_req, "req.json"));
	int pfd[2];
	if (pipe(pfd) != 0) return -1001;
	pid_t pid = fork();
	if (pid == 0) {
		dup2(pfd[1], 1);
		close(pfd[0]);
		close(pfd[1]);
		int ef = open(f_err, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (ef >= 0) dup2(ef, 2);
		char mt[16];
		snprintf(mt, sizeof mt, "%d", max_time);
		const char *proto = allow_http && !strncmp(url, "http://", 7) ? "=http,https" : "=https";
		if (post)
			execl(CURL, "curl", "-q", "-sS", "--proto", proto, "--max-time", mt, "--connect-timeout", "20", "--max-filesize",
			      MAX_RESP, "-o", f_resp, "-w", "%{http_code}", "-H", at_hdr, "--data-binary", at_req, url, (char *)NULL);
		else
			execl(CURL, "curl", "-q", "-sS", "--proto", proto, "--max-time", mt, "--connect-timeout", "20", "--max-filesize",
			      MAX_RESP, "-o", f_resp, "-w", "%{http_code}", "-H", at_hdr, url, (char *)NULL);
		_exit(127);
	}
	if (!io_draw) curl_pid = pid; /* brain_cancel stops the turn's request, never a drawing */
	else draw_curl_pid = pid;
	close(pfd[1]);
	char code[16] = { 0 };
	ssize_t r = read(pfd[0], code, sizeof code - 1);
	(void)r;
	close(pfd[0]);
	int st = 0;
	waitpid(pid, &st, 0);
	if (!io_draw) curl_pid = 0;
	else draw_curl_pid = 0;
	int ex = WIFEXITED(st) ? WEXITSTATUS(st) : 99;
	int http = atoi(code);
	if (ex != 0 && http == 0) return -(1000 + ex);
	return http;
}

static int run_curl(const char *url) { return run_curl_ex(url, 1, 170, 0); }

static int ai_request(AiJob *j, const char *img64, char **body, size_t *len);

static char *page_png64(AiJob *j) {
	Buf png = { 0 };
	if (!stbi_write_png_to_func(png_cb, &png, j->w, j->h, 1, j->px, j->w)) return NULL;
	char *img64 = b64(png.p, png.n);
	free(png.p);
	return img64;
}

/* Drawing pass (net lane 1): text only, the picture's description in, pen strokes out. */
int brain_draw_run(void *arg, char **body, size_t *len) {
	io_draw = 1; /* this thread is the drawing lane */
	*body = NULL;
	*len = 0;
	return ai_request(arg, NULL, body, len);
}

int brain_run(void *arg, char **body, size_t *len) {
	AiJob *j = arg;
	*body = NULL;
	*len = 0;
	char *img64 = page_png64(j);
	if (!img64) return -2001;
	int st = ai_request(j, img64, body, len);
	free(img64);
	return st;
}

/* Email this page (worker): the AI writes a subject and a plain-text body from the page image, then the user's own
 * mail account sends it with the page attached (SMTP through BIN_DIR/curl-smtp, email.c), or Resend's API.
 * Returns 200 and {"subject":...} or an error status and {"error":...}. */
static char *json_msg(const char *k, const char *v, size_t *len) {
	cJSON *o = cJSON_CreateObject();
	cJSON_AddStringToObject(o, k, v);
	char *s = cJSON_PrintUnformatted(o);
	cJSON_Delete(o);
	*len = s ? strlen(s) : 0;
	return s;
}

static int email_smtp(AiJob *j, const char *subject, const char *text, const char *img64, char **body, size_t *len);
static int email_resend(AiJob *j, const char *subject, const char *text, const char *img64, char **body, size_t *len);

int brain_email_run(void *arg, char **body, size_t *len) {
	AiJob *j = arg;
	*body = NULL;
	*len = 0;
	char *img64 = page_png64(j);
	if (!img64) return -2001;
	char *ai = NULL;
	size_t ailen = 0;
	int st = ai_request(j, img64, &ai, &ailen);
	char subject[200] = "Notebook page", *text = NULL;
	cJSON *root = ai ? cJSON_Parse(ai) : NULL;
	int refused = 0;
	char *inner = st == 200 && root ? answer_text(j->api, root, &refused) : NULL;
	cJSON *rj = inner ? cJSON_Parse(inner) : NULL;
	free(inner);
	const char *sj = cJSON_GetStringValue(cJSON_GetObjectItem(rj, "subject")), *bj = cJSON_GetStringValue(cJSON_GetObjectItem(rj, "body"));
	if (sj && sj[0]) snprintf(subject, sizeof subject, "%s", sj);
	if (bj) text = strdup(bj);
	cJSON_Delete(rj);
	cJSON_Delete(root);
	free(ai);
	if (!text) { free(img64); *body = json_msg("error", st == 200 ? "the AI didn't write the email" : "the AI couldn't be reached", len); return st == 200 ? -2010 : st; }
	/* the email */
	st = !strcasecmp(j->eprov, "resend") ? email_resend(j, subject, text, img64, body, len)
					     : email_smtp(j, subject, text, img64, body, len);
	free(text);
	free(img64);
	return st;
}

/* SMTP: the MIME message goes to RUN_DIR, curl-smtp sends it (login through a 0600 -K file), both are deleted. With
 * SECURE_DIR/fake_email (developer hook) the message is kept as RUN_DIR/last_email.eml and nothing is sent. */
static int email_smtp(AiJob *j, const char *subject, const char *text, const char *img64, char **body, size_t *len) {
	char url[160], name[40], err[200], detail[200], from[200], fname[48];
	int starttls = 1;
	if (email_target(j->eprov, j->ehost, j->eport, url, sizeof url, &starttls, name, sizeof name, err, sizeof err) != 0) {
		*body = json_msg("error", err, len);
		return -2020;
	}
	snprintf(from, sizeof from, "Inkling <%s>", j->euser); /* the account itself (email_from is for Resend only) */
	snprintf(fname, sizeof fname, "page-%d.png", j->page);
	const char *dom = strchr(j->euser, '@');
#ifdef INKLING_DEV
	int fake = access(SECURE_DIR "/fake_email", R_OK) == 0;
#else
	int fake = 0;
#endif
	const char *eml = fake ? RUN_DIR "/last_email.eml" : RUN_DIR "/email.eml";
	if (email_write_mime(eml, from, j->to, subject, text, img64, fname, dom ? dom + 1 : NULL) != 0) {
		unlink(eml);
		*body = json_msg("error", "couldn't write the email", len);
		return -2021;
	}
	if (fake) {
		logf_("email: fake send to %s via %s (%s)", j->to, url, eml);
		*body = json_msg("subject", subject, len);
		return 200;
	}
	SmtpSend s = { .curl = BIN_DIR "/curl-smtp", .cacert = CA_BUNDLE, .cfg = RUN_DIR "/smtp.cfg", .url = url,
		       .starttls = starttls, .user = j->euser, .pass = j->epass, .mail_from = j->euser, .rcpt = j->to,
		       .eml = eml, .name = name, .max_time = 120 };
	int rc = email_smtp_send(&s, err, sizeof err, detail, sizeof detail);
	if (rc == 6 || rc == 7) { /* Wi-Fi may still be waking up: one more try */
		sleep(4);
		rc = email_smtp_send(&s, err, sizeof err, detail, sizeof detail);
	}
	unlink(eml);
	memset(j->epass, 0, sizeof j->epass);
	if (rc == 0) {
		*body = json_msg("subject", subject, len);
		return 200;
	}
	logf_("email: %s failed: %s", url, detail);
	*body = json_msg("error", err, len);
	return -(1000 + rc);
}

/* Resend (resend.com) over HTTPS with the system curl. */
static int email_resend(AiJob *j, const char *subject, const char *text, const char *img64, char **body, size_t *len) {
	cJSON *m = cJSON_CreateObject();
	cJSON_AddStringToObject(m, "from", j->from[0] ? j->from : "Inkling <onboarding@resend.dev>");
	cJSON *to = cJSON_AddArrayToObject(m, "to");
	cJSON_AddItemToArray(to, cJSON_CreateString(j->to));
	cJSON_AddStringToObject(m, "subject", subject);
	cJSON_AddStringToObject(m, "text", text);
	cJSON *att = cJSON_AddArrayToObject(m, "attachments"), *f = cJSON_CreateObject();
	char fname[64];
	snprintf(fname, sizeof fname, "page-%d.png", j->page);
	cJSON_AddStringToObject(f, "filename", fname);
	cJSON_AddStringToObject(f, "content", img64);
	cJSON_AddItemToArray(att, f);
	char *req = cJSON_PrintUnformatted(m);
	cJSON_Delete(m);
	if (!req) return -2002;
#ifdef INKLING_DEV
	if (access(SECURE_DIR "/fake_email", R_OK) == 0) { /* developer hook: don't really send */
		write_file(RUN_DIR "/last_email.json", req, strlen(req), 0600);
		free(req);
		*body = json_msg("subject", subject, len);
		return 200;
	}
#endif
	char hdr[300];
	int hn = snprintf(hdr, sizeof hdr, "Content-Type: application/json\nAuthorization: Bearer %s\n", j->rkey);
	int st = -2003;
	if (write_file(RUN_DIR "/hdr.txt", hdr, (size_t)hn, 0600) == 0 && write_file(RUN_DIR "/req.json", req, strlen(req), 0600) == 0)
		st = run_curl("https://api.resend.com/emails");
	memset(hdr, 0, sizeof hdr);
	unlink(RUN_DIR "/hdr.txt");
	unlink(RUN_DIR "/req.json");
	free(req);
	char *resp = read_file(RUN_DIR "/resp.json", NULL);
	unlink(RUN_DIR "/resp.json");
	if (st == 200) *body = json_msg("subject", subject, len);
	else {
		cJSON *r = resp ? cJSON_Parse(resp) : NULL;
		const char *msg = cJSON_GetStringValue(cJSON_GetObjectItem(r, "message"));
		char e[200];
		snprintf(e, sizeof e, "%s", msg ? msg : st < 0 ? "no connection" : st == 401 || st == 403 ? "Resend refused the key" : "the email service refused it");
		*body = json_msg("error", e, len);
		cJSON_Delete(r);
	}
	free(resp);
	return st;
}

/* What Send > Email this page still needs, for the status line; NULL when email is set up. */
const char *brain_email_missing(App *a) {
	int resend = !strcasecmp(a->email_provider, "resend");
	if (resend && !key_get("resend_api_key")[0]) return "set up email first: add a Resend key in Settings > Email";
	if (!a->email_address[0] && !(resend && a->email_to[0])) return "set up email first: add your email address in Settings > Email";
	if (!resend && !key_get("email_password")[0]) return "set up email first: add an app password in Settings > Email (see README)";
	if (!strcasecmp(a->email_provider, "custom") && !a->smtp_server[0]) return "set up email first: add your mail server in Settings > Email";
	return NULL;
}

/* Main thread: a job for Send > Email this page (NULL if email isn't set up: see brain_email_missing). The provider,
 * account and recipient come from the App (config.txt / Settings > Email), the password or Resend key from the key
 * store, read now so a change in Settings counts straight away. */
AiJob *brain_prepare_email(App *a) {
	if (brain_email_missing(a)) return NULL;
	AiJob *j = brain_prepare(a);
	snprintf(j->mode, sizeof j->mode, "email");
	free(j->history);
	j->history = NULL;
	j->bx1 = j->bx0 - 1;
	job_route(a, j, FEAT_EMAIL); /* Claude: Haiku unless you chose Opus */
	snprintf(j->eprov, sizeof j->eprov, "%s", a->email_provider[0] ? a->email_provider : "gmail");
	snprintf(j->ehost, sizeof j->ehost, "%s", a->smtp_server);
	j->eport = a->smtp_port;
	snprintf(j->euser, sizeof j->euser, "%s", a->email_address);
	if (!strcasecmp(j->eprov, "resend")) snprintf(j->rkey, sizeof j->rkey, "%s", key_get("resend_api_key"));
	else {
		/* the big providers' app passwords have no spaces; they are shown in groups ("abcd efgh ijkl mnop") */
		const char *pw = key_get("email_password");
		size_t n = 0;
		int custom = !strcasecmp(j->eprov, "custom");
		for (; *pw && n + 1 < sizeof j->epass; pw++)
			if (custom || *pw != ' ') j->epass[n++] = *pw;
		j->epass[n] = 0;
	}
	snprintf(j->to, sizeof j->to, "%s", brain_email_to(a));
	snprintf(j->from, sizeof j->from, "%s", a->email_from);
	return j;
}

/* Where Send > Email this page sends: email_to, else your own address. */
const char *brain_email_to(App *a) { return a->email_to[0] ? a->email_to : a->email_address; }

/* The URL and the header file (with the key) for one request to the job's service. Returns the header length. */
/* "Authorization: Bearer <key>\n", or nothing for an own server without a key */
static const char *bearer(AiJob *j, char *b, size_t n) {
	if (j->key[0]) snprintf(b, n, "Authorization: Bearer %s\n", j->key);
	else b[0] = 0;
	return b;
}

static int request_target(AiJob *j, char *url, size_t un, char *hdr, size_t hn) {
	char auth[360];
	int own = j->provider == PROV_CUSTOM;
	switch (j->api) {
	case API_OPENAI_RESPONSES:
		snprintf(url, un, "%s/responses", j->base_url);
		return snprintf(hdr, hn, "Content-Type: application/json\n%s", bearer(j, auth, sizeof auth));
	case API_GEMINI:
		snprintf(url, un, "%s/models/%s:generateContent", j->base_url, j->model);
		return snprintf(hdr, hn, "Content-Type: application/json\nx-goog-api-key: %s\n", j->key);
	case API_BEDROCK: {
		char m[192];
		size_t k = 0;
		for (const char *c = j->model; *c && k < sizeof m - 4; c++) { /* the ':' in a model ID is sent as %3A */
			if (*c == ':') { memcpy(m + k, "%3A", 3); k += 3; }
			else m[k++] = *c;
		}
		m[k] = 0;
		snprintf(url, un, "https://bedrock-runtime.%s.amazonaws.com/model/%s/converse", j->region, m);
		return snprintf(hdr, hn, "Content-Type: application/json\nAuthorization: Bearer %s\n", j->key);
	}
	case API_CHAT:
		snprintf(url, un, "%s/chat/completions", j->base_url);
		return snprintf(hdr, hn, "Content-Type: application/json\n%s", bearer(j, auth, sizeof auth));
	default:
		snprintf(url, un, "%s/messages", j->base_url);
		if (j->key[0]) snprintf(auth, sizeof auth, "x-api-key: %s\n", j->key);
		else auth[0] = 0;
		return snprintf(hdr, hn, "content-type: application/json\n%santhropic-version: " ANTHROPIC_VERSION "\n%s", auth,
				!own && model_has_fallbacks(j->model) ? "anthropic-beta: server-side-fallback-2026-07-01\n" : "");
	}
}

static int ai_request(AiJob *j, const char *img64, char **body, size_t *len) {
	char *req = build_request(j, img64);
	if (!req) return -2002;
	char hdr[768], url[384];
	int hn = request_target(j, url, sizeof url, hdr, sizeof hdr);
	int st = -2003;
	char f_hdr[96], f_req[96], f_resp[96];
	io_path(f_hdr, sizeof f_hdr, "hdr.txt");
	io_path(f_req, sizeof f_req, "req.json");
	io_path(f_resp, sizeof f_resp, "resp.json");
#ifdef INKLING_DEV
	/* developer hook: a root-only canned response replaces the API call (the request is kept for inspection);
	 * SECURE_DIR/fake_draw.json answers drawing passes */
	const char *fake = io_draw && access(SECURE_DIR "/fake_draw.json", R_OK) == 0 ? SECURE_DIR "/fake_draw.json"
			   : access(SECURE_DIR "/fake_response.json", R_OK) == 0 ? SECURE_DIR "/fake_response.json" : NULL;
	if (fake) {
		char f_last[96];
		write_file(io_path(f_last, sizeof f_last, "last_req.json"), req, strlen(req), 0600);
		free(req);
		memset(hdr, 0, sizeof hdr);
		*body = read_file(fake, len);
		sleep(1);
		return 200;
	}
#endif
	if (hn > 0 && hn < (int)sizeof hdr && write_file(f_hdr, hdr, (size_t)hn, 0600) == 0 &&
	    write_file(f_req, req, strlen(req), 0600) == 0) {
		int json_dropped = 0;
		for (int adapt = 0; adapt < 4; adapt++) {
			for (int attempt = 0; attempt < 2; attempt++) {
				st = run_curl_ex(url, 1, j->timeout > 0 ? j->timeout : 170, j->provider == PROV_CUSTOM);
				/* one retry for overload / gateway errors and dropped connections */
				if (!(st == 429 || st == 500 || st == 502 || st == 503 || st == 504 || st == 529 || st == -1007 || st == -1056)) break;
				sleep(attempt ? 0 : 4);
			}
			/* Advanced AI settings a model or server doesn't take: ask again without them (a thinking level the model
			 * doesn't do; the own server rejecting the reply schema) */
			if (st != 400 && st != 422) break;
			char *eb = read_file(f_resp, NULL);
			int fixed = 0;
			if (j->temp_set && eb && ci_contains(eb, "temperature")) {
				logf_("%s doesn't take a temperature: asking again without it", j->model);
				j->temp_set = 0;
				fixed = 1;
			} else if (j->effort_set && eb && (ci_contains(eb, "reasoning") || ci_contains(eb, "effort") || ci_contains(eb, "thinking"))) {
				logf_("%s doesn't take a thinking level: asking again without it", j->model);
				j->effort_set = 0;
				fixed = 1;
			} else if (j->json_auto && j->json_mode > 0) {
				logf_("the server didn't take the reply schema (%d): asking again with it in the instructions", st);
				j->json_mode = 0;
				fixed = 2;
			}
			free(eb);
			if (!fixed) break;
			free(req);
			req = build_request(j, img64);
			if (!req || write_file(f_req, req, strlen(req), 0600) != 0) { st = -2002; break; }
			json_dropped |= fixed == 2;
		}
		if (json_dropped && st == 200) server_json_learned[j->slot] = 0; /* the next requests start without the schema */
	}
	memset(hdr, 0, sizeof hdr);
	unlink(f_hdr);
	unlink(f_req);
	free(req);
	*body = read_file(f_resp, len);
	unlink(f_resp);
	return st;
}

/* ---- "Test key" ---- */
struct KeyCheck {
	int prov, api, slot;
	char key[330], url[256], who[32], region[24], model[64];
};

/* For the own server: "Test" lists its models (GET <address>/models, which OpenAI- and Anthropic-style servers have),
 * with or without a key. */
KeyCheck *brain_key_check_prepare(App *a, const char *provider, const char *key) {
	const Provider *p = provider_find(provider);
	if (!p) return NULL;
	if (!key || !key[0]) key = key_get(p->key_name);
	if (!key[0] && !p->custom) return NULL;
	if (p->custom && !provider_custom_slot(p->slot)->url[0]) return NULL;
	KeyCheck *k = calloc(1, sizeof *k);
	k->prov = p->prov;
	k->api = p->api;
	snprintf(k->key, sizeof k->key, "%s", key);
	snprintf(k->who, sizeof k->who, "%s", p->short_name);
	snprintf(k->region, sizeof k->region, "%s", a->bedrock_region);
	if (p->custom) {
		k->slot = p->slot;
		snprintf(k->url, sizeof k->url, "%s/models", provider_custom_slot(p->slot)->url);
		snprintf(k->model, sizeof k->model, "%s", provider_custom_slot(p->slot)->model);
		return k;
	}
	/* listing calls: free, need a valid key, generate nothing */
	switch (p->api) {
	case API_GEMINI: snprintf(k->url, sizeof k->url, "%s/models?pageSize=1", p->base_url); break;
	case API_BEDROCK:
		snprintf(k->url, sizeof k->url, "https://bedrock.%s.amazonaws.com/foundation-models?byProvider=amazon", k->region);
		break;
	case API_ANTHROPIC: snprintf(k->url, sizeof k->url, "%s/models?limit=1", p->base_url); break;
	default: /* OpenRouter's model list is public, its key endpoint is not */
		snprintf(k->url, sizeof k->url, "%s/%s", p->base_url, p->prov == PROV_OPENROUTER ? "key" : "models");
	}
	return k;
}

void brain_key_check_free(KeyCheck *k) {
	if (!k) return;
	memset(k->key, 0, sizeof k->key);
	free(k);
}

int brain_key_check_run(void *arg, char **body, size_t *len) {
	KeyCheck *k = arg;
	char hdr[512];
	int hn;
	if (!k->key[0]) hn = snprintf(hdr, sizeof hdr, "Accept: application/json\n"); /* an own server without a key */
	else if (k->api == API_ANTHROPIC) hn = snprintf(hdr, sizeof hdr, "x-api-key: %s\nanthropic-version: " ANTHROPIC_VERSION "\n", k->key);
	else if (k->api == API_GEMINI) hn = snprintf(hdr, sizeof hdr, "x-goog-api-key: %s\n", k->key);
	else hn = snprintf(hdr, sizeof hdr, "Authorization: Bearer %s\n", k->key);
	*body = NULL;
	*len = 0;
	int st = -2003;
	if (hn > 0 && hn < (int)sizeof hdr && write_file(RUN_DIR "/hdr.txt", hdr, (size_t)hn, 0600) == 0)
		st = run_curl_ex(k->url, 0, 25, k->prov == PROV_CUSTOM);
	memset(hdr, 0, sizeof hdr);
	unlink(RUN_DIR "/hdr.txt");
	*body = read_file(RUN_DIR "/resp.json", len);
	unlink(RUN_DIR "/resp.json");
	return st;
}

static const char *err_message(cJSON *root);
static int key_rejected(int status, const char *m) {
	/* 401 everywhere; Gemini and xAI answer a bad key with 400, Bedrock with 403 */
	return status == 401 || ((status == 400 || status == 403) && (ci_contains(m, "api key") || ci_contains(m, "api_key"))) ||
	       (status == 403 && (ci_contains(m, "security token") || ci_contains(m, "bearer token")));
}

/* The own server's answer to GET /models: remembers the model names for Settings and says whether the chosen model is
 * among them. */
static int server_check_result(KeyCheck *k, int status, cJSON *root, const char *m, char *msg, size_t n) {
	int ce = -status - 1000;
	if (status < 0) {
		if (ce == 6) snprintf(msg, n, "couldn't find %s: check the address (curl 6)", k->who);
		else if (ce == 7) snprintf(msg, n, "%s didn't answer: is it running, and open to the network? (curl 7)", k->who);
		else if (ce == 35 || ce == 60) snprintf(msg, n, "%s's HTTPS certificate isn't trusted (curl %d)", k->who, ce);
		else if (ce == 28) snprintf(msg, n, "%s took too long to answer (curl 28)", k->who);
		else snprintf(msg, n, "couldn't reach %s (curl %d)", k->who, ce);
		return 0;
	}
	if (status == 401 || status == 403) { snprintf(msg, n, "%s wants a key, or rejected this one: %.60s", k->who, m[0] ? m : "not allowed"); return 0; }
	if (status == 404) { snprintf(msg, n, "%s answers, but has no models list here: does the address end in /v1?", k->who); return 0; }
	if (status != 200) { snprintf(msg, n, "%s error %d: %.70s", k->who, status, m); return 0; }
	int sl = k->slot >= 0 && k->slot < NSERVERS ? k->slot : 0;
	nserver_models[sl] = 0;
	cJSON *it, *list = cJSON_GetObjectItem(root, "data");
	int has = 0, total = 0;
	cJSON_ArrayForEach(it, list) {
		const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(it, "id"));
		if (!id || !id[0] || strlen(id) >= sizeof server_models[0][0]) continue;
		total++;
		if (k->model[0] && !strcmp(id, k->model)) has = 1;
		if (nserver_models[sl] < 12) {
			snprintf(server_models[sl][nserver_models[sl]], sizeof server_models[sl][0], "%s", id);
			server_model_ids[sl][nserver_models[sl]] = server_models[sl][nserver_models[sl]];
			nserver_models[sl]++;
		}
	}
	if (!total) snprintf(msg, n, "%s answers (it didn't list any models)", k->who);
	else if (!k->model[0]) snprintf(msg, n, "%s answers: %d model%s - pick one below", k->who, total, total == 1 ? "" : "s");
	else if (has) snprintf(msg, n, "%s answers and has %s", k->who, k->model);
	else snprintf(msg, n, "%s answers, but %s isn't in its list - pick one below", k->who, k->model);
	return 1;
}

int brain_key_check_result(KeyCheck *k, int status, const char *body, size_t len, char *msg, size_t n) {
	(void)len;
	cJSON *root = body ? cJSON_Parse(body) : NULL;
	const char *m = root ? err_message(root) : "";
	int ok = 0;
	if (k->prov == PROV_CUSTOM) {
		ok = server_check_result(k, status, root, m, msg, n);
		cJSON_Delete(root);
		return ok;
	}
	if (status < 0) snprintf(msg, n, "couldn't reach %s (no internet connection? curl %d)", k->who, -status - 1000);
	else if (status == 200) {
		ok = 1;
		if (k->api == API_BEDROCK) snprintf(msg, n, "the Bedrock key works (region %s)", k->region);
		else snprintf(msg, n, "the %s key works", k->who);
	} else if (k->api == API_BEDROCK && status == 403 && ci_contains(m, "not authorized")) {
		ok = 1; /* the key signed in, but its policy doesn't allow listing models; replies can still work */
		snprintf(msg, n, "the Bedrock key is valid (it may not list models; that's fine)");
	} else if (key_rejected(status, m)) snprintf(msg, n, "%s rejected this key: %.70s", k->who, m[0] ? m : "not valid");
	else if (status == 429) { ok = 1; snprintf(msg, n, "the %s key works (busy right now)", k->who); }
	else snprintf(msg, n, "%s error %d: %.70s", k->who, status, m);
	cJSON_Delete(root);
	return ok;
}

int brain_key_check(App *a, const char *provider, const char *key, char *msg, size_t n) {
	if (net_busy()) { snprintf(msg, n, "busy - try again when the reply is done"); return -1; }
	KeyCheck *k = brain_key_check_prepare(a, provider, key);
	if (!k) { snprintf(msg, n, "no key to test"); return 0; }
	char *body = NULL;
	size_t len = 0;
	int st = brain_key_check_run(k, &body, &len);
	int ok = brain_key_check_result(k, st, body, len, msg, n);
	logf_("key check %s: %s", provider, ok ? "ok" : "failed");
	free(body);
	brain_key_check_free(k);
	return ok;
}

/* ---- main thread: parse, history, export ---- */
static const char *err_message(cJSON *root) {
	cJSON *e = cJSON_GetObjectItem(root, "error");
	const char *m = cJSON_GetStringValue(cJSON_GetObjectItem(e, "message")); /* Anthropic, OpenAI, Gemini, OpenRouter */
	if (!m) m = cJSON_GetStringValue(e);                                       /* xAI */
	if (!m) m = cJSON_GetStringValue(cJSON_GetObjectItem(root, "message"));  /* Bedrock ("Message" too), Mistral */
	cJSON *d = cJSON_GetObjectItem(root, "detail");                           /* Mistral */
	if (!m) m = cJSON_GetStringValue(d);
	if (!m && cJSON_IsArray(d)) m = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(d, 0), "msg"));
	return m ? m : "";
}

int brain_parse(App *a, AiJob *j, int status, const char *body, size_t len, Reply *out) {
	(void)len;
	const Provider *pv = job_provider(j);
	const char *who = pv ? pv->short_name : "the AI";
	int own = j->provider == PROV_CUSTOM;
	const char *feat = feature_at(j->feat) ? feature_at(j->feat)->label : "this feature";
	if (status < 0) {
		int ce = -status - 1000;
		if (status <= -2000) ui_set_status(a, "could not build the request (%d)", status);
		else if (own && (ce == 6 || ce == 7)) ui_set_status(a, "couldn't reach %s (curl %d) - is it running? Check its address in Settings > AI", who, ce);
		else if (own && (ce == 35 || ce == 60)) ui_set_status(a, "%s's HTTPS certificate isn't trusted (curl %d); Inkling needs a valid one", who, ce);
		else if (ce == 6 || ce == 7) ui_set_status(a, "no internet connection? (curl %d) - check Wi-Fi, then tap Send", ce);
		else if (ce == 28 && (own || j->customized))
			ui_set_status(a, "%s took over %d s; a smaller model (Settings > AI > Each feature) may help - tap Send to retry", who, j->timeout);
		else if (ce == 28) ui_set_status(a, "%s took too long to answer; tap Send to try again", who);
		else ui_set_status(a, "network error (curl %d); tap Send to try again", ce);
		return 0;
	}
	cJSON *root = body ? cJSON_Parse(body) : NULL;
	if (status != 200) {
		const char *m = root ? err_message(root) : "";
		if (!m[0] && body && own) m = body; /* some servers answer errors in plain text */
		if (key_rejected(status, m)) ui_set_status(a, "%s rejected the API key - check it in Settings or config.txt", who);
		else if (status == 429 || status == 529) ui_set_status(a, "%s is busy or at a usage limit; try again soon", who);
		else if ((own || j->customized) && (ci_contains(m, "image") || ci_contains(m, "vision") || ci_contains(m, "multimodal")))
			ui_set_status(a, "%s can't read images, so it can't read your page - pick another model in Settings > AI", j->model);
		else if ((own || j->customized) && (status == 400 || status == 404 || status == 422))
			ui_set_status(a, "%s's AI setting failed (%s %d: %.60s) - check Settings > AI > Each feature", feat, who, status, m);
		else ui_set_status(a, "%s error %d: %.90s", who, status, m);
		logf_("API error %d: %.400s", status, body ? body : "");
		cJSON_Delete(root);
		return 0;
	}
	int refused = 0;
	char *inner = root ? answer_text(j->api, root, &refused) : NULL;
	if (refused) { ui_set_status(a, "%s declined to answer this page", who); free(inner); cJSON_Delete(root); return 0; }
	cJSON *rj = inner ? cJSON_Parse(inner) : NULL;
	free(inner);
	const char *rd = cJSON_GetStringValue(cJSON_GetObjectItem(rj, "read"));
	const char *md = cJSON_GetStringValue(cJSON_GetObjectItem(rj, "mode"));
	const char *sy = cJSON_GetStringValue(cJSON_GetObjectItem(rj, "style"));
	const char *rp = cJSON_GetStringValue(cJSON_GetObjectItem(rj, "reply"));
	const char *mm = cJSON_GetStringValue(cJSON_GetObjectItem(rj, "memo"));
	int ok = rp && rp[0];
	if (ok) {
		snprintf(out->transcript, sizeof out->transcript, "%s", rd ? rd : "");
		snprintf(out->mode, sizeof out->mode, "%s", md ? md : j->mode);
		snprintf(out->style, sizeof out->style, "%s", sy ? sy : "script");
		out->reply = strdup(rp);
		snprintf(out->memo, sizeof out->memo, "%s", mm ? mm : "");
		const char *il = cJSON_GetStringValue(cJSON_GetObjectItem(rj, "illustrate"));
		snprintf(out->illustrate, sizeof out->illustrate, "%s", il ? il : "");
	} else {
		ui_set_status(a, "%s sent an empty or unreadable reply; tap Send to try again", who);
		logf_("unparsed reply: %.400s", body ? body : "");
	}
	cJSON_Delete(rj);
	cJSON_Delete(root);
	return ok;
}

void brain_commit(App *a, AiJob *j, Reply *r) {
	char u[4400];
	snprintf(u, sizeof u, "[page %d, %s mode] %s", j->page, j->mode, r->transcript);
	cJSON *m = cJSON_CreateObject();
	cJSON_AddStringToObject(m, "role", "user");
	cJSON_AddStringToObject(m, "text", u);
	cJSON_AddItemToArray(hist, m);
	m = cJSON_CreateObject();
	cJSON_AddStringToObject(m, "role", "assistant");
	if (r->memo[0] || r->illustrate[0]) {
		size_t ln = strlen(r->reply) + strlen(r->memo) + strlen(r->illustrate) + 64;
		char *t = malloc(ln);
		snprintf(t, ln, "%s%s%s%s%s%s", r->reply, r->illustrate[0] ? "\n[I drew: " : "", r->illustrate,
			 r->illustrate[0] ? "]" : "", r->memo[0] ? "\n[my private memo: " : "", r->memo[0] ? r->memo : "");
		if (r->memo[0]) strcat(t, "]");
		cJSON_AddStringToObject(m, "text", t);
		free(t);
	} else cJSON_AddStringToObject(m, "text", r->reply);
	cJSON_AddItemToArray(hist, m);
	while (cJSON_GetArraySize(hist) > HIST_MAX) {
		cJSON_DeleteItemFromArray(hist, 0);
		cJSON_DeleteItemFromArray(hist, 0);
	}
	char *s = cJSON_PrintUnformatted(hist);
	char hp[256];
	history_path(a, hp, sizeof hp);
	if (s) { write_file(hp, s, strlen(s), 0600); free(s); }

	/* human-readable export on the USB storage */
	char ep[256], ts[32];
	snprintf(ep, sizeof ep, USB_DIR "/notebooks/%s.md", a->notebook);
	time_t now = time(NULL);
	strftime(ts, sizeof ts, "%Y-%m-%d %H:%M", localtime(&now));
	FILE *f = fopen(ep, "a");
	if (f) {
		fprintf(f, "\n## Page %d, %s (%s)\n\n**You wrote:**\n\n> ", j->page, ts, r->mode);
		for (const char *c = r->transcript; *c; c++) fputs(*c == '\n' ? "\n> " : (char[2]){ *c, 0 }, f);
		fprintf(f, "\n\n**Reply:**\n\n%s\n", r->reply);
		fclose(f);
	}
}

/* Sets one key in config.txt (replacing its line, or adding it). */
void config_set(const char *key, const char *value) {
	size_t n = 0;
	char *c = read_file(USB_DIR "/config.txt", &n);
	if (!c) { c = strdup(""); n = 0; }
	char *out = malloc(n + strlen(key) + strlen(value) + 16);
	size_t on = 0;
	int done = 0;
	for (char *p = c; *p;) {
		char *nl = strchr(p, '\n');
		size_t ll = nl ? (size_t)(nl - p) : strlen(p);
		char ln[512], k[64];
		snprintf(ln, sizeof ln, "%.*s", (int)(ll < sizeof ln - 1 ? ll : sizeof ln - 1), p);
		trim(ln);
		char *eq = strchr(ln, '=');
		int match = 0;
		if (!done && ln[0] != '#' && eq) {
			*eq = 0;
			snprintf(k, sizeof k, "%s", ln);
			trim(k);
			match = !strcmp(k, key);
		}
		if (match) { on += (size_t)sprintf(out + on, "%s = %s\n", key, value); done = 1; }
		else { memcpy(out + on, p, ll); on += ll; out[on++] = '\n'; }
		p = nl ? nl + 1 : p + ll;
	}
	if (!done) on += (size_t)sprintf(out + on, "%s = %s\n", key, value);
	if (write_file(USB_DIR "/config.txt", out, on, 0644) != 0) logf_("could not save %s to config.txt", key);
	free(out);
	free(c);
}

/* Forget the conversation: the AI's history for this notebook is wiped. archive=1 also starts a new text record
 * (the old notebooks/<name>.md is renamed with the date, never deleted). */
void brain_forget(App *a, int archive) {
	cJSON_Delete(hist);
	hist = cJSON_CreateArray();
	char hp[256];
	history_path(a, hp, sizeof hp);
	unlink(hp);
	char ep[256], ts[32];
	snprintf(ep, sizeof ep, USB_DIR "/notebooks/%s.md", a->notebook);
	time_t now = time(NULL);
	if (archive) {
		char ap[300];
		strftime(ts, sizeof ts, "%Y-%m-%d-%H%M", localtime(&now));
		snprintf(ap, sizeof ap, USB_DIR "/notebooks/%s-until-%s.md", a->notebook, ts);
		rename(ep, ap);
	} else {
		strftime(ts, sizeof ts, "%Y-%m-%d %H:%M", localtime(&now));
		FILE *f = fopen(ep, "a");
		if (f) { fprintf(f, "\n---\n\n*Conversation forgotten (%s): the AI starts fresh from here.*\n", ts); fclose(f); }
	}
	logf_("conversation forgotten%s", archive ? " (text record archived)" : "");
}

void brain_new_page(App *a) {
	char ep[256];
	snprintf(ep, sizeof ep, USB_DIR "/notebooks/%s.md", a->notebook);
	FILE *f = fopen(ep, "a");
	if (f) { fprintf(f, "\n---\n"); fclose(f); }
}

/* ---- drawing pass ---- */

/* The model that draws: each service's strongest model at low effort (about 10 s for a detailed sketch; the quick
 * chat models draw stick figures). Bedrock and Mistral keep the chosen model: model access varies by account. */
static const char *draw_model(int prov) {
	switch (prov) {
	case PROV_ANTHROPIC: return "claude-opus-5-5";
	case PROV_OPENAI: return "gpt-6-astra";
	case PROV_GEMINI: return "gemini-4-argon";
	case PROV_XAI: return "grok-4.7";
	case PROV_OPENROUTER: return "anthropic/claude-sonnet-5.5";
	default: return NULL;
	}
}

AiJob *brain_prepare_draw(App *a, const char *desc, int own) {
	if (!desc || !desc[0]) return NULL;
	AiJob *j = calloc(1, sizeof *j);
	job_route(a, j, FEAT_DRAW); /* the service's most capable model, unless Advanced AI says otherwise */
	const Provider *p = provider_by_prov(j->provider);
	if (!p) { brain_job_free(j); return NULL; }
	if (a->feat_model[FEAT_DRAW][0] && !strcmp(j->model, a->feat_model[FEAT_DRAW])) j->draw_own = 1; /* your choice */
	else { /* after a failed drawing: once more with your model for the service */
		const char *mine = brain_model(a, p);
		if (!strcmp(mine, "fast")) mine = "claude-sonnet-5-5";
		if (own) snprintf(j->model, sizeof j->model, "%s", mine);
		j->draw_own = own || !strcmp(j->model, mine);
	}
	snprintf(j->mode, sizeof j->mode, "draw");
	snprintf(j->draw, sizeof j->draw, "%s", desc);
	j->page = a->page;
	j->bx0 = 0;
	j->bx1 = -1;
	j->started = mono_ms();
	logf_("drawing with %s: %.200s", j->model, desc);
	return j;
}

int brain_draw_is_own(AiJob *j) { return j && j->draw_own; }

/* A drawing that ran out of time on a self-hosted server: say what gives it more (1 if msg was written) */
int brain_draw_fail_msg(AiJob *j, int status, char *msg, size_t n) {
	if (!j || status != -1028 || j->provider != PROV_CUSTOM) return 0;
	const Provider *pv = job_provider(j);
	snprintf(msg, n, "%s took over %d s to draw it - a longer wait (Settings > AI, the server's settings) or Thinking off "
		 "for Drawings may help", pv ? pv->short_name : "your server", j->timeout);
	return 1;
}

const char *brain_job_custom_feature(AiJob *j) {
	return j && (j->customized || j->provider == PROV_CUSTOM) && feature_at(j->feat) ? feature_at(j->feat)->label : NULL;
}

int brain_parse_draw(AiJob *j, int status, const char *body, size_t len, Reply *out) {
	(void)len;
	if (status != 200 || !body) { logf_("drawing: error %d: %.300s", status, body ? body : ""); return 0; }
	cJSON *root = cJSON_Parse(body);
	int refused = 0;
	char *inner = root ? answer_text(j->api, root, &refused) : NULL;
	cJSON_Delete(root);
	cJSON *rj = inner ? cJSON_Parse(inner) : NULL;
	free(inner);
	cJSON *dr = cJSON_GetObjectItem(rj, "strokes"), *st;
	int ns = cJSON_IsArray(dr) ? cJSON_GetArraySize(dr) : 0, np = 0, si = 0;
	if (ns > DRAW_MAX_STROKES) ns = DRAW_MAX_STROKES;
	if (ns > 0) {
		out->slen = calloc((size_t)ns, sizeof *out->slen);
		out->dpts = malloc((size_t)ns * DRAW_MAX_POINTS * 2 * sizeof *out->dpts);
		cJSON_ArrayForEach(st, dr) {
			if (si >= ns) break;
			int k = 0;
			cJSON *pt;
			cJSON_ArrayForEach(pt, st) {
				if (k >= DRAW_MAX_POINTS || !cJSON_IsArray(pt) || cJSON_GetArraySize(pt) < 2) continue;
				cJSON *cx = cJSON_GetArrayItem(pt, 0), *cy = cJSON_GetArrayItem(pt, 1);
				if (!cJSON_IsNumber(cx) || !cJSON_IsNumber(cy)) continue;
				double x = cx->valuedouble, y = cy->valuedouble;
				out->dpts[2 * (np + k)] = (float)(x < 0 ? 0 : x > 1000 ? 1000 : x);
				out->dpts[2 * (np + k) + 1] = (float)(y < 0 ? 0 : y > 1000 ? 1000 : y);
				k++;
			}
			if (k >= 1) { out->slen[si++] = k; np += k; }
		}
	}
	cJSON_Delete(rj);
	out->nstrokes = si;
	if (!si) {
		free(out->slen);
		free(out->dpts);
		out->slen = NULL;
		out->dpts = NULL;
		logf_("drawing: no strokes in the answer%s", refused ? " (declined)" : "");
		return 0;
	}
	logf_("drawing: %d strokes, %d points, %.1f s", si, np, (mono_ms() - j->started) / 1000);
	return 1;
}
