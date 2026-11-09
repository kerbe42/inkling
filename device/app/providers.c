/* The AI services the app can use, each with the user's own key, plus the user's own server. The settings page,
 * config.txt, the key store and the request code in brain.c all work from this table. Model IDs are from each vendor's
 * own documentation (November 2026); every model listed reads images. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scribe.h"

static const Provider P[] = {
	{ "anthropic", PROV_ANTHROPIC, "Claude (Anthropic)", "Claude", "anthropic_api_key", "anthropic-key.txt",
	  "console.anthropic.com", "anthropic_model", "fast", 4,
	  { { "fast", "Fastest (Haiku; Sonnet for maths)", 1 },
	    { "claude-sonnet-5-5", "Sonnet 5.5", 2 },
	    { "claude-opus-5-5", "Opus 5.5 - best, slower", 3 },
	    { "claude-haiku-5-5", "Haiku 5.5 only", 1 } },
	  0, NULL, 0, { { NULL, NULL } }, API_ANTHROPIC, "https://api.anthropic.com/v1" },

	{ "openai", PROV_OPENAI, "ChatGPT (OpenAI)", "OpenAI", "openai_api_key", "openai-key.txt", "platform.openai.com",
	  "openai_model", "gpt-6.1-sol", 4,
	  { { "gpt-6-astra", "Astra - most capable", 3 },
	    { "gpt-6.1-sol", "Sol", 2 },
	    { "gpt-6.1-terra", "Terra", 2 },
	    { "gpt-6-luna", "Luna - cheapest", 1 } },
	  0, NULL, 0, { { NULL, NULL } }, API_OPENAI_RESPONSES, "https://api.openai.com/v1" },

	{ "gemini", PROV_GEMINI, "Gemini (Google)", "Gemini", "gemini_api_key", "gemini-key.txt", "aistudio.google.com",
	  "gemini_model", "gemini-3.8-flash", 4,
	  { { "gemini-3.8-flash", "3.8 Flash", 2 },
	    { "gemini-3.5-flash-lite", "3.5 Flash-Lite - fastest, cheapest", 1 },
	    { "gemini-4-argon", "4 Argon - most capable", 3 },
	    { "gemini-3.6-flash", "3.6 Flash", 2 } },
	  0, NULL, 0, { { NULL, NULL } }, API_GEMINI, "https://generativelanguage.googleapis.com/v1beta" },

	/* Bedrock: global cross-Region inference profiles, which every region below can call */
	{ "bedrock", PROV_BEDROCK, "Amazon Bedrock (Nova, Claude)", "Bedrock", "bedrock_api_key", "bedrock-key.txt",
	  "AWS console > Amazon Bedrock > API keys", "bedrock_model", "global.amazon.nova-2-lite-v1:0", 3,
	  { { "global.amazon.nova-2-lite-v1:0", "Amazon Nova 2 Lite", 1 },
	    { "global.anthropic.claude-haiku-5-5", "Claude Haiku 5.5", 1 },
	    { "global.anthropic.claude-sonnet-5-5", "Claude Sonnet 5.5", 2 } },
	  1, "us-east-1", 12,
	  { { "us-east-1", "US East (N. Virginia)" },
	    { "us-east-2", "US East (Ohio)" },
	    { "us-west-2", "US West (Oregon)" },
	    { "ca-central-1", "Canada (Central)" },
	    { "eu-central-1", "Europe (Frankfurt)" },
	    { "eu-west-1", "Europe (Ireland)" },
	    { "eu-west-2", "Europe (London)" },
	    { "eu-west-3", "Europe (Paris)" },
	    { "eu-north-1", "Europe (Stockholm)" },
	    { "ap-northeast-1", "Asia Pacific (Tokyo)" },
	    { "ap-southeast-2", "Asia Pacific (Sydney)" },
	    { "ap-south-1", "Asia Pacific (Mumbai)" } },
	  API_BEDROCK, NULL },

	{ "xai", PROV_XAI, "Grok (xAI)", "Grok", "xai_api_key", "xai-key.txt", "console.x.ai", "xai_model", "grok-4.3", 3,
	  { { "grok-4.3", "Grok 4.3 - fast", 2 },
	    { "grok-4.7", "Grok 4.7 - most capable", 3 },
	    { "grok-4.20-0309-non-reasoning", "Grok 4.20 - no reasoning", 1 } },
	  0, NULL, 0, { { NULL, NULL } }, API_CHAT, "https://api.x.ai/v1" },

	{ "mistral", PROV_MISTRAL, "Mistral", "Mistral", "mistral_api_key", "mistral-key.txt", "console.mistral.ai",
	  "mistral_model", "mistral-large-4", 3,
	  { { "mistral-large-4", "Mistral Large 4", 3 },
	    { "ministral-14b-2512", "Ministral 14B - faster", 2 },
	    { "ministral-8b-2512", "Ministral 8B - cheapest", 1 } },
	  0, NULL, 0, { { NULL, NULL } }, API_CHAT, "https://api.mistral.ai/v1" },

	{ "openrouter", PROV_OPENROUTER, "OpenRouter (many models)", "OpenRouter", "openrouter_api_key",
	  "openrouter-key.txt", "openrouter.ai/keys", "openrouter_model", "google/gemini-3.8-flash", 5,
	  { { "google/gemini-3.8-flash", "Gemini 3.8 Flash", 2 },
	    { "anthropic/claude-haiku-5.5", "Claude Haiku 5.5", 1 },
	    { "anthropic/claude-sonnet-5.5", "Claude Sonnet 5.5", 2 },
	    { "openai/gpt-6.1-sol", "GPT-6.1 Sol", 2 },
	    { "x-ai/grok-4.3", "Grok 4.3", 2 } },
	  0, NULL, 0, { { NULL, NULL } }, API_CHAT, "https://openrouter.ai/api/v1" },
};

