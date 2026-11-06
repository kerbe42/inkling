/* The AI services the app can use, each with the user's own key. The settings page, config.txt, the key store and
 * the request code in brain.c all work from this table. Model IDs are from each vendor's own documentation
 * (November 2026); every model listed reads images. */
#include <string.h>

#include "scribe.h"

static const Provider P[] = {
	{ "anthropic", PROV_ANTHROPIC, "Claude (Anthropic)", "Claude", "anthropic_api_key", "anthropic-key.txt",
	  "console.anthropic.com", "anthropic_model", "fast", 4,
	  { { "fast", "Fastest (Haiku; Sonnet for maths)" },
	    { "claude-sonnet-5-5", "Sonnet 5.5" },
	    { "claude-opus-5-5", "Opus 5.5 - best, slower" },
	    { "claude-haiku-5-5", "Haiku 5.5 only" } },
	  0, NULL, 0, { { NULL, NULL } }, API_ANTHROPIC, "https://api.anthropic.com/v1" },

	{ "openai", PROV_OPENAI, "ChatGPT (OpenAI)", "OpenAI", "openai_api_key", "openai-key.txt", "platform.openai.com",
	  "openai_model", "gpt-6.1-sol", 4,
	  { { "gpt-6-astra", "Astra - most capable" },
	    { "gpt-6.1-sol", "Sol" },
	    { "gpt-6.1-terra", "Terra" },
	    { "gpt-6-luna", "Luna - cheapest" } },
	  0, NULL, 0, { { NULL, NULL } }, API_OPENAI_RESPONSES, "https://api.openai.com/v1" },

	{ "gemini", PROV_GEMINI, "Gemini (Google)", "Gemini", "gemini_api_key", "gemini-key.txt", "aistudio.google.com",
	  "gemini_model", "gemini-3.8-flash", 4,
	  { { "gemini-3.8-flash", "3.8 Flash" },
	    { "gemini-3.5-flash-lite", "3.5 Flash-Lite - fastest, cheapest" },
	    { "gemini-4-argon", "4 Argon - most capable" },
	    { "gemini-3.6-flash", "3.6 Flash" } },
	  0, NULL, 0, { { NULL, NULL } }, API_GEMINI, "https://generativelanguage.googleapis.com/v1beta" },

	/* Bedrock: global cross-Region inference profiles, which every region below can call */
	{ "bedrock", PROV_BEDROCK, "Amazon Bedrock (Nova, Claude)", "Bedrock", "bedrock_api_key", "bedrock-key.txt",
	  "AWS console > Amazon Bedrock > API keys", "bedrock_model", "global.amazon.nova-2-lite-v1:0", 3,
	  { { "global.amazon.nova-2-lite-v1:0", "Amazon Nova 2 Lite" },
	    { "global.anthropic.claude-haiku-5-5", "Claude Haiku 5.5" },
	    { "global.anthropic.claude-sonnet-5-5", "Claude Sonnet 5.5" } },
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
	  { { "grok-4.3", "Grok 4.3 - fast" },
	    { "grok-4.7", "Grok 4.7 - most capable" },
	    { "grok-4.20-0309-non-reasoning", "Grok 4.20 - no reasoning" } },
	  0, NULL, 0, { { NULL, NULL } }, API_CHAT, "https://api.x.ai/v1" },

	{ "mistral", PROV_MISTRAL, "Mistral", "Mistral", "mistral_api_key", "mistral-key.txt", "console.mistral.ai",
	  "mistral_model", "mistral-large-4", 3,
	  { { "mistral-large-4", "Mistral Large 4" },
	    { "ministral-14b-2512", "Ministral 14B - faster" },
	    { "ministral-8b-2512", "Ministral 8B - cheapest" } },
	  0, NULL, 0, { { NULL, NULL } }, API_CHAT, "https://api.mistral.ai/v1" },

	{ "openrouter", PROV_OPENROUTER, "OpenRouter (many models)", "OpenRouter", "openrouter_api_key",
	  "openrouter-key.txt", "openrouter.ai/keys", "openrouter_model", "google/gemini-3.8-flash", 5,
	  { { "google/gemini-3.8-flash", "Gemini 3.8 Flash" },
	    { "anthropic/claude-haiku-5.5", "Claude Haiku 5.5" },
	    { "anthropic/claude-sonnet-5.5", "Claude Sonnet 5.5" },
	    { "openai/gpt-6.1-sol", "GPT-6.1 Sol" },
	    { "x-ai/grok-4.3", "Grok 4.3" } },
	  0, NULL, 0, { { NULL, NULL } }, API_CHAT, "https://openrouter.ai/api/v1" },
};

int providers_count(void) { return (int)(sizeof P / sizeof P[0]); }

const Provider *provider_at(int i) { return i >= 0 && i < providers_count() ? &P[i] : NULL; }

const Provider *provider_find(const char *id) {
	for (int i = 0; id && i < providers_count(); i++)
		if (!strcmp(P[i].id, id)) return &P[i];
	return NULL;
}

const Provider *provider_by_prov(int prov) {
	for (int i = 0; i < providers_count(); i++)
		if (P[i].prov == prov) return &P[i];
	return NULL;
}

/* The label of one of the provider's models ("3.8 Flash"), or NULL for a model ID that isn't in the table. */
const char *provider_model_label(const Provider *p, const char *model) {
	for (int i = 0; p && model && i < p->nmodels; i++)
		if (!strcmp(p->models[i].id, model)) return p->models[i].label;
	return NULL;
}