#define NBUILTIN ((int)(sizeof P / sizeof P[0]))

/* ---- self-hosted servers, set up once in Settings > AI: any server with an OpenAI-compatible API (Ollama, LM Studio,
 * llama.cpp, vLLM, LiteLLM, a company gateway, or a cloud service's own API with a separate key), or one that speaks
 * Anthropic's Messages API or OpenAI's Responses API. Up to NSERVERS: provider ids "custom", "custom2", "custom3",
 * config custom_*, custom2_*, custom3_*. Each has its own name, address, kind of API, key (in the key store), model,
 * reply format and timeout; any feature can use any of them. ---- */
static CustomServer CS[NSERVERS];
static Provider CUSTOMP[NSERVERS];
static struct {
	char id[16], label[40], key_name[32], key_file[32], model_key[32];
} SL[NSERVERS];

static void default_name(int k, char *out, size_t n) {
	if (k) snprintf(out, n, "Self-hosted %d", k + 1);
	else snprintf(out, n, "Self-hosted");
}

static void slots_init(void) {
	static int done;
	if (done) return;
	done = 1;
	for (int k = 0; k < NSERVERS; k++) {
		CustomServer *c = &CS[k];
		default_name(k, c->name, sizeof c->name);
		snprintf(c->api, sizeof c->api, "openai");
		snprintf(c->json, sizeof c->json, "auto");
		c->timeout = 170;
		if (k) snprintf(SL[k].id, sizeof SL[k].id, "custom%d", k + 1);
		else snprintf(SL[k].id, sizeof SL[k].id, "custom");
		snprintf(SL[k].label, sizeof SL[k].label, "Self-hosted server%s%.0d", k ? " " : "", k ? k + 1 : 0);
		snprintf(SL[k].key_name, sizeof SL[k].key_name, "%s_api_key", SL[k].id);
		snprintf(SL[k].key_file, sizeof SL[k].key_file, "%s-key.txt", SL[k].id);
		snprintf(SL[k].model_key, sizeof SL[k].model_key, "%s_model", SL[k].id);
		CUSTOMP[k] = (Provider){ SL[k].id, PROV_CUSTOM, SL[k].label, c->name, SL[k].key_name, SL[k].key_file,
					 "your server's settings (only if it needs a key)", SL[k].model_key, c->model, 0, { { NULL, NULL, 0 } },
					 0, NULL, 0, { { NULL, NULL, 0 } }, API_CHAT, c->url, 1, k };
	}
}

const CustomServer *provider_custom_slot(int slot) {
	slots_init();
	return slot >= 0 && slot < NSERVERS ? &CS[slot] : &CS[0];
}
const CustomServer *provider_custom(void) { return provider_custom_slot(0); }
const Provider *provider_slot(int slot) {
	slots_init();
	return slot >= 0 && slot < NSERVERS ? &CUSTOMP[slot] : NULL;
}
int provider_custom_ready_slot(int slot) {
	const CustomServer *c = provider_custom_slot(slot);
	return c->url[0] && c->model[0];
}
int provider_custom_ready(void) { return provider_custom_ready_slot(0); }

/* An address for curl: http:// or https://, a host, no spaces, quotes, credentials (user:pass@), query or fragment
 * (paths are added to it). A trailing slash is dropped. */
static int good_url(const char *v, char *out, size_t n) {
	size_t len = strlen(v);
	const char *host = !strncmp(v, "https://", 8) ? v + 8 : !strncmp(v, "http://", 7) ? v + 7 : NULL;
	if (!host || !*host || *host == '/' || *host == ':' || len >= n) return 0;
	for (const char *c = v; *c; c++)
		if (*c <= ' ' || *c > '~' || strchr("\"'`<>\\{}|^@?#", *c)) return 0;
	snprintf(out, n, "%s", v);
	size_t hoff = (size_t)(host - v);
	while (len > hoff + 1 && out[len - 1] == '/') out[--len] = 0;
	return 1;
}

/* A model name goes into the request body (and the log): letters, digits and . _ : / @ + - only. */
static int good_model(const char *v) {
	size_t n = strlen(v);
	if (!n || n > 63) return 0;
	for (const char *c = v; *c; c++)
		if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || strchr("._:/@+-", *c)))
			return 0;
	return 1;
}

void logf_(const char *fmt, ...);

/* custom_<field> (server 1), custom2_<field>, custom3_<field>. Returns the server's slot, or -1 if k isn't one of these
 * settings (keys, custom[N]_api_key, live in the key store). *field gets the field's name. */
int provider_custom_apply(const char *k, const char *v, const char **field) {
	slots_init();
	if (strncmp(k, "custom", 6)) return -1;
	const char *rest = k + 6;
	int slot = 0;
	if (*rest >= '2' && *rest <= '0' + NSERVERS) slot = *rest++ - '1';
	if (*rest++ != '_') return -1;
	CustomServer *c = &CS[slot];
	Provider *pv = &CUSTOMP[slot];
	if (field) *field = rest;
	if (!strcmp(rest, "url")) {
		if (!v[0]) c->url[0] = 0;
		else if (!good_url(v, c->url, sizeof c->url)) logf_("ignoring %s %.80s: not an http(s) address", k, v);
	} else if (!strcmp(rest, "model")) {
		if (!v[0]) c->model[0] = 0;
		else if (good_model(v)) snprintf(c->model, sizeof c->model, "%s", v);
		else logf_("ignoring %s %.60s: not a model name", k, v);
	} else if (!strcmp(rest, "name")) {
		size_t j = 0;
		for (const char *ch = v; *ch && j < sizeof c->name - 1; ch++)
			if (*ch >= ' ' && *ch <= '~') c->name[j++] = *ch;
		c->name[j] = 0;
		if (!j) default_name(slot, c->name, sizeof c->name);
	} else if (!strcmp(rest, "api")) {
		if (!strcmp(v, "openai") || !strcmp(v, "chat")) { snprintf(c->api, sizeof c->api, "openai"); pv->api = API_CHAT; }
		else if (!strcmp(v, "anthropic")) { snprintf(c->api, sizeof c->api, "anthropic"); pv->api = API_ANTHROPIC; }
		else if (!strcmp(v, "responses")) { snprintf(c->api, sizeof c->api, "responses"); pv->api = API_OPENAI_RESPONSES; }
		else if (v[0]) logf_("ignoring %s %.30s: use openai, anthropic or responses", k, v);
	} else if (!strcmp(rest, "json")) {
		if (!strcmp(v, "auto") || !strcmp(v, "schema") || !strcmp(v, "json") || !strcmp(v, "prompt"))
			snprintf(c->json, sizeof c->json, "%s", v);
		else if (v[0]) logf_("ignoring %s %.30s: use auto, schema, json or prompt", k, v);
	} else if (!strcmp(rest, "timeout")) {
		int t = atoi(v);
		if (v[0]) c->timeout = t < 30 ? 30 : t > 600 ? 600 : t;
	} else return -1;
	return slot;
}

/* Every service: the built-in ones, then the self-hosted servers. */
int providers_count(void) { return NBUILTIN + NSERVERS; }

const Provider *provider_at(int i) { return i >= 0 && i < NBUILTIN ? &P[i] : provider_slot(i - NBUILTIN); }

const Provider *provider_find(const char *id) {
	for (int i = 0; id && i < providers_count(); i++)
		if (!strcmp(provider_at(i)->id, id)) return provider_at(i);
	return NULL;
}

const Provider *provider_by_prov(int prov) {
	for (int i = 0; i < providers_count(); i++)
		if (provider_at(i)->prov == prov) return provider_at(i);
	return NULL;
}

/* The label of one of the provider's models ("3.8 Flash"), or NULL for a model ID that isn't in the table. */
const char *provider_model_label(const Provider *p, const char *model) {
	for (int i = 0; p && model && i < p->nmodels; i++)
		if (!strcmp(p->models[i].id, model)) return p->models[i].label;
	return NULL;
}

/* ---- the AI features (each can have its own service, model and thinking level: Settings > Advanced AI) ---- */
static const Feature F[FEAT_COUNT] = {
	{ "chat", "Chat", "Reads your page and writes back (Chat and Auto)", 1 },
	{ "typeup", "Type up", "Turns your handwriting into typed text", 1 },
	{ "math", "Math", "Checks your working step by step", 1 },
	{ "translate", "Translate", "Writes back what you wrote in another language", 1 },
	{ "games", "AI games", "Plays the AI games with you", 1 },
	{ "drawing", "Drawings", "Draws a picture when a reply asks for one (text only, no page image)", 0 },
	{ "email", "Email a page", "Writes the subject and text for Send > Email this page", 1 },
};

const Feature *feature_at(int f) { return f >= 0 && f < FEAT_COUNT ? &F[f] : NULL; }
